cmake_minimum_required(VERSION 3.0)

project(nkqc)
file(GLOB_RECURSE SOURCE nkqc/*.cpp nkqc/*.h)
message("source: " ${SOURCE})
set(CMAKE_CXX_STANDARD 14)
add_executable(nkqc ${SOURCE})
