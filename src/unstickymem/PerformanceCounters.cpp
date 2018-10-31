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

#include <likwid.h>

namespace unstickymem {

//perf_event_open variables
struct perf_event_attr pe;
uint64_t count;
int fd;
static bool initiatialized = false;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags) {
	int ret;

	ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
	return ret;
}

/*
 * A function that uses the likwid library to measure the stall rates
 *
 * On AMD we use the following counters
 * EventSelect 0D1h Dispatch Stalls: The number of processor cycles where the decoder
 * is stalled for any reason (has one or more instructions ready but can't dispatch
 * them due to resource limitations in execution)
 * &
 * EventSelect 076h CPU Clocks not Halted: The number of clocks that the CPU is not in a halted state.
 *
 * On Intel we use the following counters
 * RESOURCE_STALLS: Cycles Allocation is stalled due to Resource Related reason
 * &
 * UNHALTED_CORE_CYCLES:  Count core clock cycles whenever the clock signal on the specific
 * core is running (not halted)
 *
 */
int err;
int* cpus;
int gid;

double get_stall_rate_v2() {
	int i, j;
	double result = 0.0;

	static double prev_cycles = 0;
	static double prev_stalls = 0;
	static uint64_t prev_clockcounts = 0;

	//char estr[] = "CPU_CLOCKS_UNHALTED:PMC0,DISPATCH_STALLS:PMC1"; //AMD
	char estr[] = "CPU_CLOCK_UNHALTED_THREAD_P:PMC0,RESOURCE_STALLS_ANY:PMC1"; //Intel Broadwell EP
	if (!initiatialized) {
		//perfmon_setVerbosity(3);
		//Load the topology module and print some values.
		err = topology_init();
		if (err < 0) {
			printf("Failed to initialize LIKWID's topology module\n");
			//return 1;
			exit(1);
		}
		// CpuInfo_t contains global information like name, CPU family, ...
		CpuInfo_t info = get_cpuInfo();
		// CpuTopology_t contains information about the topology of the CPUs.
		CpuTopology_t topo = get_cpuTopology();
		// Create affinity domains. Commonly only needed when reading Uncore counters
		affinity_init();

		printf("Likwid Measuremennts on a %s with %d CPUs\n", info->name,
				topo->numHWThreads);

		cpus = (int*) malloc(topo->numHWThreads * sizeof(int));
		if (!cpus)
			exit(1);		//return 1;

		for (i = 0; i < topo->numHWThreads; i++) {
			cpus[i] = topo->threadPool[i].apicId;
		}

		// Must be called before perfmon_init() but only if you want to use another
		// access mode as the pre-configured one. For direct access (0) you have to
		// be root.
		//accessClient_setaccessmode(0);

		// Initialize the perfmon module.
		err = perfmon_init(topo->numHWThreads, cpus);
		if (err < 0) {
			printf(
					"Failed to initialize LIKWID's performance monitoring module\n");
			topology_finalize();
			//return 1;
			exit(1);
		}

		// Add eventset string to the perfmon module.
		gid = perfmon_addEventSet(estr);
		if (gid < 0) {
			printf(
					"Failed to add event string %s to LIKWID's performance monitoring module\n",
					estr);
			perfmon_finalize();
			topology_finalize();
			//return 1;
			exit(1);
		}

		// Setup the eventset identified by group ID (gid).
		err = perfmon_setupCounters(gid);
		if (err < 0) {
			printf(
					"Failed to setup group %d in LIKWID's performance monitoring module\n",
					gid);
			perfmon_finalize();
			topology_finalize();
			//return 1;
			exit(1);
		}
		// Start all counters in the previously set up event set.
		err = perfmon_startCounters();
		if (err < 0) {
			printf("Failed to start counters for group %d for thread %d\n", gid,
					(-1 * err) - 1);
			perfmon_finalize();
			topology_finalize();
			exit(1);
			//return 1;
		}
		initiatialized = true;
		//printf("Setting up Likwid statistics for the first time\n");
	}

	// Stop all counters in the previously started event set before doing a read.
	err = perfmon_stopCounters();
	if (err < 0) {
		printf("Failed to stop counters for group %d for thread %d\n", gid,
				(-1 * err) - 1);
		perfmon_finalize();
		topology_finalize();
		//return 1;
		exit(1);
	}

	// Read the result of every thread/CPU for all events in estr.
	// For now just read/print for CPU 0
	char* ptr = strtok(estr, ",");
	double cycles = 0;
	double stalls = 0;
	j = 0;
	while (ptr != NULL) {
		for (i = 0; i < 1; i++) {
			result = perfmon_getResult(gid, j, i);
			if (j == 0) {
				cycles = result;
			} else {
				stalls = result;
			}
			//printf("Measurement result for event set %s at CPU %d: %f\n", ptr,
			//		cpus[i], result);
		}
		ptr = strtok(NULL, ",");
		j++;
	}

	//uint64_t clock = readtsc(); // read clock

	double stall_rate = (stalls - prev_stalls) / (cycles - prev_cycles);
	//double stall_rate = ((double) (stalls - prev_stalls))
	//		/ (clock - prev_clockcounts);
	prev_cycles = cycles;
	prev_stalls = stalls;
	//prev_clockcounts = clock;

	err = perfmon_startCounters();
	if (err < 0) {
		printf("Failed to start counters for group %d for thread %d\n", gid,
				(-1 * err) - 1);
		perfmon_finalize();
		topology_finalize();
		exit(1);
		//return 1;
	}

	return stall_rate;
}

void stop_all_counters() {
	err = perfmon_stopCounters();
	if (err < 0) {
		printf("Failed to stop counters for group %d for thread %d\n", gid,
				(-1 * err) - 1);
		perfmon_finalize();
		topology_finalize();
		//return 1;
		exit(1);
	}
	free(cpus);
	// Uninitialize the perfmon module.
	perfmon_finalize();
	affinity_finalize();
	// Uninitialize the topology module.
	topology_finalize();
	printf("All counters have been stopped\n");
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
	double stall_rate = ((double) (pmc - prev_pmcounts))
			/ (clock - prev_clockcounts);
	prev_pmcounts = pmc;
	prev_clockcounts = clock;
	return stall_rate;
}

//check performance counters and computes stalls per second since last call using perf_event_open
//EventSelect 0D1h Dispatch Stalls - 0x0004000D1
double get_stall_rate_v1() {

	static uint64_t prev_clockcounts = 0;
	static uint64_t prev_pmcounts = 0;
	//initialize and start the counters
	if (!initiatialized) {
		memset(&pe, 0, sizeof(struct perf_event_attr));
		pe.type = PERF_TYPE_RAW;
		pe.size = sizeof(struct perf_event_attr);
		pe.config = 0x0004000D1;
		pe.disabled = 1;
		pe.exclude_kernel = 1;
		pe.exclude_hv = 1;

		fd = perf_event_open(&pe, 0, 0, -1, 0);
		if (fd == -1) {
			fprintf(stderr, "Error opening leader %llx\n", pe.config);
			exit (EXIT_FAILURE);
		}

		ioctl(fd, PERF_EVENT_IOC_RESET, 0);
		ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

		initiatialized = true;
	}

	uint64_t clock = readtsc();
	ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
	read(fd, &count, sizeof(uint64_t));

	double stall_rate = (double) count;
	//double stall_rate = ((double) (count - prev_pmcounts))
	//		/ (clock - prev_clockcounts);

	prev_clockcounts = clock;
	prev_pmcounts = count;

	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

	return stall_rate;
}

// samples stall rate multiple times and filters outliers
double get_average_stall_rate(size_t num_measurements,
		useconds_t usec_between_measurements, size_t num_outliers_to_filter) {
	std::vector<double> measurements(num_measurements);

	// throw away a measurement, just because
	//get_stall_rate();
	//get_stall_rate_v1();
	get_stall_rate_v2();
	usleep(usec_between_measurements);

	// do N measurements, T usec apart
	for (size_t i = 0; i < num_measurements; i++) {
		//measurements[i] = get_stall_rate();
		//measurements[i] = get_stall_rate_v1();
		measurements[i] = get_stall_rate_v2();
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
