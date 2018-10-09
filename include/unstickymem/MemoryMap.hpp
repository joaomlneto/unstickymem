#ifndef UNSTICKYMEM_MEMORY_MAP
#define UNSTICKYMEM_MEMORY_MAP

#include "unstickymem/MemorySegment.hpp"

class MemoryMap : public std::vector<MemorySegment> {
 public:
  MemoryMap();
};

#endif  // UNSTICKYMEM_MEMORY_MAP
