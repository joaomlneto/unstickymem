#include <numa.h>
#include <numaif.h>
#include <unistd.h>
#include <linux/version.h>
#include <omp.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cinttypes>
#include <chrono>
#include <cmath>
#include <cassert>
#include <initializer_list>

static const size_t N = 2ULL << 28;

// XXX temporary workaround for bug in numactl XXX
// https://github.com/numactl/numactl/issues/38
#ifndef MPOL_LOCAL
#define MPOL_LOCAL 4
#endif

void printmask(const char *name, struct bitmask *mask) {
  printf("%s: ", name);
  for (int i = 0; i < mask->size; i++)
    if (numa_bitmask_isbitset(mask, i))
      printf("%d ", i);
  putchar('\n');
}

/**
  * This function places part of a memory segment identified by its start
  * address `addr` and its length `len` and spreads it between the different
  * NUMA nodes. Places `local_ratio`*len in the local node and the remaining
  * is interleaved between the other NUMA nodes
 **/
void change_membind(void* memory_to_access, unsigned long memory_size, double ratio) {
  int r;
  double local_ratio = ratio - (1 - ratio) / (numa_num_configured_nodes() - 1);
  double interleave_ratio = 1 - local_ratio;
  uint64_t size_to_bind = interleave_ratio * memory_size;
  uint64_t page_align_mask = ~(sysconf(_SC_PAGESIZE) - 1);
  size_to_bind = size_to_bind & page_align_mask;
  //printf("stb=%ld\n", size_to_bind);

  printf("w=%lf ratio_interleave = %lf\n", ratio, interleave_ratio);

  //printf("will call mbind INTERLEAVE on %p with size %ld\n", memory_to_access, size_to_bind);
  // First, uniform interleaving on 2xw2 portion of the array
  printmask("numa_get_mems_allowed", numa_get_mems_allowed());
  //r = mbind(memory_to_access, size_to_bind, MPOL_INTERLEAVE, &nodemask, 3, MPOL_MF_MOVE);
  printf("mbind(%p, %lu, MPOL_INTERLEAVE, all, %lu, MPOL_MF_MOVE | MPOL_MF_STRICT)\n",
          memory_to_access, size_to_bind, numa_get_mems_allowed()->size);
  r = mbind(memory_to_access, size_to_bind, MPOL_INTERLEAVE, numa_get_mems_allowed()->maskp, numa_get_mems_allowed()->size, MPOL_MF_MOVE | MPOL_MF_STRICT);
  if (r!=0) {perror("mbind");exit(1);}

  // Then, interleave the remaining pages on the local node
  memory_to_access = (uint64_t*) (((char*)memory_to_access)+size_to_bind);
  size_to_bind = memory_size - size_to_bind;
  if (size_to_bind > 0) {
    // printf("will call mbind on %p with size %ld\n", memory_to_access, size_to_bind);
    //r = mbind(memory_to_access, size_to_bind, MPOL_BIND, &nodemask, 2, MPOL_MF_MOVE);
    r = mbind(memory_to_access, size_to_bind, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT);
    if (r!=0) {perror("mbind");exit(1);}
  }
}

/**
  * Benchmark (stolen from Gureya)
  * The "O0" attribute is required to make sure that the compiler does not optimize the code
  **/
uint64_t __attribute__((optimize("O0"))) bench_seq_read(uint64_t* memory_to_access, uint64_t memory_size) {
	uint64_t fake = 0;
  #pragma omp parallel for collapse(2) shared(memory_to_access) reduction(+:fake)
	for (uint64_t i = 0; i < 30; i++) {
		for (uint64_t j = 0; j < (memory_size / sizeof(*memory_to_access)); j += 8) {
			fake += memory_to_access[j];
			fake += memory_to_access[j + 1];
			fake += memory_to_access[j + 2];
			fake += memory_to_access[j + 3];
			fake += memory_to_access[j + 4];
			fake += memory_to_access[j + 5];
			fake += memory_to_access[j + 6];
			fake += memory_to_access[j + 7];
		}
	}
	return memory_size;
}

// where the action happens
int main() {
	uint64_t *a;
  std::chrono::high_resolution_clock::time_point t1, t2;
  uint64_t duration;
  double duration_local;

	// check if NUMA is available
  if (numa_available() == -1) {
    printf("NUMA is not available here :-(\n");
    exit(1);
  }

  // number of NUMA nodes in the system
  printf("NUMA nodes: %d\n", numa_num_configured_nodes());
  // list of nodes where this process can allocate memory
  printmask("Nodes allowed", numa_get_mems_allowed());

  // print dataset size
  static const char *suff[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  int suff_i = 0;
  double array_size = N * sizeof(uint64_t);
  for (; array_size >= 1024 / sizeof(suff[0]); suff_i++, array_size /= 1024);
  printf("array size is %0.2lf%s\n", array_size, suff[suff_i]);
  printf("running benchmark using %d threads\n", omp_get_max_threads());

	// initialize: bind array to local node, initialize it and warm up
  assert(posix_memalign((void**)&a, sysconf(_SC_PAGESIZE), N * sizeof(uint64_t)) == 0);
  printf("initializing array...\n");
  change_membind(a, N*sizeof(uint64_t), 1.0);
  a[0] = 123;
  #pragma omp parallel for shared(a)
  for(uint64_t i = 1; i < N; i++) {
    a[i] = (a[i-1] * a[i-1]) % 1048576;
  }
  printf("warming up...\n");
  bench_seq_read(a, N*sizeof(uint64_t));

/*  // run benchmark with 100% of the array allocated to local node
	printf("checking local\n");
  change_membind(a, N*sizeof(uint64_t), 1.0);
	t1 = std::chrono::system_clock::now();
  bench_seq_read(a, N*sizeof(uint64_t));
  t2 = std::chrono::system_clock::now();
  duration_local = duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count();
	printf("\x1B[31mreference time: %" PRIu64 "ns\n\x1B[0m", duration);*/

  // run benchmark with different local node allocation ratios
  for (double r=0.25; r <= 1.00; r+=0.25) {
    printf("\n> Checking %.0f%% local\n", r*100);
	  change_membind(a, N*sizeof(uint64_t), r);
		t1 = std::chrono::system_clock::now();
	  bench_seq_read(a, N*sizeof(uint64_t));
		t2 = std::chrono::system_clock::now();
	  duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count();
		printf("\x1B[36mlocal_ratio=%f time: %" PRIu64 "ns (relative = %lf*local)\n\x1B[0m", r, duration, duration/duration_local);
  }

	return 0;
}
