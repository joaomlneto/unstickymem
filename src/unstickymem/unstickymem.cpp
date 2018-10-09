#include <cstdio>
#include <cassert>
#include <algorithm>
#include <numeric>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>

#include "timingtest.h"

#include "fpthread/Logger.hpp"
#include "unstickymem/mem-stats.h"
#include "unstickymem/unstickymem.h"

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

// how many times should we read the HW counters before progressing?
#define NUM_POLLS 14
// how many should we ignore
#define NUM_POLL_OUTLIERS 2
// how long should we wait between 
#define POLL_SLEEP 200000 // 0.2s

static pthread_t hw_poller_thread;

// sample the HW counters and get the stall rate (since last measurement)
double get_stall_rate() {
  const int pmc_num = 0x00000000; // program counter monitor number
  static bool initialized = false;
  static uint64_t prev_clockcounts = 0;
  static uint64_t prev_pmcounts = 0;
  // wait a bit to get a baseline first time function is called
  if (!initialized) {
    prev_clockcounts = readtsc();
    prev_pmcounts = readpmc(pmc_num);
    usleep(POLL_SLEEP);
    initialized = true;
  }
  uint64_t clock = readtsc();
  uint64_t pmc = readpmc(pmc_num);
  double stall_rate = ((double)(pmc - prev_pmcounts)) / (clock - prev_clockcounts);
  return stall_rate;
}

// samples HW counters several times to avoid outliers
double get_average_stall_rate(size_t num_measurements,
                              useconds_t usec_between_measurements,
                              size_t num_outliers_to_filter) {
  std::vector<double> measurements(num_measurements);

  // throw away a measurement, because why not
  get_stall_rate();
  usleep(usec_between_measurements);

  // do N measurements, T usec apart
  for (size_t i=0; i < num_measurements; i++) {
    measurements[i] = get_stall_rate();
    usleep(usec_between_measurements);
  }

  // filter outliers
  std::sort(measurements.begin(), measurements.end());
  measurements.erase(measurements.end() - num_outliers_to_filter, measurements.end());
  measurements.erase(measurements.begin(), measurements.begin() + num_outliers_to_filter);

  /*for (size_t i=0; i < measurements.size(); i++)
    printf("%lf ", measurements[i]);
  printf("\n"); // */

  // return the average
  double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);
  return sum / measurements.size();
}

/**
 * Thread that monitors hardware counters in a given core
 */
void *hw_monitor_thread(void *arg) {
  double local_ratio = 1.0 / numa_num_configured_nodes(); // start with everything interleaved
  double stall_rate, prev_stall_rate;

  // run on core zero
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(0, &mask);
  DIEIF(sched_setaffinity(syscall(SYS_gettid), sizeof(mask), &mask) < 0,
        "could not set affinity for hw monitor thread");

  // lets wait a bit before starting the process
  sleep(2);

  // get baseline stall rate
  prev_stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP, NUM_POLL_OUTLIERS);
  LINFOF("RATIO = %lf STALL RATE = %lf\n", local_ratio, prev_stall_rate);

  // slowly achieve awesomeness
  while(local_ratio < 1.00) {
    LWARNF("GOING TO CHECK A LOCAL RATIO OF %lf", local_ratio);
    place_all_pages(local_ratio);
    stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP, NUM_POLL_OUTLIERS);
    LINFOF("RATIO = %lf STALL RATE = %lf", local_ratio, prev_stall_rate);
    if (stall_rate > prev_stall_rate) break;
    prev_stall_rate = stall_rate;
    local_ratio += 0.01;
  }

  LINFO("My work here is done! Enjoy the speedup");
  LINFOF("Ratio: %lf", local_ratio);
  LINFOF("Stall Rate: %lf", stall_rate);

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
