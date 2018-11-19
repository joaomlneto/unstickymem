#ifndef UNSTICKYMEM_H_
#define UNSTICKYMEM_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// the number of worker nodes
extern int OPT_NUM_WORKERS_VALUE;

#define MAX_NODES 8
// A structure to hold the nodes information
typedef struct rec {
	int id;
	float weight;
	int count;
} RECORD;

extern RECORD nodes_info[MAX_NODES]; // hold the nodes information ids and weights
extern double sum_ww;
extern double sum_nww;

int check_sum(RECORD nodes_info[MAX_NODES]);

void unstickymem_start(void);
void unstickymem_nop(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UNSTICKYMEM_H_
