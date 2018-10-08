#ifndef   MEM_STATS_H
#define   MEM_STATS_H
#include <stdlib.h>
#include <sys/types.h>
#include <string>
#include <vector>

struct MappedMemorySegment {
  void*         _startAddress;
  void*         _endAddress;
  unsigned long _offset;
  unsigned int  _deviceMajor;
  unsigned int  _deviceMinor;
  ino_t         _inode;
  unsigned char _permissions;
	std::string   _name;

  MappedMemorySegment(char *unparsed_line);
  size_t length();
  dev_t device();
  bool isReadable();
  bool isWriteable();
  bool isExecutable();
  bool isShared();
  bool isPrivate();
  void toString();
};

std::vector<MappedMemorySegment> get_memory_map();

#endif /* MEM_STATS_H */
