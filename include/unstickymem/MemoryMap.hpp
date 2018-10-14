#ifndef UNSTICKYMEM_MEMORY_MAP
#define UNSTICKYMEM_MEMORY_MAP

#include "unstickymem/MemorySegment.hpp"

namespace unstickymem {

class MemoryMap : private std::vector<MemorySegment> {
 public:
  MemoryMap();
  void print();

  //allowed methods from std::vector
  using vector::operator[];
  using vector::begin;
  using vector::end;

};

}  // namespace unstickymem

#endif  // UNSTICKYMEM_MEMORY_MAP
