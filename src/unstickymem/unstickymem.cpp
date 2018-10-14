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
#include "unstickymem/PerformanceCounters.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/MemoryMap.hpp"
#include "unstickymem/Logger.hpp"

// wait before starting
#define WAIT_START 2 // seconds
// how many times should we read the HW counters before progressing?
#define NUM_POLLS 20
// how many should we ignore
#define NUM_POLL_OUTLIERS 5
// how long should we wait between
#define POLL_SLEEP 200000 // 0.2s

namespace unstickymem {

static bool OPT_DISABLED = false;
static bool OPT_SCAN = false;
static bool OPT_FIXED_RATIO = false;
static double OPT_FIXED_RATIO_VALUE = 0.0;

static pthread_t hw_poller_thread;

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
/*
  if (OPT_FIXED_RATIO) {
    while(1) {
      LINFOF("Fixed Ratio selected. Placing %lf in local node.", OPT_FIXED_RATIO_VALUE);
      place_all_pages(OPT_FIXED_RATIO_VALUE);
      usleep(NUM_POLLS * POLL_SLEEP);
      pthread_exit(0);
    }
    exit(-1);
  }
*/
  // slowly achieve awesomeness
  for (uint64_t local_percentage = 100 / numa_num_configured_nodes();
       local_percentage <= 100;
       local_percentage += 5) {
    local_ratio = ((double) local_percentage) / 100;
    place_all_pages(local_ratio);
    stall_rate = get_average_stall_rate(NUM_POLLS, POLL_SLEEP, NUM_POLL_OUTLIERS);
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
      if (get_average_stall_rate(NUM_POLLS*2, POLL_SLEEP, NUM_POLL_OUTLIERS*2)) {
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

	if (OPT_SCAN) exit(0);

  return NULL;
}

void dump_info(void) {
  LINFOF("PAGE_SIZE %d", PAGE_SIZE);
  LINFOF("PAGE_MASK 0x%x", PAGE_MASK);
  LINFOF("sbrk(0): 0x%lx", sbrk(0));
  LINFOF("Program break: %p", sbrk(0));
  MemoryMap segments;
  segments.print();
}

void read_config(void) {
  OPT_DISABLED = std::getenv("UNSTICKYMEM_DISABLED") != nullptr;
  OPT_SCAN = std::getenv("UNSTICKYMEM_SCAN") != nullptr;
  OPT_FIXED_RATIO = std::getenv("UNSTICKYMEM_FIXED_RATIO") != nullptr;
  if (OPT_FIXED_RATIO) {
    OPT_FIXED_RATIO_VALUE = std::stod(std::getenv("UNSTICKYMEM_FIXED_RATIO"));
  }
}

void print_config(void) {
  LINFOF("disabled:    %s", OPT_DISABLED ? "yes" : "no");
  LINFOF("scan mode:   %s", OPT_SCAN ? "yes" : "no");
  LINFOF("fixed ratio: %s",
         OPT_FIXED_RATIO ? std::to_string(OPT_FIXED_RATIO_VALUE).c_str()
                         : "no");
}

__attribute__((constructor)) void libunstickymem_initialize(void) {
  LINFO("Initializing");
  read_config();
  print_config();
	if (OPT_DISABLED) return;

  // interleave memory by default
  LINFO("Setting default memory policy to interleaved");
  set_mempolicy(MPOL_INTERLEAVE,
                numa_get_mems_allowed()->maskp,
                numa_get_mems_allowed()->size);

  pthread_create(&hw_poller_thread, NULL, hw_monitor_thread, NULL);
  LINFO("Initialized");
}

__attribute((destructor)) void libunstickymem_finalize(void) {
  //LINFO("Finalizing");
  LINFO("Finalized");
}

}  // namespace unstickymem

#ifdef __cplusplus
extern "C" {
#endif

void unstickymem_nop(void) {
  LDEBUG("unstickymem NO-OP!");
}

#ifdef __cplusplus
}  // extern "C"
#endif
