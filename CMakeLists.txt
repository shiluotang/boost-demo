cmake_minimum_required(VERSION 3.6)
project(boost-demo VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(GTest)
find_package(Boost 1.53 REQUIRED COMPONENTS system thread chrono)
enable_testing()
add_subdirectory(tests)
