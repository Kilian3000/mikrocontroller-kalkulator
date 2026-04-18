cmake_minimum_required(VERSION 3.16)
project(microcontroller_pc_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(microcontroller_pc_client microcontroller_pc_client.cpp)
