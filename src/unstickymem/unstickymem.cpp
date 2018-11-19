#include <cstdio>
#include <cassert>
#include <algorithm>
#include <numeric>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>

#include <math.h>

#include "unstickymem/unstickymem.h"
#include "unstickymem/PerformanceCounters.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/MemoryMap.hpp"
#include "unstickymem/Logger.hpp"

// wait before starting
#define WAIT_START 2 // seconds
// how many times should we read the HW counters before progressing?
#define NUM_POLLS 15
// how many should we ignore
#define NUM_POLL_OUTLIERS 5
// how long should we wait between
#define POLL_SLEEP 200000 // 0.2s
// The adaptation step
#define ADAPTATION_STEP 10 //E.g. Move 10% of shared pages to the worker nodes!
// number of workers
int OPT_NUM_WORKERS_VALUE = 1;

RECORD nodes_info[MAX_NODES]; // hold the nodes information ids and weights
double sum_ww = 0;
double sum_nww = 0;

namespace unstickymem {

static bool OPT_DISABLED = false;
static bool OPT_SCAN = false;
static bool OPT_FIXED_RATIO = false;
static bool OPT_NUM_WORKERS = false;
static double OPT_FIXED_RATIO_VALUE = 0.0;

static pthread_t hw_poller_thread;

void read_weights(char filename[]) {
	FILE * fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	const char s[2] = " ";
	char *token;

	int retcode;
	// First sort the file if not sorted
	char cmdbuf[256];
	snprintf(cmdbuf, sizeof(cmdbuf), "sort -n -o %s %s", filename, filename);
	retcode = system(cmdbuf);
	if (retcode == -1) {
		printf("Unable to sort the weights!");
		exit (EXIT_FAILURE);
	}

	int j = 0;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("Weights have not been provided!\n");
		exit (EXIT_FAILURE);
	}

	while ((read = getline(&line, &len, fp)) != -1) {
		//printf("Retrieved line of length %zu :\n", read);
		//printf("%s", line);

		//get the first token
		token = strtok(line, s);
		nodes_info[j].weight = atof(token);
		//printf(" %s\n", token);

		//get the second token
		token = strtok(NULL, s);
		nodes_info[j].id = atoi(token);
		//printf(" %s\n", token);
		j++;
	}

	/*int i;
	 for (i = 0; i < MAX_NODES; i++) {
	 printf("i: %d\tw: %.1f\tid: %d\n", i, nodes_info[i].weight,
	 nodes_info[i].id);
	 }*/

	fclose(fp);
	if (line)
		free(line);

	LINFO("weights initialized!");

	return;
}

void get_sum_nww_ww(int num_workers) {

	int i;

	if (num_workers == 1) {
		//workers: 0
		char weights[] = "config/weights_1w.txt";
		read_weights(weights);
		//printf("Worker Nodes:\t");
		LDEBUG("Worker Nodes: 0");
		for (i = 0; i < MAX_NODES; i++) {
			if (nodes_info[i].id == 0) {
				//printf("nodes_info[%d].id=%d", i, nodes_info[i].id);
				sum_ww += nodes_info[i].weight;
			} else {
				sum_nww += nodes_info[i].weight;
			}
		}
	} else if (num_workers == 2) {
		//workers: 0,1
		char weights[] = "config/weights_2w.txt";
		read_weights(weights);
		//printf("Worker Nodes:\t");
		LDEBUG("Worker Nodes: 0,1");
		for (i = 0; i < MAX_NODES; i++) {
			if (nodes_info[i].id == 0 || nodes_info[i].id == 1) {
				//printf("nodes_info[%d].id=%d\t", i, nodes_info[i].id);
				sum_ww += nodes_info[i].weight;
			} else {
				sum_nww += nodes_info[i].weight;
			}
		}
	} else if (num_workers == 3) {
		//workers: 1,2,3
		char weights[] = "config/weights_3w.txt";
		read_weights(weights);
		//printf("Worker Nodes:\t");
		LDEBUG("Worker Nodes: 1,2,3");
		for (i = 0; i < MAX_NODES; i++) {
			if (nodes_info[i].id == 1 || nodes_info[i].id == 2
					|| nodes_info[i].id == 3) {
				//printf("nodes_info[%d].id=%d\t", i, nodes_info[i].id);
				sum_ww += nodes_info[i].weight;
			} else {
				sum_nww += nodes_info[i].weight;
			}
		}
	} else if (num_workers == 4) {
		//workers: 0,1,2,3
		char weights[] = "config/weights_4w.txt";
		read_weights(weights);
		//printf("Worker Nodes:\t");
		LDEBUG("Worker Nodes: 0,1,2,3");
		for (i = 0; i < MAX_NODES; i++) {
			if (nodes_info[i].id == 0 || nodes_info[i].id == 1
					|| nodes_info[i].id == 2 || nodes_info[i].id == 3) {
				//printf("nodes_info[%d].id=%d\t", i, nodes_info[i].id);
				sum_ww += nodes_info[i].weight;
			} else {
				sum_nww += nodes_info[i].weight;
			}
		}
	} else {
		LDEBUGF("Sorry, %d workers is not supported at the moment!",
				num_workers);
		exit (EXIT_FAILURE);
	}

	printf("\n");

	if ((int) round((sum_nww + sum_ww)) != 100) {
		LDEBUGF(
				"Sum of WW and NWW must be equal to 100! WW=%.2f\tNWW=%.2f\tSUM=%.2f\n",
				sum_ww, sum_nww, sum_nww + sum_ww);
		exit(-1);
	} else {
		LDEBUGF("WW = %.2f\tNWW = %.2f\n", sum_ww, sum_nww);
	}

	return;
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
	//cpu_set_t mask;
	//CPU_ZERO(&mask);
	//CPU_SET(0, &mask);
	//DIEIF(sched_setaffinity(syscall(SYS_gettid), sizeof(mask), &mask) < 0,
	//		"could not set affinity for hw monitor thread");

	// lets wait a bit before starting the process
	//get_stall_rate();
	//get_stall_rate_v1();
	//get_stall_rate_v2();
	sleep(WAIT_START);
	//sleep(10);

	// dump mapping information
	MemoryMap segments;
	segments.print();

	//set sum_ww & sum_nww & initialize the weights!
	get_sum_nww_ww(OPT_NUM_WORKERS_VALUE);

	if (OPT_FIXED_RATIO) {
		while (1) {
			LINFOF("Fixed Ratio selected. Placing %lf in local node.",
					OPT_FIXED_RATIO_VALUE);
			place_all_pages(OPT_FIXED_RATIO_VALUE);
			unstickymem_log(OPT_FIXED_RATIO_VALUE);
			//sleep(2);
			stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP,
			NUM_POLL_OUTLIERS);
			//fprintf(stderr, "measured stall rate: %lf\n",
			//		get_average_stall_rate(NUM_POLLS, POLL_SLEEP,
			//		NUM_POLL_OUTLIERS));

			//print stall_rate to a file for debugging!
			unstickymem_log(stall_rate, OPT_FIXED_RATIO_VALUE);
			pthread_exit(0);
		}
		exit(-1);
	}

	//slowly achieve awesomeness - asymmetric weights version!
	int i;
	for (i = 0; i <= sum_nww; i += ADAPTATION_STEP) {
		LINFOF("Going to check a ratio of %d", i);
		place_all_pages(segments, i);
		sleep(1);
		/*unstickymem_log(i);
		 printf(
		 "NUM_POLLS: %d\n===========================================================\n",
		 NUM_POLLS);
		 stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP,
		 NUM_POLL_OUTLIERS);
		 //print stall_rate to a file for debugging!
		 unstickymem_log(stall_rate, i);

		 LINFOF("Ratio: %d StallRate: %1.10lf (previous %1.10lf; best %1.10lf)",
		 i, stall_rate, prev_stall_rate, best_stall_rate);
		 std::string s = std::to_string(stall_rate);
		 s.replace(s.find("."), std::string(".").length(), ",");
		 fprintf(stderr, "%s\n", s.c_str());
		 // compute the minimum rate
		 best_stall_rate = std::min(best_stall_rate, stall_rate);
		 // check if we are geting worse
		 if (!OPT_SCAN && stall_rate > best_stall_rate * 1.001) {
		 // just make sure that its not something transient...!
		 LINFO("Hmm... Is this the best we can do?");
		 if (get_average_stall_rate(NUM_POLLS * 2, POLL_SLEEP,
		 NUM_POLL_OUTLIERS * 2)) {
		 LINFO("I guess so!");
		 break;
		 }
		 }
		 prev_stall_rate = stall_rate;*/

	}

	// slowly achieve awesomeness
	/*for (uint64_t local_percentage = (100 / numa_num_configured_nodes() + 4) / 5
	 * 5; local_percentage <= 100; local_percentage += 5) {
	 local_ratio = ((double) local_percentage) / 100;
	 LINFOF("going to check a ratio of %3.1lf%%", local_ratio * 100);
	 place_all_pages(segments, local_ratio);
	 sleep(5);
	 unstickymem_log(local_ratio);
	 printf(
	 "NUM_POLLS: %d\n===========================================================\n",
	 NUM_POLLS);
	 stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP,
	 NUM_POLL_OUTLIERS);
	 //print stall_rate to a file for debugging!
	 unstickymem_log(stall_rate, local_ratio);

	 LINFOF(
	 "Ratio: %1.2lf StallRate: %1.10lf (previous %1.10lf; best %1.10lf)",
	 local_ratio, stall_rate, prev_stall_rate, best_stall_rate);
	 std::string s = std::to_string(stall_rate);
	 s.replace(s.find("."), std::string(".").length(), ",");
	 fprintf(stderr, "%s\n", s.c_str());
	 // compute the minimum rate
	 best_stall_rate = std::min(best_stall_rate, stall_rate);
	 // check if we are geting worse
	 if (!OPT_SCAN && stall_rate > best_stall_rate * 1.001) {
	 // just make sure that its not something transient...!
	 LINFO("Hmm... Is this the best we can do?");
	 if (get_average_stall_rate(NUM_POLLS * 2, POLL_SLEEP,
	 NUM_POLL_OUTLIERS * 2)) {
	 LINFO("I guess so!");
	 break;
	 }
	 }
	 prev_stall_rate = stall_rate;
	 }*/

	LINFO("My work here is done! Enjoy the speedup");
	LINFOF("Ratio: %1.2lf", local_ratio);
	LINFOF("Stall Rate: %1.10lf", stall_rate);
	LINFOF("Best Measured Stall Rate: %1.10lf", best_stall_rate);

	if (OPT_SCAN)
		exit(0);

	return NULL;
}

/*
 void dump_info(void) {
 LINFOF("PAGE_SIZE %d", PAGE_SIZE);
 LINFOF("PAGE_MASK 0x%x", PAGE_MASK);
 LINFOF("sbrk(0): 0x%lx", sbrk(0));
 LINFOF("Program break: %p", sbrk(0));
 MemoryMap segments;
 segments.print();
 }
 */

void read_config(void) {
	OPT_DISABLED = std::getenv("UNSTICKYMEM_DISABLED") != nullptr;
	OPT_SCAN = std::getenv("UNSTICKYMEM_SCAN") != nullptr;
	OPT_FIXED_RATIO = std::getenv("UNSTICKYMEM_FIXED_RATIO") != nullptr;
	OPT_NUM_WORKERS = std::getenv("UNSTICKYMEM_WORKERS") != nullptr;
	if (OPT_FIXED_RATIO) {
		OPT_FIXED_RATIO_VALUE = std::stod(
				std::getenv("UNSTICKYMEM_FIXED_RATIO"));
	}
	if (OPT_NUM_WORKERS) {
		OPT_NUM_WORKERS_VALUE = std::stoi(std::getenv("UNSTICKYMEM_WORKERS"));
	}
}

void print_config(void) {
	LINFOF("disabled:    %s", OPT_DISABLED ? "yes" : "no");
	LINFOF("scan mode:   %s", OPT_SCAN ? "yes" : "no");
	LINFOF("fixed ratio: %s",
			OPT_FIXED_RATIO ?
					std::to_string(OPT_FIXED_RATIO_VALUE).c_str() : "no");
	LINFOF("num_workers: %s",
			OPT_NUM_WORKERS ?
					std::to_string(OPT_NUM_WORKERS_VALUE).c_str() : "no");
}

// library initialization
__attribute__((constructor)) void libunstickymem_initialize(void) {
	LINFO("Initializing");

	// parse and display the configuration
	read_config();
	print_config();

	// don't do anything if disabled
	if (OPT_DISABLED)
		return;

	// interleave memory by default
	LINFO("Setting default memory policy to interleaved");
	set_mempolicy(MPOL_INTERLEAVE, numa_get_mems_allowed()->maskp,
			numa_get_mems_allowed()->size);

	// spawn the dynamic placement thread
	// pthread_create(&hw_poller_thread, NULL, hw_monitor_thread, NULL);
	// unstickymem_start();

	LINFO("Initialized");
}

// library destructor
__attribute((destructor)) void libunstickymem_finalize(void) {
	//stop all the counters
	stop_all_counters();
	//LINFO("Finalizing");
	LINFO("Finalized");
}

}  // namespace unstickymem

// the public API goes here

#ifdef __cplusplus
extern "C" {
#endif

int check_sum(RECORD n_i[MAX_NODES]) {
	double sum = 0;
	int i = 0;

	for (i = 0; i < MAX_NODES; i++) {
		sum += n_i[i].weight;
	}
	return (int) round(sum);
}

void unstickymem_nop(void) {
	LDEBUG("unstickymem NO-OP!");
}

void unstickymem_start(void) {
// spawn the dynamic placement thread
	LDEBUG("Starting the unstickymem thread!");
	pthread_create(&unstickymem::hw_poller_thread, NULL,
			unstickymem::hw_monitor_thread, NULL);
}

#ifdef __cplusplus
}  // extern "C"
#endif
