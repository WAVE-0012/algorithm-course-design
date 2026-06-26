cmake_minimum_required(VERSION 3.10)
project(GPU_Scheduler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

add_executable(main
    src/main.cpp
    src/ResourceManager.cpp
    src/Scheduler.cpp
)

target_include_directories(main PRIVATE src)
