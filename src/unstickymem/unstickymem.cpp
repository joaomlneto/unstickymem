
#include <unistd.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <math.h>

#include <numa.h>
#include <numaif.h>

#include <cstdio>
#include <cassert>
#include <algorithm>
#include <numeric>

#include <boost/interprocess/shared_memory_object.hpp>

#include "unstickymem/unstickymem.h"
#include "unstickymem/PerformanceCounters.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"
#include "unstickymem/Runtime.hpp"
#include "unstickymem/memory/MemoryMap.hpp"
#include "unstickymem/mode/Mode.hpp"

// The adaptation step
#define ADAPTATION_STEP 10 //E.g. Move 10% of shared pages to the worker nodes!
// number of workers
static bool OPT_NUM_WORKERS = false;
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


static bool is_initialized = false;
Runtime *runtime;
MemoryMap *memory;

void read_config(void) {
  OPT_NUM_WORKERS = std::getenv("UNSTICKYMEM_WORKERS") != nullptr;
  if (OPT_NUM_WORKERS) {
    OPT_NUM_WORKERS_VALUE = std::stoi(std::getenv("UNSTICKYMEM_WORKERS"));
  }
}

void print_config(void) {
  LINFOF("num_workers: %s",
         OPT_NUM_WORKERS ? std::to_string(OPT_NUM_WORKERS_VALUE).c_str()
                         : "no");
}

// library initialization
__attribute__((constructor)) void libunstickymem_initialize(void) {
  LDEBUG("Initializing");

  // initialize pointers to wrapped functions
  unstickymem::init_real_functions();

  // parse and display the configuration
  read_config();
  print_config();

  // set default memory policy to interleaved
  LDEBUG("Setting default memory policy to interleaved");
  set_mempolicy(MPOL_INTERLEAVE,
                numa_get_mems_allowed()->maskp,
                numa_get_mems_allowed()->size);

  // remove the previous unstickymem library segment (if exists)
  boost::interprocess::shared_memory_object::remove("unstickymem");

  // initialize the memory
  memory = &MemoryMap::getInstance();

  // start the runtime
  runtime = &Runtime::getInstance();

  is_initialized = true;
  LDEBUG("Initialized");
}

// library destructor
__attribute((destructor)) void libunstickymem_finalize(void) {
  // stop all the counters
  stop_all_counters();
  LINFO("Finalized");
}

}  // namespace unstickymem

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
  unstickymem::runtime->startSelectedMode();
}

void unstickymem_print_memory(void) {
  unstickymem::memory->print();
}

// Wrapped functions

void *malloc(size_t size) {
  static bool inside_unstickymem = false;

  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized || inside_unstickymem) {
    return ((void* (*)(size_t))dlsym(RTLD_NEXT, "malloc"))(size);
  }

  // handle the function ourselves
  inside_unstickymem = true;
  void *result = unstickymem::memory->handle_malloc(size);
  inside_unstickymem = false;
  LTRACEF("malloc(%zu) => %p", size, result);
  return result;
}

// XXX this is a hack XXX
// this is to solve the recursion in calloc -> dlsym -> calloc -> ...
#define DLSYM_CALLOC_BUFFER_LENGTH 1024*1024
static unsigned char calloc_buffer[DLSYM_CALLOC_BUFFER_LENGTH];
static bool calloc_buffer_in_use = false;

void *calloc(size_t nmemb, size_t size) {
  static bool inside_unstickymem = false;
  static bool inside_dlsym = false;

  // XXX beware: ugly hack! XXX
  // check if we are inside dlsym -- return a temporary buffer for it!
  if (inside_dlsym) {
    DIEIF(calloc_buffer_in_use, "calling dlsym requires more buffers");
    calloc_buffer_in_use = true;
    memset(calloc_buffer, 0, DLSYM_CALLOC_BUFFER_LENGTH);
    return calloc_buffer;
  }

  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized || inside_unstickymem) {
    inside_dlsym = true;
    void *result = ((void* (*)(size_t, size_t))
      dlsym(RTLD_NEXT, "calloc"))(nmemb, size);
    inside_dlsym = false;
    return result;
  }

  // handle the function ourselves
  inside_unstickymem = true;
  void *result = unstickymem::memory->handle_calloc(nmemb, size);
  inside_unstickymem = false;
  LTRACEF("calloc(%zu, %zu) => %p", nmemb, size, result);
  return result;
}

void *realloc(void *ptr, size_t size) {
  static bool inside_unstickymem = false;

  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized || inside_unstickymem) {
    return ((void *(*)(void*, size_t)) dlsym(RTLD_NEXT, "realloc"))(ptr, size);
  }

  // handle the function ourselves
  inside_unstickymem = true;
  void *result = unstickymem::memory->handle_realloc(ptr, size);
  LTRACEF("realloc(%p, %zu) => %p", ptr, size, result);
  inside_unstickymem = false;
  return result;
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
  static bool inside_unstickymem = false;
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized || inside_unstickymem) {
    return ((void *(*)(void*, size_t, size_t))
      dlsym(RTLD_NEXT, "reallocarray"))(ptr, nmemb, size);
  }
  // handle the function ourselves
  inside_unstickymem = true;
  void *result = unstickymem::memory->handle_reallocarray(ptr, nmemb, size);
  LTRACEF("reallocarray(%p, %zu, %zu) => %p", ptr, nmemb, size, result);
  inside_unstickymem = false;
  return result;
}

void free(void *ptr) {
  static bool inside_unstickymem = false;
  // check if this is the temporary buffer passed to dlsym (see calloc)
  if (ptr == calloc_buffer) {
    calloc_buffer_in_use = false;
    return;
  }
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized || inside_unstickymem) {
    return ((void (*)(void*)) dlsym(RTLD_NEXT, "free"))(ptr);
  }
  // handle the function ourselves
  inside_unstickymem = true;
  unstickymem::memory->handle_free(ptr);
  LTRACEF("free(%p)", ptr);
  inside_unstickymem = false;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((int (*)(void**, size_t, size_t))
      dlsym(RTLD_NEXT, "posix_memalign"))(memptr, alignment, size);
  }
  // handle the function ourselves
  int result = unstickymem::memory->handle_posix_memalign(memptr, alignment,
                                                          size);
  LTRACEF("posix_memalign(%p, %zu, %zu) => %d",
          memptr, alignment, size, result);
  return result;
}


void *mmap(void *addr, size_t length, int prot,
           int flags, int fd, off_t offset) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((void *(*)(void*, size_t, int, int, int, off_t))
      dlsym(RTLD_NEXT, "mmap"))(addr, length, prot, flags, fd, offset);
  }
  // handle the function ourselves
  void *result = unstickymem::memory->handle_mmap(addr, length, prot, flags,
                                                  fd, offset);
  LTRACEF("mmap(%p, %zu, %d, %d, %d, %d) => %p",
          addr, length, prot, flags, fd, offset, result);
  // return the result
  return result;
}

int munmap(void *addr, size_t length) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((int (*)(void*, size_t))dlsym(RTLD_NEXT, "munmap"))(addr, length);
  }
  // handle the function ourselves
  int result = unstickymem::memory->handle_munmap(addr, length);
  LTRACEF("munmap(%p, %zu) => %d", addr, length, result);
  // return the result
  return result;
}

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags,
             ... /* void *new_address */) {
  DIE("TODO");
}

int brk(void *addr) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((int (*)(void*))
      dlsym(RTLD_NEXT, "brk"))(addr);
  }
  // handle the function ourselves
  int result = unstickymem::memory->handle_brk(addr);
  LTRACEF("brk(%p) => %d", addr, result);
  return result;
}

void *sbrk(intptr_t increment) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((void *(*)(intptr_t))
      dlsym(RTLD_NEXT, "sbrk"))(increment);
  }
  // handle the function ourselves
  void *result = unstickymem::memory->handle_sbrk(increment);
  LTRACEF("sbrk(%zu) => %p", increment, result);
  return result;
}

long mbind(void *addr, unsigned long len, int mode,
           const unsigned long *nodemask, unsigned long maxnode,
           unsigned flags) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((long (*)(void*, unsigned long, int, const unsigned long*,
                      unsigned long, unsigned)) dlsym(RTLD_NEXT, "mbind"))
        (addr, len, mode, nodemask, maxnode, flags);
  }
  // handle the function ourselves
  long result = unstickymem::memory->handle_mbind(addr, len, mode, nodemask,
                                                  maxnode, flags);
  LTRACEF("mbind(%p, %lu, %d, %p, %lu, %u) => %ld",
          addr, len, mode, nodemask, maxnode, flags, result);
  return result;
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
