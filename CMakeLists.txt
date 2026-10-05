cmake_minimum_required(VERSION 3.20)

project(Merik2)

set(CMAKE_CXX_STANDARD 20)

add_executable(
    merik
    src/main.cpp
    src/MidiParser.cpp
)

target_include_directories(
    merik PRIVATE
    include
)
