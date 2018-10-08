#include <cstdio>
#include <cassert>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>

#include "timingtest.h"

#include "fpthread/Logger.hpp"
#include "unstickymem/mem-stats.h"

#define PAGE_ALIGN_DOWN(x) (((intptr_t) (x)) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(x) ((((intptr_t) (x)) + PAGE_MASK) & ~PAGE_MASK)
static const int PAGE_SIZE        = sysconf(_SC_PAGESIZE);
static const int PAGE_MASK        = PAGE_SIZE - 1;

// XXX temporary workaround for bug in numactl XXX
// https://github.com/numactl/numactl/issues/38
#ifndef MPOL_LOCAL
#define MPOL_LOCAL 4
#endif

#ifdef __cplusplus
extern "C" {
#endif

static pthread_t hw_poller_thread;

/**
 * Thread that monitors hardware counters in a given core
 */
void *hw_monitor_thread(void *arg) {
  const int pmc_num = 0x00000000; // program monitor counter number
  static uint64_t prev_clockcounts = 0;
  static uint64_t prev_pmcounts = 0;

  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(0, &mask);
  DIEIF(sched_setaffinity(syscall(SYS_gettid), sizeof(mask), &mask) < 0,
        "could not set affinity for hw monitor thread");

  while(1) {
    uint64_t clock = readtsc(); // read clock
    uint64_t pmc = readpmc(pmc_num); // read PMC

    printf("just measured: clock=%ld pmc=%ld\n", clock, pmc);
    if (prev_clockcounts > 0) {
      LWARNF("STALL RATE: %f\n", ((float)(pmc-prev_pmcounts))/(clock-prev_clockcounts));
    } else {
      LWARN("setting up HW perf statistics for the first time\n");
    }
    prev_clockcounts = clock;
    prev_pmcounts = pmc;
    sleep(2);
  }
  return NULL;
}

__attribute__((constructor)) void libunstickymem_initialize(void) {
  LINFO("Initializing");
  pthread_create(&hw_poller_thread, NULL, hw_monitor_thread, NULL);
}

__attribute((destructor)) void libunstickymem_finalize(void) {
  LINFO("Finalizing");
}

void place_pages(void *addr, unsigned long len, double r) {
  double local_ratio = r - (1 - r) / (numa_num_configured_nodes() - 1);
  double interleave_ratio = 1 - local_ratio;
  unsigned long size_to_bind = interleave_ratio * len;
  size_to_bind &= ~PAGE_MASK;
  DIEIF(size_to_bind < 0 || size_to_bind > len,
        "that ratio does not compute!");
  // interleave some portion
  /*LTRACEF("mbind(%p, %lu, MPOL_INTERLEAVE, numa_get_mems_allowed(), MPOL_MF_MOVE | MPOL_MF_STRICT)",
          addr, size_to_bind);*/
  DIEIF(mbind(addr, size_to_bind, MPOL_INTERLEAVE, numa_get_mems_allowed()->maskp,
              numa_get_mems_allowed()->size, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
        "mbind interleave failed");
  // check if there is something left to bind to local
  unsigned long local_len = len - size_to_bind;
  if (local_len <= 0)
    return;
  // bind the remainder to the local node
  void *local_addr = ((char*) addr) + size_to_bind;
  /*LTRACEF("mbind(%p, %lu, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT)",
          local_addr, local_len);*/
  DIEIF(mbind(local_addr, local_len, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
        "mbind local failed");
}

void place_all_pages(double r) {
  std::vector<MappedMemorySegment> segments = get_memory_map();
  for (auto &segment: segments) {
    // CAUTION: dont touch vsyscall or it'll crash!
    if (segment._name == "[vsyscall]") continue;
    place_pages(segment._startAddress, segment.length(), r);
  }
}

void dump_info(void) {
  LINFOF("PAGE_SIZE %d", PAGE_SIZE);
  LINFOF("PAGE_MASK 0x%x", PAGE_MASK);
  LINFOF("sbrk(0): 0x%lx", sbrk(0));
  LINFOF("Program break: %p", sbrk(0));
  std::vector<MappedMemorySegment> segments = get_memory_map();
  // place all on local node!
  for (auto &segment: segments) {
    segment.toString();
  }
}

#ifdef __cplusplus
}  // extern "C"
#endif
