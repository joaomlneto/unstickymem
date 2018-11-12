#ifndef UNSTICKYMEM_MEMORY_MAP
#define UNSTICKYMEM_MEMORY_MAP

#include <vector>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include "unstickymem/MemorySegment.hpp"

namespace unstickymem {

// namespace ipc = boost::interprocess;
// using Segment = ipc::managed_shared_memory;

class MemoryMap : private std::vector<MemorySegment> {
 private:
  MemoryMap();
  // Segment _segment{ipc::create_only, "unstickymem-memorymap", 1ul<<30};
 public:
  // get the global-unique MemoryMap object
  static MemoryMap& getInstance(void);
  MemoryMap(MemoryMap const&) = delete;
  void operator=(MemoryMap const&) = delete;
  void print();

  // handle allocations/deallocations
  int posix_memalign(void **memptr, size_t alignment, size_t size);

  void *malloc(size_t size);

  void* calloc(size_t nmemb, size_t size);

  void* realloc(void *ptr, size_t size);

  void* reallocarray(void *ptr, size_t nmemb, size_t size);

  void free(void *ptr);

  void* mmap(void *addr, size_t length, int prot,
             int flags, int fd, off_t offset);

  int brk(void* addr);

  void* sbrk(intptr_t increment);

  long mbind(void* addr, unsigned long len, int mode,
             const unsigned long *nodemask, unsigned long maxnode,
             unsigned flags);

  // allowed methods from std::vector
  using vector::operator[];
  using vector::begin;
  using vector::end;
};

}  // namespace unstickymem

#endif  // UNSTICKYMEM_MEMORY_MAP
