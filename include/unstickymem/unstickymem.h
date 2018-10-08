#ifndef UNSTICKYMEM_H_
#define UNSTICKYMEM_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void place_pages(void *addr, unsigned long len, double ratio);
void place_all_pages(double ratio);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UNSTICKYMEM_H_
