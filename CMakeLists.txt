cmake_minimum_required(VERSION 3.15)
project(PA3)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

set(GCC_COVERAGE_COMPILE_FLAGS "-Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS} -lpthread" )

add_executable(multi-lookup
        util.c
        util.h multi-lookup.c multi-lookup.h)


target_link_libraries(multi-lookup pthread)
