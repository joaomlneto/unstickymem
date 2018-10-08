#include <numa.h>
#include <numaif.h>
#include <unistd.h>
#include <linux/version.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

static const size_t N = 1ULL << 26;

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
  printf("array size is %ldMB\n", (N * sizeof(uint64_t))/1024/1024);
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

  // run benchmark with different local node allocation ratios
  for (double r=0.25; r <= 1.00; r+=0.25) {
    printf("\n> Checking %.0f%% local\n", r*100);
	  change_membind(a, N*sizeof(uint64_t), r);
	  bench_seq_read(a, N*sizeof(uint64_t));
  }

	return 0;
}
