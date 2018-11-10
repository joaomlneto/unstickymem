/**
 * Copyright 2018 João Neto
 * Wrapper for native libc/pthread functions
 **/
#include <pthread.h>
#include <dlfcn.h>
#include <cassert>

#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"

#define SET_WRAPPED(x, handle) \
  do {\
    WRAP(x) = (__typeof__(WRAP(x))) dlsym(handle, #x);\
    assert(#x);\
    if (WRAP(x) == NULL) {\
      LFATALF("Could not wrap function %s", #x);\
    }\
  } while (0)

// linux
int   (*WRAP(posix_memalign))(void**, size_t, size_t);
void* (*WRAP(malloc))(size_t);
void  (*WRAP(free))  (void*);
void* (*WRAP(mmap))  (void*, size_t, int, int, int, off_t);
void* (*WRAP(sbrk))  (intptr_t);
long  (*WRAP(mbind)) (void*, unsigned long, int, const unsigned long*,
                      unsigned long, unsigned);

void init_real_functions() {

  LDEBUG("Initializing references to replaced library functions");

  // linux
  SET_WRAPPED(posix_memalign, RTLD_NEXT);
  SET_WRAPPED(malloc,         RTLD_NEXT);
  SET_WRAPPED(free,           RTLD_NEXT);
  SET_WRAPPED(mmap,           RTLD_NEXT);
  SET_WRAPPED(sbrk,           RTLD_NEXT);
  SET_WRAPPED(mbind,          RTLD_NEXT);
}
