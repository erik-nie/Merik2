cmake_minimum_required(VERSION 3.50)

project(Merik2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

# Prevent midifile from building all example tools.
set(BUILD_MIDILIBRARY_ONLY ON CACHE BOOL "Build only midifile library" FORCE)
add_subdirectory(external/midifile)

add_executable(
    merik
    src/main.cpp
    src/MidiFileReader.cpp
    src/TempoMap.cpp
    src/ConsoleEventSink.cpp
    src/MidiPlayer.cpp
    src/FluidSynthEventSink.cpp
)

target_include_directories(
    merik PRIVATE
    include
    external/midifile/include
)

target_link_libraries(
    merik 
    PRIVATE
    midifile
    PkgConfig::FLUIDSYNTH
)