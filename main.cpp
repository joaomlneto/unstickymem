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
void change_membind(void *addr, unsigned long len, float local_ratio) {
  if (local_ratio < 0.0 || local_ratio > 1.0) {
    printf("duh! %f? thats not a ratio. mbind failed\n", local_ratio);
		return;
  }
  if (local_ratio < 1.0 / numa_num_configured_nodes()) {
    printf("hmm.. putting more per remote node than local node? thats not a good ratio! sorry! aborting!\n");
    return;
  }
  // the local/remote ratios are the relative quantities that will be allocated
  // in the local/remote NUMA nodes. a 0.5 local_ratio means half the memory
  // will be in the local node and the other half will be spread out in the
  // remote NUMA nodes
  float remote_ratio = 1.0 - local_ratio;
  // the preferred/interleave ratios are the ratios that are sent to `mbind`
  // to achieve the local/remote ratios we want
  float preferred_ratio = local_ratio - remote_ratio / (numa_num_configured_nodes() - 1);
  float interleave_ratio = 1.0 - preferred_ratio;
  // here we just convert relative amounts to bytes
  unsigned long len_preferred = std::lround(len * preferred_ratio);
  unsigned long len_interleave = std::lround(len * interleave_ratio);
  // compute the start/end addresses of the local/interleaved segments
  char *interleave_addr = ((char*) addr) + len_preferred;
  char *end_preferred_addr = ((char*) addr) + len_preferred - 1;
  char *end_interleave_addr = ((char*) interleave_addr) + len_interleave - 1;
  // bitmasks for mbind
  struct bitmask *all_nodes_mask = numa_allocate_nodemask();
  copy_bitmask_to_bitmask(numa_all_nodes_ptr, all_nodes_mask);
  printmask("ALL NODES MASK", all_nodes_mask);
  /*printf("[RATIOS]  local = %f remote = %f\n", local_ratio, remote_ratio);
  printf("[MBIND]   preferred %f (%ld bytes) interleaved %f (%ld bytes)\n",
         preferred_ratio, len_preferred, interleave_ratio, len_interleave);
  printf("[MEMADDR] preferred [%p:%p] interleaved [%p:%p]\n", addr, end_preferred_addr, interleave_addr, end_interleave_addr);*/
  printf("\x1B[32mmbind(%p, %lu, MPOL_LOCAL, NULL, 0, MPOL_MF_STRICT | MPOL_MF_MOVE_ALL);\n\x1B[0m", addr, len_preferred);
  printf("\x1B[33mmbind(%p, %lu, MPOL_INTERLEAVE, NULL, 0, MPOL_MF_STRICT | MPOL_MF_MOVE_ALL);\n\x1B[0m", interleave_addr, len_interleave);
  mbind(addr, len_preferred, MPOL_LOCAL, NULL, 0, MPOL_MF_STRICT | MPOL_MF_MOVE_ALL);
	mbind(interleave_addr, len_interleave, MPOL_INTERLEAVE, all_nodes_mask->maskp, all_nodes_mask->size, MPOL_MF_STRICT | MPOL_MF_MOVE_ALL);
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
	uint64_t *a = reinterpret_cast<uint64_t*>(malloc(N*sizeof(uint64_t)));
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
  printmask("allnodes", numa_all_nodes_ptr);

  // print dataset size
  static const char *suff[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  int suff_i = 0;
  double array_size = N * sizeof(uint64_t);
  for (; array_size >= 1024 / sizeof(suff[0]); suff_i++, array_size /= 1024);
  printf("array size is %0.2lf%s\n", array_size, suff[suff_i]);
  printf("running benchmark using %d threads\n", omp_get_max_threads());


	// initialize: bind array to local node, initialize it and warm up
  printf("initializing array...\n");
  change_membind(a, N*sizeof(uint64_t), 1.0);
  a[0] = 123;
  #pragma omp parallel for shared(a)
  for(uint64_t i = 1; i < N; i++) {
    a[i] = (a[i-1] * a[i-1]) % 1048576;
  }
  printf("warming up...\n");
  bench_seq_read(a, N*sizeof(uint64_t));

  // run benchmark with 100% of the array allocated to local node
	printf("checking local\n");
  change_membind(a, N*sizeof(uint64_t), 1.0);
	t1 = std::chrono::system_clock::now();
  bench_seq_read(a, N*sizeof(uint64_t));
  t2 = std::chrono::system_clock::now();
  duration_local = duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count();
	printf("\x1B[31mreference time: %" PRIu64 "ns\n\x1B[0m", duration);

  // run benchmark with different local node allocation ratios
  for (double r=0.75; r >= 0.25; r-=0.25) {
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
