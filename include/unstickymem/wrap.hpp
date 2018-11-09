/**
 * Copyright 2018 João Neto
 * Wrapper for native libc/pthread functions
 **/

#ifndef UNSTICKYMEM_WRAP_HPP_
#define UNSTICKYMEM_WRAP_HPP_

#include <sys/types.h>
#include <pthread.h>

#define WRAP(x) _real_##x

void init_real_functions();

// libc functions
extern void* (*WRAP(malloc))(size_t);
extern void* (*WRAP(mmap))(void*, size_t, int, int, int, off_t);
extern void* (*WRAP(sbrk))(intptr_t);

#endif  // FPTHREADS_INCLUDE_FPTHREAD_WRAP_HPP_
