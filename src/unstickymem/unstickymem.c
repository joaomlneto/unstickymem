#include <stdio.h>

__attribute__((constructor)) void libunstickymem_initialize(void) {
  printf("[unstickymem] initializing!\n");
}

__attribute((destructor)) void libunstickymem_finalize(void) {
  printf("[unstickymem] bye\n");
}

void optimize_numa_placement(void) {
  printf("numa placement optimization scheduled...\n");
}
