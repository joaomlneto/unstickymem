#include <cstdio>
#include <cassert>

#include <unistd.h>

#include <numa.h>
#include <numaif.h>

#include "fpthread/Logger.hpp"
#include "unstickymem/mem-stats.h"

extern "C" {
  extern int __data_start;
  extern int _end;
}

#define PAGE_ALIGN_DOWN(x) (((intptr_t) (x)) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(x) ((((intptr_t) (x)) + PAGE_MASK) & ~PAGE_MASK)
static const int PAGE_SIZE        = sysconf(_SC_PAGESIZE);
static const int PAGE_MASK        = PAGE_SIZE - 1;
static const size_t GLOBALS_START = PAGE_ALIGN_DOWN((intptr_t) &__data_start);
static const size_t GLOBALS_END   = PAGE_ALIGN_UP((intptr_t) &_end - 1);
static const size_t GLOBALS_SIZE  = GLOBALS_END - GLOBALS_START;

// XXX temporary workaround for bug in numactl XXX
// https://github.com/numactl/numactl/issues/38
#ifndef MPOL_LOCAL
#define MPOL_LOCAL 4
#endif

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((constructor)) void libunstickymem_initialize(void) {
  printf("[unstickymem] Initializing\n");
}

__attribute((destructor)) void libunstickymem_finalize(void) {
  printf("[unstickymem] Bye\n");
}

void place_pages(void *addr, unsigned long len, double r) {
  double local_ratio = r - (1 - r) / (numa_num_configured_nodes() - 1);
  double interleave_ratio = 1 - local_ratio;
  unsigned long size_to_bind = interleave_ratio * len;
  size_to_bind &= ~PAGE_MASK;
  // interleave some portion
  LTRACEF("size_to_bind=%d", size_to_bind);
  LDEBUGF("mbind(%p, %lu, MPOL_INTERLEAVE, numa_get_mems_allowed(), MPOL_MF_MOVE | MPOL_MF_STRICT)",
          addr, size_to_bind);
/*  DIEIF(mbind(addr, size_to_bind, MPOL_INTERLEAVE, numa_get_mems_allowed()->maskp,
              numa_get_mems_allowed()->size, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
        "mbind interleave failed");*/
  // check if there is something left to bind to local
  unsigned long local_len = len - size_to_bind;
  if (local_len <= 0)
    return;
  // bind the remainder to the local node
  void *local_addr = ((char*) addr) + size_to_bind;
  LDEBUGF("mbind(%p, %lu, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT)",
          local_addr, local_len);
/*  DIEIF(mbind(local_addr, local_len, MPOL_LOCAL, NULL, 0, MPOL_MF_MOVE | MPOL_MF_STRICT) != 0,
        "mbind local failed");*/
}

void dump_info(void) {
  LINFOF("PAGE_SIZE %d", PAGE_SIZE);
  LINFOF("PAGE_MASK 0x%x", PAGE_MASK);
  LINFOF("GLOBALS_START 0x%lx", GLOBALS_START);
  LINFOF("GLOBALS_END 0x%lx", GLOBALS_END);
  LINFOF("GLOBALS_SIZE 0x%lx", GLOBALS_SIZE);
  LINFOF("sbrk(0): 0x%lx", sbrk(0));
}

void print_memory_map(void) {
  LINFO("PROCESS MEMORY MAP");
  LINFOF("Global Region: %p:%p (%ld pages)", GLOBALS_START, GLOBALS_END, GLOBALS_SIZE/PAGE_SIZE);
  LINFOF("Program break: %p", sbrk(0));
	//address_range *list = mem_stats(getpid());
  get_memory_map();

  /*for (address_range *curr = list; curr != NULL; curr = curr->next)
    printf("\t%p .. %p: %s\n", curr->start, (void *)((char *)curr->start + curr->length), curr->name);
  printf("\n");
  fflush(stdout);
  free_mem_stats(list);*/


}

/**
 * Thread that monitors hardware counters in a given core
 */
void *hpc_monitor_thread(void *arg) {
  return NULL;
}

/**
 * Starts the whole automatic optimization process
 **/
void optimize_numa_placement(void) {
  LDEBUG("NUMA Placement Optimization Scheduled");
  LFATAL("fake place_pages call");
  dump_info();
  place_pages(0, PAGE_SIZE * 100, 0.5);
  print_memory_map();
}

#ifdef __cplusplus
}  // extern "C"
#endif
