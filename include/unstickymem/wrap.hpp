/**
 * Copyright 2018 João Neto
 * Wrapper for native libc/pthread functions
 **/

#ifndef UNSTICKYMEM_WRAP_HPP_
#define UNSTICKYMEM_WRAP_HPP_

#include <pthread.h>

#define WRAP(x) _real_##x

void init_real_functions();

extern int   (*WRAP(posix_memalign))(void **, size_t, size_t);
extern void* (*WRAP(malloc))        (size_t);
extern void  (*WRAP(free))          (void*);
extern void* (*WRAP(mmap))          (void*, size_t, int, int, int, off_t);
extern void* (*WRAP(sbrk))          (intptr_t);
extern long  (*WRAP(mbind))         (void*, unsigned long, int,
                                     const unsigned long*, unsigned long,
                                     unsigned);


#endif  // FPTHREADS_INCLUDE_FPTHREAD_WRAP_HPP_
