#ifndef UNSTICKYMEM_HARDWARE_EVENTS
#define UNSTICKYMEM_HARDWARE_EVENTS

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__unix__) || defined(__linux__)
// System-specific definitions for Linux

#include <cpuid.h>

static inline void cpuid_ (int32_t output[4], int32_t functionnumber) {	
    __get_cpuid(functionnumber, (uint32_t*)output, (uint32_t*)(output+1), (uint32_t*)(output+2), (uint32_t*)(output+3));
}

static inline void serialize () {
    __asm __volatile__ ("cpuid" : : "a"(0) : "ebx", "ecx", "edx" );  // serialize
}

// read time stamp counter
static inline uint64_t readtsc() {
    uint32_t lo, hi;
    __asm __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi) : : );
    return lo | (uint64_t)hi << 32;
}

// read performance monitor counter
static inline uint64_t readpmc(int32_t n) {
    uint32_t lo, hi;
    __asm __volatile__ ("rdpmc" : "=a"(lo), "=d"(hi) : "c"(n) : );
    return lo | (uint64_t)hi << 32;
}


#else  // not Linux

#error We only support Linux

#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UNSTICKYMEM_HARDWARE_EVENTS
