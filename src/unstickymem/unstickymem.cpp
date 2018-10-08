#include <stdio.h>
#include "unstickymem/logger.hpp"

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((constructor)) void libunstickymem_initialize(void) {
  printf("[unstickymem] initializing!\n");
}

__attribute((destructor)) void libunstickymem_finalize(void) {
  printf("[unstickymem] bye\n");
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
  printf("numa placement optimization scheduled...");
  
}

#ifdef __cplusplus
}  // extern "C"
#endif
