#ifndef UNSTICKYMEM_H_
#define UNSTICKYMEM_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void optimize_numa_placement(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UNSTICKYMEM_H_