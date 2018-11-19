
#include <unistd.h>
#include <dlfcn.h>
#include <stdarg.h>
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
#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"
#include "unstickymem/Runtime.hpp"
#include "unstickymem/memory/MemoryMap.hpp"
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

#ifdef __cplusplus
}  // extern "C"
#endif
