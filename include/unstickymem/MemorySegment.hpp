#ifndef UNSTICKYMEM_MEMORY_SEGMENT
#define UNSTICKYMEM_MEMORY_SEGMENT

#include <stdlib.h>
#include <string>
#include <vector>

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
  MemorySegment(char *unparsed_line);
  // getters
  void* startAddress();
  void* endAddress();
  std::string name();
  size_t length();
  dev_t device();
  bool isReadable();
  bool isWriteable();
  bool isExecutable();
  bool isShared();
  bool isPrivate();
  // other functions
  bool isBindable();
  void toString();
};

#endif  // UNSTICKYMEM_MEMORY_SEGMENT
