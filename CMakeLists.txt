# https://stackoverflow.com/questions/17511496/how-to-create-a-shared-library-with-cmake

cmake_minimum_required(VERSION 3.5)
project(unstickymem VERSION 0.1.0 LANGUAGES CXX C)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_VERBOSE_MAKEFILE ON)

# require pthreads
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# get list of source files
file(GLOB_RECURSE unstickymem_src relative ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cpp" "src/*c")
add_library(unstickymem SHARED ${unstickymem_src})

set_property(TARGET unstickymem PROPERTY POSITION_INDEPENDENT_CODE on)
target_compile_options(unstickymem PRIVATE -g -Wall -pedantic -Wshadow -Wfatal-errors -Werror)

target_include_directories(unstickymem
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	PUBLIC $<INSTALL_INTERFACE:include>
	PRIVATE src)

target_link_libraries(unstickymem
	Threads::Threads
  -lnuma
)

# gureya's benchmark
add_executable(autobench test/autobench.c)
target_link_libraries(autobench Threads::Threads -lnuma)

# a hello world example to show that its linking to the shared library
add_executable(test_hello_world test/test_hello_world.c)
target_link_libraries(test_hello_world unstickymem)
add_test(test_hello_world test_hello_world)

# a dumbed down version of gureya's benchmark to run as a test
add_executable(test_joao test/test_joao.c)
target_link_libraries(test_joao unstickymem Threads::Threads -fopenmp)
add_test(test_joao test_joao)


