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

static bool is_initialized = false;

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
  unstickymem::Runtime r;

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
