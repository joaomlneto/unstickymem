# link fpthread library and include necessary headers in all tests
link_libraries(unstickymem)

add_executable(test_hello_world test_hello_world.c)
add_test(test_hello_world test_hello_world)


