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

//perf_event_open variables
struct perf_event_attr pe;
uint64_t count;
int fd;
static bool initiatialized = false;

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
	double stall_rate = ((double) (pmc - prev_pmcounts))
			/ (clock - prev_clockcounts);
	prev_pmcounts = pmc;
	prev_clockcounts = clock;
	return stall_rate;
}

//check performance counters and computes stalls per second since last call using perf_event_open
double get_stall_rate_v1() {

	//initialize and start the counters
	if (!initiatialized) {
		memset(&pe, 0, sizeof(struct perf_event_attr));
		pe.type = PERF_TYPE_HARDWARE;
		pe.size = sizeof(struct perf_event_attr);
		pe.config = PERF_COUNT_HW_STALLED_CYCLES_BACKEND;
		pe.disabled = 1;
		pe.exclude_kernel = 1;
		pe.exclude_hv = 1;

		fd = perf_event_open(&pe, 0, -1, -1, 0);
		if (fd == -1) {
			fprintf(stderr, "Error opening leader %llx\n", pe.config);
			exit (EXIT_FAILURE);
		}

		ioctl(fd, PERF_EVENT_IOC_RESET, 0);
		ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

		initiatialized = true;
	}

	uint64_t clock = readtsc();
	read(fd, &count, sizeof(uint64_t));
	double stall_rate = ((double) (count)) / (clock);
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);

	return stall_rate;
}

// samples stall rate multiple times and filters outliers
double get_average_stall_rate(size_t num_measurements,
		useconds_t usec_between_measurements, size_t num_outliers_to_filter) {
	std::vector<double> measurements(num_measurements);

	// throw away a measurement, just because
	//get_stall_rate();
	get_stall_rate_v1();
	usleep(usec_between_measurements);

	// do N measurements, T usec apart
	for (size_t i = 0; i < num_measurements; i++) {
		//measurements[i] = get_stall_rate();
		measurements[i] = get_stall_rate_v1();
		usleep(usec_between_measurements);
	}

	for (auto m : measurements) {
		std::cout << m << " ";
	}
	std::cout << std::endl;

	// filter outliers
	std::sort(measurements.begin(), measurements.end());
	measurements.erase(measurements.end() - num_outliers_to_filter,
			measurements.end());
	measurements.erase(measurements.begin(),
			measurements.begin() + num_outliers_to_filter);

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

}
	// namespace unstickymem
