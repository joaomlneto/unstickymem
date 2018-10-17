# https://stackoverflow.com/questions/17511496/how-to-create-a-shared-library-with-cmake

cmake_minimum_required(VERSION 3.5)
project(unstickymem VERSION 0.1.0 LANGUAGES CXX C)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_VERBOSE_MAKEFILE OFF)

# require pthreads
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# get list of source files
file(GLOB_RECURSE unstickymem_src relative ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cpp" "src/*c")
add_library(unstickymem SHARED ${unstickymem_src})

set_property(TARGET unstickymem PROPERTY POSITION_INDEPENDENT_CODE on)
target_compile_options(unstickymem PRIVATE -g -Wall -pedantic -Wshadow -Wfatal-errors)

target_include_directories(unstickymem
	PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	PUBLIC $<INSTALL_INTERFACE:include>
	PRIVATE src)

target_link_libraries(unstickymem
	Threads::Threads
  -lnuma
)

# 'make install' to the correct locations (provided by GNUInstallDirs)
install(TARGETS unstickymem
	EXPORT unstickymem_config
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# This makes the project importable from the install directory
# Put config file in per-project dir (name MUST match), can also
# just go into 'cmake'
install(EXPORT unstickymem_config DESTINATION share/unstickymem/cmake)

# uninstall target
if(NOT TARGET uninstall)
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
		IMMEDIATE @ONLY)
	add_custom_target(uninstall
		COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()






# gureya's benchmark
add_executable(autobench test/autobench.c)
target_link_libraries(autobench unstickymem Threads::Threads -lnuma)

# a hello world example to show that its linking to the shared library
add_executable(test_hello_world test/test_hello_world.c)
target_link_libraries(test_hello_world unstickymem)
add_test(test_hello_world test_hello_world)


