#define _POSIX_C_SOURCE 200809L
#define _BSD_SOURCE
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "unstickymem/mem-stats.h"
#include "fpthread/Logger.hpp"

#include <vector>
#include <string>

// XXX joaomlneto: stolen and adapted from:
// https://stackoverflow.com/a/36524010/4288486

MappedMemorySegment::MappedMemorySegment(char *line) {
  int name_start = 0, name_end = 0;
  unsigned long addr_start, addr_end;
  char perms_str[8];
  //printf("line: %s", line);
  DIEIF(sscanf(line, "%lx-%lx %7s %lx %u:%u %lu %n%*[^\n]%n",
                     &addr_start, &addr_end, perms_str, &_offset,
                     &_deviceMajor, &_deviceMinor, &_inode,
                     &name_start, &name_end) < 7,
        "FAILED TO PARSE");
  _startAddress = reinterpret_cast<void*>(addr_start);
  _endAddress = reinterpret_cast<void*>(addr_end);

  // copy permissions
  _permissions = 0U;
  if (strchr(perms_str, 'r'))
    _permissions |= 1U << 0;
  if (strchr(perms_str, 'w'))
    _permissions |= 1U << 1;
  if (strchr(perms_str, 'x'))
    _permissions |= 1U << 2;
  if (strchr(perms_str, 's'))
    _permissions |= 1U << 3;
  if (strchr(perms_str, 'p'))
    _permissions |= 1U << 4;

  // copy name
  if (name_end > name_start) {
    line[name_end] = '\0';
    _name.assign(&line[name_start]);
  }
}

size_t MappedMemorySegment::length() {
  return ((char*)_endAddress) - ((char*)_startAddress);
}

dev_t MappedMemorySegment::device() {
  return makedev(_deviceMajor, _deviceMinor);
}

bool MappedMemorySegment::isReadable() {
  return (_permissions & 1U) != 0;
}

bool MappedMemorySegment::isWriteable() {
  return (_permissions & 2U) != 0;
}

bool MappedMemorySegment::isExecutable() {
  return (_permissions & 4U) != 0;
}

bool MappedMemorySegment::isShared() {
  return (_permissions & 8U) != 0;
}

bool MappedMemorySegment::isPrivate() {
  return (_permissions && 16U) != 0;
}

void MappedMemorySegment::toString() {
  char info[1024];
  snprintf(info, sizeof(info),
           "[%p-%p] (%lu pages) name='%s' perms=%c%c%c%c",
           _startAddress, _endAddress, length() / sysconf(_SC_PAGESIZE),
           _name.c_str(),
           (isPrivate() ?    'P' : 'S'),
           (isExecutable() ? 'X' : '-'),
           (isWriteable() ?  'W' : '-'),
           (isReadable() ?   'R' : '-'));
  L->printHorizontalRule(info, (isWriteable() ? 2 : 1));
}

std::vector<MappedMemorySegment> get_memory_map() {
  std::vector<MappedMemorySegment> segments;
  char *line = NULL;
  size_t line_size = 0;

  // open maps file
  FILE *maps = fopen("/proc/self/maps", "r");
  DIEIF(maps == nullptr, "error opening maps file");

  // parse the maps file
  while (getline(&line, &line_size, maps) > 0) {
    segments.emplace_back(line);
  }

  // cleanup
  free(line);
  DIEIF(!feof(maps) || ferror(maps), "error parsing maps file");
  DIEIF(fclose(maps), "error closing maps file");
  //printf("done\n");
  /*for (auto &segment: segments) {
    segment.toString();
  }*/
  return segments;
}

