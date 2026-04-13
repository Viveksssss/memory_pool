cmake_minimum_required(VERSION 3.10.0)
project(memory_pool VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED on)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
add_compile_options(-O2)

file(GLOB_RECURSE SRC_FILES
    src/*.cpp
    src/*.hpp
)

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})

add_executable(memory_pool main.cpp ${SRC_FILES})

