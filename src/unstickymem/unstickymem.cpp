
#include <unistd.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/syscall.h>

#include <numa.h>
#include <numaif.h>

#include <cstdio>
#include <cassert>
#include <algorithm>
#include <numeric>


#include "unstickymem/unstickymem.h"
#include "unstickymem/PerformanceCounters.hpp"
#include "unstickymem/PagePlacement.hpp"
#include "unstickymem/MemoryMap.hpp"
#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"
#include "unstickymem/Runtime.hpp"
#include "unstickymem/mode/Mode.hpp"

static bool OPT_NUM_WORKERS = false;
int OPT_NUM_WORKERS_VALUE = 1;

namespace unstickymem {

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

  // initialize the memory
  memory = &MemoryMap::getInstance();

  // start the runtime
  runtime = new Runtime();

  is_initialized = true;
  LDEBUG("Initialized");
}

// library destructor
__attribute((destructor)) void libunstickymem_finalize(void) {
  // stop all the counters
  // stop_all_counters();
  LINFO("Finalized");
}

}  // namespace unstickymem

#ifdef __cplusplus
extern "C" {
#endif

// Public API

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

void *mmap(void *addr, size_t length, int prot,
           int flags, int fd, off_t offset) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((void *(*)(void*, size_t, int, int, int, off_t))
      dlsym(RTLD_NEXT, "mmap"))(addr, length, prot, flags, fd, offset);
  }
  // handle the function ourselves
  //std::shared_ptr<unstickymem::Mode> mode = unstickymem::runtime->getMode();
  //void *result = mode->mmap(addr, length, prot, flags, fd, offset);
  void *result = unstickymem::memory->mmap(addr, length, prot, flags, fd, offset);
  LTRACEF("mmap(%p, %zu, %d, %d, %d, %d) => %p",
          addr, length, prot, flags, fd, offset, result);
  // return the result
  return result;
}

int brk(void *addr) {
  // dont do anything fancy if library is not initialized
  if (!unstickymem::is_initialized) {
    return ((int (*)(void*))
      dlsym(RTLD_NEXT, "brk"))(addr);
  }
  // handle the function ourselves
  //std::shared_ptr<unstickymem::Mode> mode = unstickymem::runtime->getMode();
  //int result = mode->brk(addr);
  int result = unstickymem::memory->brk(addr);
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
  //std::shared_ptr<unstickymem::Mode> mode = unstickymem::runtime->getMode();
  //void *result = mode->sbrk(increment);
  void *result = unstickymem::memory->sbrk(increment);
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
  //std::shared_ptr<unstickymem::Mode> m = unstickymem::runtime->getMode();
  //long result = m->mbind(addr, len, mode, nodemask, maxnode, flags);
  long result = unstickymem::memory->mbind(addr, len, mode, nodemask, maxnode, flags);
  LTRACEF("mbind(%p, %lu, %d, %p, %lu, %u) => %ld",
          addr, len, mode, nodemask, maxnode, flags, result);
  return result;
}

void *malloc(size_t size) {
    // dont do anything fancy if library is not initialized
    if (!unstickymem::is_initialized) {
        return ((void* (*)(size_t))
          dlsym(RTLD_NEXT, "malloc"))(size);
    }
    // handle the function ourselves
    //std::shared_ptr<unstickymem::Mode> mode = unstickymem::runtime->getMode();
    //void *result = mode->malloc(size);
    void *result = unstickymem::memory->malloc(size);
    LTRACEF("malloc(%zu) => %p", size, result);
    return result;
}

void free(void *ptr) {
    // dont do anything fancy if library is not initialized
    if (!unstickymem::is_initialized) {
        return ((void (*)(void*))
          dlsym(RTLD_NEXT, "free"))(ptr);
    }
    // handle the function ourselves
    //std::shared_ptr<unstickymem::Mode> mode = unstickymem::runtime->getMode();
    //mode->free(ptr);
    unstickymem::memory->free(ptr);
    LTRACEF("free(%p)", ptr);
    return;
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    // dont do anything fancy if library is not initialized
    if (!unstickymem::is_initialized) {
        return ((int (*)(void**, size_t, size_t))
          dlsym(RTLD_NEXT, "posix_memalign"))(memptr, alignment, size);
    }
    // handle the function ourselves
    //std::shared_ptr<unstickymem::Mode> mode = unstickymem::runtime->getMode();
    //int result = mode->posix_memalign(memptr, alignment, size);
    int result = unstickymem::memory->posix_memalign(memptr, alignment, size);
    LTRACEF("posix_memalign(%p, %zu, %zu) => %d",
            memptr, alignment, size, result);
    return result;
}

#ifdef __cplusplus
}  // extern "C"
#endif
