#include <cstdio>
#include <cassert>
#include <algorithm>
#include <numeric>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>

#include "unstickymem/unstickymem.h"
#include "unstickymem/MemoryMap.hpp"
#include "fpthread/Logger.hpp"
#include "timingtest.h"

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

// wait before starting
#define WAIT_START 2 // seconds
// how many times should we read the HW counters before progressing?
#define NUM_POLLS 20
// how many should we ignore
#define NUM_POLL_OUTLIERS 5
// how long should we wait between
#define POLL_SLEEP 100000 // 0.1s

static pthread_t hw_poller_thread;

// sample the HW counters and get the stall rate (since last measurement)
double get_stall_rate() {
  const int pmc_num = 0x00000000; // program counter monitor number
  //static bool initialized = false;
  static uint64_t prev_clockcounts = 0;
  static uint64_t prev_pmcounts = 0;
  // wait a bit to get a baseline first time function is called
  /*if (!initialized) {
    prev_clockcounts = readtsc();
    prev_pmcounts = readpmc(pmc_num);
    usleep(POLL_SLEEP);
    initialized = true;
  }*/
  uint64_t clock = readtsc();
  uint64_t pmc = readpmc(pmc_num);
  double stall_rate = ((double)(pmc - prev_pmcounts)) / (clock - prev_clockcounts);
  prev_pmcounts = pmc;
  prev_clockcounts = clock;
  return stall_rate;
}

// samples HW counters several times to avoid outliers
double get_average_stall_rate(size_t num_measurements,
                              useconds_t usec_between_measurements,
                              size_t num_outliers_to_filter) {
  std::vector<double> measurements(num_measurements);

  // throw away a measurement, just because
  get_stall_rate();
  usleep(usec_between_measurements);

  // do N measurements, T usec apart
  for (size_t i=0; i < num_measurements; i++) {
    measurements[i] = get_stall_rate();
    usleep(usec_between_measurements);
  }

  for (auto m : measurements) {
    printf("%lf ", m);
  }
  printf("\n");

  // filter outliers
  std::sort(measurements.begin(), measurements.end());
  measurements.erase(measurements.end() - num_outliers_to_filter, measurements.end());
  measurements.erase(measurements.begin(), measurements.begin() + num_outliers_to_filter);

  // return the average
  double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);
  return sum / measurements.size();
}

/**
 * Thread that monitors hardware counters in a given core
 */
void *hw_monitor_thread(void *arg) {
  double local_ratio = 1.0 / numa_num_configured_nodes(); // start with everything interleaved
  double prev_stall_rate = std::numeric_limits<double>::infinity();
  double best_stall_rate = std::numeric_limits<double>::infinity();
  double stall_rate;

  // pin thread to core zero
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(0, &mask);
  DIEIF(sched_setaffinity(syscall(SYS_gettid), sizeof(mask), &mask) < 0,
        "could not set affinity for hw monitor thread");

  // lets wait a bit before starting the process
  get_stall_rate();
  sleep(WAIT_START);

  // dump information
  LINFOF("PAGE_SIZE %d", PAGE_SIZE);
  LINFOF("PAGE_MASK 0x%x", PAGE_MASK);
  LINFOF("sbrk(0): 0x%lx", sbrk(0));
  LINFOF("Program break: %p", sbrk(0));
  MemoryMap().print();

  // slowly achieve awesomeness
  for (uint64_t local_percentage = 100 / numa_num_configured_nodes();
       local_percentage <= 100;
       local_percentage += 5) {
    local_ratio = ((double) local_percentage) / 100;
    place_all_pages(local_ratio);
    stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP, NUM_POLL_OUTLIERS);
    LINFOF("Ratio: %1.2lf StallRate: %1.10lf (previous %1.10lf; best %1.10lf)",
           local_ratio, stall_rate, prev_stall_rate, best_stall_rate);
    // compute the minimum rate
    best_stall_rate = std::min(best_stall_rate, stall_rate);
    // check if we are geting worse
    if (stall_rate > best_stall_rate * 1.001) {
      // just make sure that its not something transient...!
      LINFO("Hmm... Is this the best we can do?");
      if (get_average_stall_rate(NUM_POLLS*2, POLL_SLEEP, NUM_POLL_OUTLIERS*2)) {
        LINFO("I guess so!");
        break;
      }
    }
    prev_stall_rate = stall_rate;
    local_ratio += 0.05;
  }


  LINFO("My work here is done! Enjoy the speedup");
  LINFOF("Ratio: %1.2lf", local_ratio);
  LINFOF("Stall Rate: %1.10lf", stall_rate);
  LINFOF("Best Measured Stall Rate: %1.10lf", best_stall_rate);
  return NULL;
}

void place_pages(void *addr, unsigned long len, double r) {
  double local_ratio = r - (1 - r) / (numa_num_configured_nodes() - 1);
  double interleave_ratio = 1 - local_ratio;
  unsigned long size_to_bind = interleave_ratio * len;
  size_to_bind &= ~PAGE_MASK;
  DIEIF(size_to_bind < 0 || size_to_bind > len,
        "that ratio does not compute!");
  // interleave some portion of the memory segment between all NUMA nodes
  LTRACEF("mbind(%p, %lu, MPOL_INTERLEAVE, numa_get_mems_allowed(), MPOL_MF_MOVE | MPOL_MF_STRICT)",
          addr, size_to_bind);
  DIEIF(mbind(addr, size_to_bind, MPOL_INTERLEAVE, numa_get_mems_allowed()->maskp,
              numa_get_mems_allowed()->size, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
        "mbind interleave failed");
  // check if there is something left to bind to local
  unsigned long local_len = len - size_to_bind;
  if (local_len <= 0)
    return;
  // bind the remainder to the local node
  void *local_addr = ((char*) addr) + size_to_bind;
  LTRACEF("mbind(%p, %lu, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT)",
          local_addr, local_len);
  DIEIF(mbind(local_addr, local_len, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
        "mbind local failed");
}

void place_all_pages(double r) {
  MemoryMap segments;
  for (auto &segment: segments) {
    if (segment.isBindable() &&        // careful with [vsyscall]
        segment.isWriteable() &&       // lets just move writeable regions
        segment.length() > 1ULL<<20) { // dont care about regions <1MB
      place_pages(segment.startAddress(), segment.length(), r);
    }
  }
}

void dump_info(void) {
  LINFOF("PAGE_SIZE %d", PAGE_SIZE);
  LINFOF("PAGE_MASK 0x%x", PAGE_MASK);
  LINFOF("sbrk(0): 0x%lx", sbrk(0));
  LINFOF("Program break: %p", sbrk(0));
  MemoryMap segments;
  segments.print();
}

__attribute__((constructor)) void libunstickymem_initialize(void) {
  LINFO("Initializing");
  pthread_create(&hw_poller_thread, NULL, hw_monitor_thread, NULL);
  LINFO("Initialized");
}

__attribute((destructor)) void libunstickymem_finalize(void) {
  LINFO("Finalizing");
  LINFO("Finalized");
}

#ifdef __cplusplus
}  // extern "C"
#endif
