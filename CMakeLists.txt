cmake_minimum_required(VERSION 3.5)

project(cdn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(GCC_COVERAGE_COMPILE_FLAGS "-Wall -g -luring -lssl -lcrypto")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")

file(GLOB_RECURSE sources src/*.cpp src/*.hpp src/*.h)

add_executable(cdn main.cpp ${sources})

install(TARGETS cdn 
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

