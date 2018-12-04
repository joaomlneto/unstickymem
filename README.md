# unstickymem

[![Build Status](https://travis-ci.com/joaomlneto/unstickymem.svg?token=QsVQsaqyQNrgjyTyzV4W&branch=master)](https://travis-ci.com/joaomlneto/unstickymem)
[![CodeFactor](https://www.codefactor.io/repository/github/joaomlneto/unstickymem/badge)](https://www.codefactor.io/repository/github/joaomlneto/unstickymem)

A library for dynamic page placement in NUMA nodes.

## What does it do?
The library will move pages between the NUMA nodes during your program's
execution in order to speed it up.

A simple explanation of the library is as follows:
1. **Interleave pages on all nodes** by default (optimizing for bandwidth)
2. **Analyze the program's memory mapping** via the `/proc/self/maps` file.
3. Use Hardware Performance Counters to **monitor the average resource stall
rate**
4. **Move some pages from remote (non-worker) NUMA nodes into local (worker)
NUMA nodes** -- this reduces the bandwidth but also places pages closer to where
they are requested, which may result in a performance increase if latency is the
issue when accessing memory.
5. If we see a **drop in the average resource stall rate**, we **go back to step
4**. A lower stall rate means the CPU is less time idle waiting for a resource.
We assume that the loss in memory bandwidth is compensated with a lower access
latency.

## How do I use it?

### Pre-requisites

- `cmake` -- version 3.5 or newer
- A modern C++ compiler
  - We have used `gcc` 8 during our testing
  - `gcc` from version 6 compiles the program, but binaries haven't been tested
  - `clang` from version 6 compiles the program, but binaries haven't been tested
- `libnuma-dev` -- for the `numa.h` and `libnuma.h` headers

### Compiling

1. `cmake .` to generate a Makefile
2. `make` to build the library and tests

### Using

You can opt to use the library with or without modifying your program.

#### Without modifying the program
Preload the library to run alongside your program via `LD_PRELOAD`:

```LD_PRELOAD=/path/to/libunstickymem.so ./myProgram```

#### With program modification
1. Include the library header in your program:
  - `#include <unstickymem/unstickymem.h>`
2. Call at least one function (otherwise `gcc` won't bother to actually include
it with your executable)
  - See the available functions in [`unstickymem/unstickymem.h`](https://github.com/joaomlneto/unstickymem/blob/master/include/unstickymem/unstickymem.h)
3. Compile your program

#### System installation
You can make the library generally available to any user in the system.
- `make install` installs the library and required header files in your system.

Run `make uninstall` to undo the effects of `make install`.

## Library Options
There are a few options that can change the behavior of the library.
These are specified via environment variables.

###### `UNSTICKYMEM_SCAN`
If set, it will not tune the program. Instead, it will just report the observed
stall rates while shifting memory from remote to worker nodes.

###### `UNSTICKYMEM_DISABLED`
If set, completely disables the library self-starting the tuning procedure on
startup.

###### `UNSTICKYMEM_FIXED_RATIO`
This will set a fixed ratio of pages to be placed in the worker nodes. This
disables the tuning procedure.

## A tour of the source tree
- We are using the [`CMake`](https://cmake.org) build system for this library.
- `src` contains all source files
- `include` contains all header files. Each library uses its own subfolder in
order to reduce collisions when installed in a system (following the [Google
C++ Style Guide](https://google.github.io/styleguide/cppguide.html)).
- A few example programs are included in the `test` subfolder.

### Logical Components
- The higher-level logic is found in [`unstickymem.cpp`](https://github.com/joaomlneto/unstickymem/blob/master/src/unstickymem/unstickymem.cpp).
- The logic to view/parse/modify the process memory map is in [`MemoryMap.cpp`](https://github.com/joaomlneto/unstickymem/blob/master/src/unstickymem/MemoryMap.cpp) and [`MemorySegment.cpp`](https://github.com/joaomlneto/unstickymem/blob/master/src/unstickymem/MemorySegment.cpp).
- The logic to deal with hardware performance counters is in [`PerformanceCounters.cpp`](https://github.com/joaomlneto/unstickymem/blob/master/src/unstickymem/PerformanceCounters.cpp)
- Utility functions to simplify page placement and migration are in [`PagePlacement.cpp`](https://github.com/joaomlneto/unstickymem/blob/master/src/unstickymem/PagePlacement.cpp)

## Issues / Feature Requests
If you found a bug or would like a feature added, please
[file a new Issue](https://github.com/joaomlneto/unstickymem/issues/new)!

## Contributing
Pull-requests are welcome.

Check for issues with the
[`help-wanted`](https://github.com/joaomlneto/unstickymem/issues)
tag -- these are usually ideal as first
issues or where development has been hampered.
