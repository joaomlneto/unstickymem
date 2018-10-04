#include <numaif.h>
#include <linux/version.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cinttypes>
#include <chrono>
#include <cmath>

static const uint64_t N = 2ULL << 24;

// XXX temporary workaround for bug in numactl XXX
// https://github.com/numactl/numactl/issues/38
#ifndef MPOL_LOCAL
#define MPOL_LOCAL 4
#endif

void change_membind(void *addr, unsigned long len, float local_ratio) {
  if (local_ratio < 0.0 || local_ratio > 1.0) {
    printf("duh! %f? thats not a ratio. mbind failed\n", local_ratio);
		return;
  }
  unsigned int num_numa_nodes = 4;
  float remote_ratio = 1.0 - local_ratio;
  float preferred_ratio = local_ratio - remote_ratio / (num_numa_nodes - 1);
  float interleave_ratio = 1.0 - preferred_ratio;
  unsigned long len_preferred = std::lround(len * preferred_ratio);
  unsigned long len_interleave = std::lround(len * interleave_ratio);
  printf("[RATIOS]  local = %f remote = %f\n", local_ratio, remote_ratio);
  printf("[MBIND]   preferred %f (%ld bytes) interleaved %f (%ld bytes)\n",
         preferred_ratio, len_preferred, interleave_ratio, len_interleave);
  char *interleave_addr = ((char*) addr) + len_preferred;
  char *end_preferred_addr = ((char*) addr) + len_preferred - 1;
  char *end_interleave_addr = ((char*) interleave_addr) + len_interleave - 1;
  printf("[MEMADDR] preferred [%p:%p] interleaved [%p:%p]\n", addr, end_preferred_addr, interleave_addr, end_interleave_addr);
  if (local_ratio < 1.0 / num_numa_nodes) {
    printf("hmm.. putting more per remote node than local node? thats not a good ratio! sorry! aborting!\n");
    return;
  }
  mbind(addr, len_preferred, MPOL_PREFERRED, NULL, 0, MPOL_MF_STRICT | MPOL_MF_MOVE);
	mbind(interleave_addr, len_interleave, MPOL_INTERLEAVE, NULL, 0, MPOL_MF_STRICT | MPOL_MF_MOVE);
}

uint64_t bench_seq_read(uint64_t* memory_to_access, uint64_t memory_size) {
	uint64_t fake = 0;
	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < (memory_size / sizeof(*memory_to_access)); j += 8) {
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

int main() {
	uint64_t *a = reinterpret_cast<uint64_t*>(malloc(N*sizeof(uint64_t)));
  std::chrono::high_resolution_clock::time_point t1, t2;
  uint64_t duration;
  double duration_local;

  printf("warming up...\n");
  bench_seq_read(a, N*sizeof(uint64_t));

	printf("checking local\n");
  change_membind(a, N*sizeof(uint64_t), 1.0);
	t1 = std::chrono::system_clock::now();
  bench_seq_read(a, N*sizeof(uint64_t));
  t2 = std::chrono::system_clock::now();
  duration_local = duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count();
	printf("time: %" PRIu64 "ns\n", duration);

  for (double r=1; r >= 0.24; r-=0.05) {
	  change_membind(a, N*sizeof(uint64_t), r);
		t1 = std::chrono::system_clock::now();
	  bench_seq_read(a, N*sizeof(uint64_t));
		t2 = std::chrono::system_clock::now();
	  duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count();
		printf("\x1B[36mlocal_ratio=%f time: %" PRIu64 "ns (relative = %lf*local)\n\x1B[0m", r, duration, duration/duration_local);
  }

	return 0;
}
