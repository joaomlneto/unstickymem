#include <sys/mman.h>

#include "unstickymem/MemoryMap.hpp"
#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"

namespace unstickymem {

MemoryMap& MemoryMap::getInstance(void) {
  static MemoryMap *object = nullptr;
  if (!object) {
    LDEBUG("Creating MemoryMap singleton object");
    void *buf = WRAP(mmap)(nullptr, 1<<30, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    DIEIF(buf == MAP_FAILED, "error allocating space for memory map object");
    object = new(buf) MemoryMap();
  }
  return *object;
}

MemoryMap::MemoryMap() {
  char *line = NULL;
  size_t line_size = 0;

  // open maps file
  FILE *maps = fopen("/proc/self/maps", "r");
  DIEIF(maps == nullptr, "error opening maps file");

  // parse the maps file
  while (getline(&line, &line_size, maps) > 0) {
    emplace_back(line);
  }

  // cleanup
  free(line);
  DIEIF(!feof(maps) || ferror(maps), "error parsing maps file");
  DIEIF(fclose(maps), "error closing maps file");
}

void MemoryMap::print() {
  for (auto &segment : *this) {
    segment.print();
  }
}

int MemoryMap::posix_memalign(void **memptr, size_t alignment, size_t size) {
  int result = WRAP(posix_memalign)(memptr, alignment, size);
  emplace_back("posix_memalign", *memptr, size);
  return result;
}

void *MemoryMap::malloc(size_t size) {
  void *result = WRAP(malloc)(size);
  //emplace_back("malloc", result, size);
  //LFATAL("MALLOC NOT BEING HANDLED CORRECTLY");
  return result;
}

void* MemoryMap::calloc(size_t nmemb, size_t size) {
  void *result = WRAP(calloc)(nmemb, size);
  emplace_back("calloc", result, size * nmemb);
  return result;
}

void* MemoryMap::realloc(void *ptr, size_t size) {
  void *result = WRAP(realloc)(ptr, size);
  // FIXME(joaomlneto) remove old segment located at `ptr`!!!
  emplace_back("realloc", result, size);
  return result;
}

void* MemoryMap::reallocarray(void *ptr, size_t nmemb, size_t size) {
  void *result = WRAP(reallocarray)(ptr, nmemb, size);
  // FIXME(joaomlneto) remove old segment located at `ptr`!!!
  emplace_back("reallocarray", result, size * nmemb);
  return result;
}

void MemoryMap::free(void *ptr) {
  // FIXME(joaomlneto) remove old segment located at `ptr`!!!
  return WRAP(free)(ptr);
}

void* MemoryMap::mmap(void *addr, size_t length, int prot,
                   int flags, int fd, off_t offset) {
  void *result = WRAP(mmap)(addr, length, prot, flags, fd, offset);
  emplace_back("mmap", result, length);
  return result;
}

int MemoryMap::brk(void* addr) {
  DIE("NOT IMPLEMENTED");
  // return WRAP(brk)(addr);
}

void* MemoryMap::sbrk(intptr_t increment) {
  DIE("NOT IMPLEMENTED");
  // return WRAP(sbrk)(increment);
}

long MemoryMap::mbind(void* addr, unsigned long len, int mode,
                      const unsigned long *nodemask, unsigned long maxnode,
                      unsigned flags) {
  long result = WRAP(mbind)(addr, len, mode, nodemask, maxnode, flags);
  DIE("NOT IMPLEMENTED");
  return result;
}

}  // namespace unstickymem
