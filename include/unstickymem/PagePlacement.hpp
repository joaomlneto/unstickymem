#ifndef UNSTICKYMEM_PAGE_PLACEMENT_HPP_
#define UNSTICKYMEM_PAGE_PLACEMENT_HPP_

#include <numaif.h>
#include <numa.h>

#include "unstickymem/MemorySegment.hpp"

#define PAGE_ALIGN_DOWN(x) (((intptr_t) (x)) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(x) ((((intptr_t) (x)) + PAGE_MASK) & ~PAGE_MASK)

namespace unstickymem {

static const int PAGE_SIZE = sysconf(_SC_PAGESIZE);
static const int PAGE_MASK = PAGE_SIZE - 1;

// XXX temporary workaround for bug in numactl XXX
// https://github.com/numactl/numactl/issues/38
#ifndef MPOL_LOCAL
#define MPOL_LOCAL 4
#endif

void force_uniform_interleave(char *addr, unsigned long len);
void force_uniform_interleave(MemorySegment &segment);
void place_pages(void *addr, unsigned long len, double r);
void place_all_pages(double r);

}  // namespace unstickymem

#endif  // UNSTICKYMEM_PAGE_PLACEMENT_HPP_
