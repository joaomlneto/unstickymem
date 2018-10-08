#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <sched.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <numa.h>
#include <numaif.h>

#define N 8388608

#define MY_CORE 0

int main() {
  uint64_t *a;

	printf("Hello world!\n");
  printf("ncpus: %d\n", get_nprocs());
  printf("nnodes: %d\n", numa_num_configured_nodes());
  printf("cores per numa node: %d\n", get_nprocs() / numa_num_configured_nodes());

  // assign myself to core 0
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(MY_CORE, &mask);
  assert(sched_setaffinity(syscall(SYS_gettid), sizeof(mask), &mask) == 0);

#ifdef MADV_HUGEPAGES
  #error "oops. please adjust code for huge pages!"
#else
  assert(posix_memalign((void**)&a, sysconf(_SC_PAGESIZE), N*sizeof(uint64_t)) == 0);
#endif

  // make sure things are as we configured them
  assert(sched_getcpu() == MY_CORE);

  // initialize the memory!
  memset(a, 0, N*sizeof(uint64_t));



  sleep(10);
	return 0;
}
