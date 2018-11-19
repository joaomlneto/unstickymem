
#include <sys/mman.h>

#include <algorithm>
#include <numeric>
#include <iostream>

// #include <boost/stacktrace.hpp>

#include "unstickymem/memory/MemoryMap.hpp"
#include "unstickymem/Logger.hpp"
#include "unstickymem/wrap.hpp"

extern void *etext;
extern void *edata;
extern void *end;

namespace unstickymem {

MemoryMap::MemoryMap() {
  char *line = NULL;
  size_t line_size = 0;

  // open maps file
  FILE *maps = fopen("/proc/self/maps", "r");
  DIEIF(maps == nullptr, "error opening maps file");

  // parse the maps file, searching for the heap start
  while (getline(&line, &line_size, maps) > 0) {
    MemorySegment s(line);
    s.print();
    if (s.name() == "[heap]") {
      // found the heap!
      _segments.emplace_back(s.startAddress(), s.endAddress(), "heap");
      _heap = &_segments.back();
    } else if (s.name() == "[stack]") {
      // found the stack!
      _segments.emplace_back(s.startAddress(), s.endAddress(), "stack");
      _stack = &_segments.back();
    } else if (s.contains(&etext - 1)) {
      // found the text segment (read-only data)
      _segments.emplace_back(s.startAddress(), s.endAddress(), "text");
      _text = &_segments.back();
    } else if (s.contains(&edata - 1)) {
      // found the data segment (global variables)
      _segments.emplace_back(s.startAddress(), s.endAddress(), "data");
      _data = &_segments.back();
    } else if (s.name() == "") {
      _segments.emplace_back(s.startAddress(), s.endAddress(), "anonymous");
    }
  }

  DIEIF(_heap == nullptr, "didnt find the heap!");
  DIEIF(_stack == nullptr, "didnt find the stack!");
  DIEIF(_text == nullptr, "didnt find the text segment!");
  DIEIF(_data == nullptr, "didnt find the data segment!");

  // cleanup
  WRAP(free)(line);
  DIEIF(!feof(maps) || ferror(maps), "error parsing maps file");
  DIEIF(fclose(maps), "error closing maps file");
}

MemoryMap& MemoryMap::getInstance(void) {
  static MemoryMap *object = nullptr;
  if (!object) {
    LDEBUG("Creating MemoryMap singleton object");
    void *buf = WRAP(mmap)(nullptr, sizeof(MemoryMap), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    DIEIF(buf == MAP_FAILED, "error allocating space for memory map object");
    object = new(buf) MemoryMap();
  }
  return *object;
}

void MemoryMap::print(void) const {
  for (auto &segment : _segments) {
    segment.print();
  }
}

void MemoryMap::updateHeap(void) {
  _heap->endAddress(WRAP(sbrk)(0));
}

// iterators
std::_List_iterator<MemorySegment> MemoryMap::begin() noexcept {
  return _segments.begin();
}

std::_List_iterator<MemorySegment> MemoryMap::end() noexcept {
  return _segments.end();
}

std::_List_const_iterator<MemorySegment> MemoryMap::cbegin() const noexcept {
  return _segments.cbegin();
}

std::_List_const_iterator<MemorySegment> MemoryMap::cend() const noexcept {
  return _segments.cend();
}

void *MemoryMap::handle_malloc(size_t size) {
  void *result = WRAP(malloc)(size);

  // compute the end address
  void *start = reinterpret_cast<void*>PAGE_ALIGN_DOWN(result);
  void *end = reinterpret_cast<void*>
    (PAGE_ALIGN_UP(reinterpret_cast<intptr_t>(result) + size) - 1);

  // update the heap
  updateHeap();

  // if it was not placed in the heap, means it is a new region!
  if (!_heap->contains(result)) {
    std::scoped_lock lock(_segments_lock);
    _segments.emplace_back(start, end, "malloc");
  }
  return result;
}

void* MemoryMap::handle_calloc(size_t nmemb, size_t size) {
  void *result = WRAP(calloc)(nmemb, size);

  // compute end address
  void *start = reinterpret_cast<void*>PAGE_ALIGN_DOWN(result);
  void *end = reinterpret_cast<void*>
    (PAGE_ALIGN_UP(reinterpret_cast<intptr_t>(result) + (nmemb * size)) - 1);

  // update the heap
  updateHeap();

  // if it was not placed in the heap, means it is a new region!
  if (!_heap->contains(result)) {
    std::scoped_lock lock(_segments_lock);
    _segments.emplace_back(start, end, "calloc");
  }
  return result;
}

void* MemoryMap::handle_realloc(void *ptr, size_t size) {
  // check if object is in heap before realloc
  bool was_in_heap = _heap->contains(ptr);

  // do the realloc
  void *result = WRAP(realloc)(ptr, size);

  // check if object is in heap after realloc
  updateHeap();
  bool is_in_heap = _heap->contains(result);

  // determine if object before was outside the heap
  if (!was_in_heap) {
    std::scoped_lock lock(_segments_lock);
    _segments.remove_if([ptr](const MemorySegment& s) {
      return s.contains(ptr);
    });
  }

  if (!is_in_heap) {
    void *start = reinterpret_cast<void*>PAGE_ALIGN_DOWN(result);
    void *end = reinterpret_cast<void*>
      (PAGE_ALIGN_UP(reinterpret_cast<intptr_t>(result) + size) - 1);
    std::scoped_lock lock(_segments_lock);
    _segments.emplace_back(start, end, "realloc");
  }
  return result;
}

void* MemoryMap::handle_reallocarray(void *ptr, size_t nmemb, size_t size) {
  void *result = WRAP(reallocarray)(ptr, nmemb, size);
  DIE("NOT IMPLEMENTED");
  return result;
}

void MemoryMap::handle_free(void *ptr) {
  bool was_in_heap = _heap->contains(ptr);
  WRAP(free)(ptr);

  // check where the segment was allocated
  if (was_in_heap) {
    updateHeap();
  } else {
    // if not in heap, remove the mapped segment
    std::scoped_lock lock(_segments_lock);
    _segments.remove_if([ptr](const MemorySegment& s) {
      return s.contains(ptr);
    });
  }
}

int MemoryMap::handle_posix_memalign(void **memptr, size_t alignment,
                                     size_t size) {
  // call the actual function
  int result = WRAP(posix_memalign)(memptr, alignment, size);

  // update the heap
  updateHeap();

  // compute region start and address
  void *start = reinterpret_cast<void*>PAGE_ALIGN_DOWN(*memptr);
  void *end = reinterpret_cast<void*>
    (PAGE_ALIGN_UP(reinterpret_cast<intptr_t>(*memptr) + size) - 1);

  // add the new region
  if (!_heap->contains(*memptr)) {
    std::scoped_lock lock(_segments_lock);
    _segments.emplace_back(start, end, "posix_memalign");
  }

  return result;
}

void* MemoryMap::handle_mmap(void *addr, size_t length, int prot,
                             int flags, int fd, off_t offset) {
  void *result = WRAP(mmap)(addr, length, prot, flags, fd, offset);

  // compute the end address
  void *start = reinterpret_cast<void*>PAGE_ALIGN_DOWN(result);
  void *end = reinterpret_cast<void*>
    (PAGE_ALIGN_UP(reinterpret_cast<intptr_t>(result) + length) - 1);

  // insert the new segment
  std::scoped_lock lock(_segments_lock);
  _segments.emplace_back(start, end, "mmap");

  // return the result
  return result;
}

int MemoryMap::handle_munmap(void *addr, size_t length) {
  int result = WRAP(munmap)(addr, length);

  // remove the mapped region
  std::scoped_lock lock(_segments_lock);
  _segments.remove_if([addr](const MemorySegment& s) {
    return s.contains(addr);
  });

  return result;
}

void* MemoryMap::handle_mremap(void *old_address, size_t old_size,
                               size_t new_size, int flags,
                               ... /* void *new_address */) {
  DIE("NOT IMPLEMENTED");
}

int MemoryMap::handle_brk(void* addr) {
  DIE("NOT IMPLEMENTED");
}

void* MemoryMap::handle_sbrk(intptr_t increment) {
  DIE("NOT IMPLEMENTED");
}

long MemoryMap::handle_mbind(void* addr, unsigned long len, int mode,
                             const unsigned long *nodemask,
                             unsigned long maxnode, unsigned flags) {
  long result = WRAP(mbind)(addr, len, mode, nodemask, maxnode, flags);
  DIE("NOT IMPLEMENTED");
  return result;
}

}  // namespace unstickymem
