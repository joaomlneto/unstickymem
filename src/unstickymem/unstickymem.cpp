#include <cstdio>
#include <cassert>
#include <algorithm>
#include <numeric>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <dlfcn.h>

#include <numa.h>
#include <numaif.h>

#include "unstickymem/unstickymem.h"
#include "unstickymem/PerformanceCounters.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/MemoryMap.hpp"
#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"
#include "unstickymem/Runtime.hpp"

// wait before starting
#define WAIT_START 2 // seconds
// how many times should we read the HW counters before progressing?
#define NUM_POLLS 20
// how many should we ignore
#define NUM_POLL_OUTLIERS 5
// how long should we wait between
#define POLL_SLEEP 200000 // 0.2s
int OPT_NUM_WORKERS_VALUE = 1;

namespace unstickymem {

static bool OPT_DISABLED = false;
static bool OPT_SCAN = false;
static bool OPT_FIXED_RATIO = false;
static bool OPT_NUM_WORKERS = false;
static double OPT_FIXED_RATIO_VALUE = 0.0;

static pthread_t hw_poller_thread;

static bool is_initialized = false;

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
  //get_stall_rate();
  //get_stall_rate_v1();
  get_stall_rate_v2();
  sleep(WAIT_START);

  // dump mapping information
  MemoryMap segments;
  segments.print();

  if (OPT_FIXED_RATIO) {
    while (1) {
      LINFOF("Fixed Ratio selected. Placing %lf in local node.",
             OPT_FIXED_RATIO_VALUE);
      place_all_pages(OPT_FIXED_RATIO_VALUE);
      unstickymem_log(OPT_FIXED_RATIO_VALUE);
      stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP,
                                          NUM_POLL_OUTLIERS);
      fprintf(stderr, "measured stall rate: %lf\n",
      get_average_stall_rate(NUM_POLLS, POLL_SLEEP, NUM_POLL_OUTLIERS));

      //print stall_rate to a file for debugging!
      unstickymem_log(stall_rate, OPT_FIXED_RATIO_VALUE);
      pthread_exit(0);
    }
    exit(-1);
  }

  // slowly achieve awesomeness
  for (uint64_t local_percentage = (100 / numa_num_configured_nodes() + 4) / 5 * 5;
       local_percentage <= 100; local_percentage += 5) {
    local_ratio = ((double) local_percentage) / 100;
    LINFOF("going to check a ratio of %3.1lf%%", local_ratio * 100);
    place_all_pages(segments, local_ratio);
    unstickymem_log(local_ratio);
    stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP, NUM_POLL_OUTLIERS);
    //print stall_rate to a file for debugging!
    unstickymem_log(stall_rate, local_ratio);

    LINFOF("Ratio: %1.2lf StallRate: %1.10lf (previous %1.10lf; best %1.10lf)",
           local_ratio, stall_rate, prev_stall_rate, best_stall_rate);
    /*std::string s = std::to_string(stall_rate);
    s.replace(s.find("."), std::string(".").length(), ",");
    fprintf(stderr, "%s\n", s.c_str());*/
    // compute the minimum rate
    best_stall_rate = std::min(best_stall_rate, stall_rate);
    // check if we are geting worse
    if (!OPT_SCAN && stall_rate > best_stall_rate * 1.001) {
      // just make sure that its not something transient...!
      LINFO("Hmm... Is this the best we can do?");
      if (get_average_stall_rate(NUM_POLLS * 2, POLL_SLEEP, NUM_POLL_OUTLIERS * 2)) {
        LINFO("I guess so!");
        break;
      }
    }
    prev_stall_rate = stall_rate;
  }

  LINFO("My work here is done! Enjoy the speedup");
  LINFOF("Ratio: %1.2lf", local_ratio);
  LINFOF("Stall Rate: %1.10lf", stall_rate);
  LINFOF("Best Measured Stall Rate: %1.10lf", best_stall_rate);

  if (OPT_SCAN)
    exit(0);

  return NULL;
}

void read_config(void) {
  OPT_DISABLED = std::getenv("UNSTICKYMEM_DISABLED") != nullptr;
  OPT_SCAN = std::getenv("UNSTICKYMEM_SCAN") != nullptr;
  OPT_FIXED_RATIO = std::getenv("UNSTICKYMEM_FIXED_RATIO") != nullptr;
  OPT_NUM_WORKERS = std::getenv("UNSTICKYMEM_WORKERS") != nullptr;
  if (OPT_FIXED_RATIO) {
    OPT_FIXED_RATIO_VALUE = std::stod(std::getenv("UNSTICKYMEM_FIXED_RATIO"));
  }
  if (OPT_NUM_WORKERS) {
  	OPT_NUM_WORKERS_VALUE = std::stoi(std::getenv("UNSTICKYMEM_WORKERS"));
  }
}

void print_config(void) {
  LINFOF("disabled:    %s", OPT_DISABLED ? "yes" : "no");
  LINFOF("scan mode:   %s", OPT_SCAN ? "yes" : "no");
  LINFOF("fixed ratio: %s",
         OPT_FIXED_RATIO ? std::to_string(OPT_FIXED_RATIO_VALUE).c_str() : "no");
  LINFOF("num_workers: %s",
         OPT_NUM_WORKERS ? std::to_string(OPT_NUM_WORKERS_VALUE).c_str() : "no");
}

// library initialization
__attribute__((constructor)) void libunstickymem_initialize(void) {
  LDEBUG("Initializing");

  // initialize pointers to wrapped functions
  init_real_functions();

  // parse and display the configuration
  read_config();
  print_config();

  // start the runtime
  Runtime r;

  // interleave memory by default
  LDEBUG("Setting default memory policy to interleaved");
  set_mempolicy(MPOL_INTERLEAVE,
                numa_get_mems_allowed()->maskp,
                numa_get_mems_allowed()->size);

  // spawn the dynamic placement thread
  pthread_create(&hw_poller_thread, NULL, hw_monitor_thread, NULL);

  is_initialized = true;
  LDEBUG("Initialized");
}

// library destructor
__attribute((destructor)) void libunstickymem_finalize(void) {
  //stop all the counters
  //stop_all_counters();
  LINFO("Finalized");
}

}  // namespace unstickymem

#ifdef __cplusplus
extern "C" {
#endif

// the public API and wrapped functions go here

void unstickymem_nop(void) {
  LDEBUG("unstickymem NO-OP!");
}

void *mmap(void *addr, size_t length, int prot,
           int flags, int fd, off_t offset) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return
      ((void *(*)(void*, size_t, int, int, int, off_t))dlsym(RTLD_NEXT, "mmap"))
        (addr, length, prot, flags, fd, offset);
  }

  // call original function
  void *result = WRAP(mmap)(addr, length, prot, flags, fd, offset);
  LTRACEF("mmap(%p, %zu, %d, %d, %d, %d) => %p",
          addr, length, prot, flags, fd, offset, result);
  // return the result
  return result;
}

void *sbrk(intptr_t increment) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((void *(*)(intptr_t))dlsym(RTLD_NEXT, "sbrk"))(increment);
  }
  void *result = WRAP(sbrk)(increment);
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
  long result = WRAP(mbind)(addr, len, mode, nodemask, maxnode, flags);
  LTRACEF("mbind(%p, %lu, %d, %p, %lu, %u) => %ld",
          addr, len, mode, nodemask, maxnode, flags, result);
  return result;
}

#ifdef __cplusplus
}  // extern "C"
#endif
