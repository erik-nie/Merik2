cmake_minimum_required(VERSION 3.5)

project(Merik2 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)

pkg_check_modules(
    FLUIDSYNTH
    REQUIRED
    IMPORTED_TARGET
    fluidsynth
)

# JUCE
include(FetchContent)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# MIDI library
# ---------------------------------------------------------------------------

set(
    BUILD_MIDILIBRARY_ONLY
    ON
    CACHE BOOL "Build only midifile library"
    FORCE
)

add_subdirectory(external/midifile)

# ---------------------------------------------------------------------------
# Merik GUI application
# ---------------------------------------------------------------------------

juce_add_gui_app(
    Merik
    PRODUCT_NAME "Merik"
    COMPANY_NAME "Erik Nieuwlands"
    BUNDLE_ID "nl.eriknieuwlands.merik"
)

target_sources(
    Merik
    PRIVATE
        src/main.cpp
        src/MidiFileReader.cpp
        src/TempoMap.cpp
        src/FluidSynthEngine.cpp
        src/MainComponent.cpp
)

target_include_directories(
    Merik
    PRIVATE
        include
        external/midifile/include
)

target_link_libraries(
    Merik
    PRIVATE
        midifile
        PkgConfig::FLUIDSYNTH

        juce::juce_audio_utils
        juce::juce_gui_extra
)

target_compile_definitions(
    Merik
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# ---------------------------------------------------------------------------
# Keep the old console player available for testing.
# ---------------------------------------------------------------------------

add_executable(
    merik-cli
    src/cli_main.cpp
    src/MidiFileReader.cpp
    src/TempoMap.cpp
    src/ConsoleEventSink.cpp
    src/MidiPlayer.cpp
    src/FluidSynthEventSink.cpp
)

target_include_directories(
    merik-cli
    PRIVATE
        include
        external/midifile/include
)

target_link_libraries(
    merik-cli
    PRIVATE
        midifile
        PkgConfig::FLUIDSYNTH
)