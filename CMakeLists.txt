cmake_minimum_required(VERSION 3.19)
project(Flex-GPUsim)

set(CMAKE_CXX_STANDARD 11)
add_executable(Flex-GPUsim config_reader.cpp config_reader.h trace_reader.cpp trace_reader.h toplevel.cpp toplevel.h cache.cpp cache.h mem_fetch.cpp mem_fetch.h gpu.h gpu.cpp main.cpp kernel.cpp kernel.h tlb.cpp tlb.h debug.cpp debug.h crossbar.cpp crossbar.h)
target_link_libraries(Flex-GPUsim pthread)