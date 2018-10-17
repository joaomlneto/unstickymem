#include <unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <numeric>
#include <unstickymem/PerformanceCounters.hpp>
#include <unstickymem/Logger.hpp>

namespace unstickymem {



static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

// checks performance counters and computes stalls per second since last call
double get_stall_rate2() {
  static bool initialized = false;
  static int stalls_pmc_fd = 0;
  static uint64_t prev_clockcounts = 0;

  if (!initialized) {
    // set up monitoring of CPU stalls
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.size = sizeof(struct perf_event_attr);
    pe.type = PERF_TYPE_HARDWARE;
    pe.config = PERF_COUNT_HW_STALLED_CYCLES_BACKEND;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    stalls_pmc_fd = perf_event_open(&pe, 0, 0, -1, 0);
    DIEIF(stalls_pmc_fd == -1, "error setting up hardware performance counter");
    // enable and set stalls event counter to zero
    ioctl(stalls_pmc_fd, PERF_EVENT_IOC_ENABLE, 0);
    ioctl(stalls_pmc_fd, PERF_EVENT_IOC_RESET, 0);
    // read the time stamp counter
    prev_clockcounts = readtsc();
    // mark stall rate setup as complete
    initialized = true;
  }
  // read timestamp counter and resource stalls
  uint64_t clock = readtsc();
  uint64_t pmc;
  ioctl(stalls_pmc_fd, PERF_EVENT_IOC_DISABLE, 0);
  read(stalls_pmc_fd, &pmc, sizeof(pmc));
  // compute stall rate
  double stall_rate = ((double)(pmc)) / (clock - prev_clockcounts);
  printf("%.10lf : stalls = %lu/%lu | clock = (%lu - %lu)\n", stall_rate, pmc, (clock - prev_clockcounts), clock, prev_clockcounts);
  // update previous clock value and reset stall counter
  prev_clockcounts = clock;
  ioctl(stalls_pmc_fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(stalls_pmc_fd, PERF_EVENT_IOC_ENABLE, 0);
  return stall_rate;
}

// samples stall rate multiple times and filters outliers
double get_average_stall_rate2(size_t     num_measurements,
                               useconds_t usec_between_measurements,
                               size_t     num_outliers_to_filter) {
  std::vector<double> measurements(num_measurements);

  // throw away a measurement, just because
  get_stall_rate2();
  usleep(usec_between_measurements);

  // do N measurements, T usec apart
  for (size_t i=0; i < num_measurements; i++) {
    measurements[i] = get_stall_rate2();
    usleep(usec_between_measurements);
  }

  for (auto m : measurements) {
    std::cout << m << " ";
  }
  std::cout << std::endl;

  // filter outliers
  std::sort(measurements.begin(), measurements.end());
  measurements.erase(measurements.end() - num_outliers_to_filter, measurements.end());
  measurements.erase(measurements.begin(), measurements.begin() + num_outliers_to_filter);

  // return the average
  double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);

  // TODO(joaomlneto): replace `std::accumulate` with the faster `std::reduce`
  //double sum2 = std::reduce(std::execution::par_unseq, measurements.begin(), measurements.end(), 0.0);
  //DIEIF(sum != sum2, "reduce and accumulate dont yield the same result");

  return sum / measurements.size();
}

// checks performance counters and computes stalls per second since last call
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

// samples stall rate multiple times and filters outliers
double get_average_stall_rate(size_t     num_measurements,
                              useconds_t usec_between_measurements,
                              size_t     num_outliers_to_filter) {
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
    std::cout << m << " ";
  }
  std::cout << std::endl;

  // filter outliers
  std::sort(measurements.begin(), measurements.end());
  measurements.erase(measurements.end() - num_outliers_to_filter, measurements.end());
  measurements.erase(measurements.begin(), measurements.begin() + num_outliers_to_filter);

  // return the average
  double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);
  return sum / measurements.size();
}


#if defined(__unix__) || defined(__linux__)
// System-specific definitions for Linux

// read time stamp counter
inline uint64_t readtsc(void) {
    uint32_t lo, hi;
    __asm __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi) : : );
    return lo | (uint64_t)hi << 32;
}

// read performance monitor counter
inline uint64_t readpmc(int32_t n) {
    uint32_t lo, hi;
    __asm __volatile__ ("rdpmc" : "=a"(lo), "=d"(hi) : "c"(n) : );
    return lo | (uint64_t)hi << 32;
}


#else  // not Linux

#error We only support Linux

#endif

}  // namespace unstickymem
