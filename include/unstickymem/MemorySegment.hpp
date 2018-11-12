#ifndef UNSTICKYMEM_MEMORY_SEGMENT
#define UNSTICKYMEM_MEMORY_SEGMENT

#include <stdlib.h>
#include <string>
#include <vector>

namespace unstickymem {

class MemorySegment {
  void*         _startAddress;
  void*         _endAddress;
  unsigned long _offset;
  unsigned int  _deviceMajor;
  unsigned int  _deviceMinor;
  ino_t         _inode;
  unsigned char _permissions;
  std::string   _name;

 public:
  explicit MemorySegment(char *unparsed_line);
  explicit MemorySegment(std::string name, void *start, size_t size);
  // getters
  void* startAddress();
  void* endAddress();
  std::string name();
  size_t length();
  // getters for the permissions bitmask
  bool isReadable();
  bool isWriteable();
  bool isExecutable();
  bool isShared();
  bool isPrivate();
  // other functions
  bool isBindable();
  bool isAnonymous();
  bool isHeap();
  bool isStack();
  void print();
};

}  // namespace unstickymem

#endif  // UNSTICKYMEM_MEMORY_SEGMENT
