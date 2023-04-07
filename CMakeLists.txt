cmake_minimum_required(VERSION 3.5)

PROJECT(cdn LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

FILE(GLOB_RECURSE sources src/*.cpp src/*.hpp src/*.h)

ADD_EXECUTABLE(cdn main.cpp ${sources})

INSTALL(TARGETS cdn 
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

TARGET_LINK_LIBRARIES(cdn -Wall -g -luring -lssl -lcrypto)
