#ifndef UNSTICKYMEM_H_
#define UNSTICKYMEM_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void unstickymem_nop(void);

#ifdef __cplusplus
}  // extern "C"
#endif

// the number of worker nodes
extern int OPT_NUM_WORKERS_VALUE;

#endif  // UNSTICKYMEM_H_
