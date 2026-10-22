cmake_minimum_required(VERSION 3.22)

project(Merik2 VERSION 0.7.1)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# FluidSynth: eerst via een CMake-pakket (vcpkg op Windows), anders via
# pkg-config (Homebrew op macOS, apt op Linux).
find_package(FluidSynth CONFIG QUIET)

if(TARGET FluidSynth::libfluidsynth)
    set(MERIK_FLUIDSYNTH_TARGET FluidSynth::libfluidsynth)
else()
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(
        FLUIDSYNTH
        REQUIRED
        IMPORTED_TARGET
        fluidsynth
    )

    set(MERIK_FLUIDSYNTH_TARGET PkgConfig::FLUIDSYNTH)
endif()

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

juce_add_binary_data(MerikAssets
    SOURCES
        assets/fonts/NotoSans-Regular.ttf
        assets/fonts/NotoSans-Medium.ttf
        assets/fonts/NotoSans-SemiBold.ttf
)


# ---------------------------------------------------------------------------
# MIDI library
# ---------------------------------------------------------------------------

set(
    BUILD_MIDILIBRARY_ONLY
    ON
    CACHE BOOL "Build only midifile library"
    FORCE
)

add_library(midifile
    src/midifile/src/Binasc.cpp
    src/midifile/src/MidiEvent.cpp
    src/midifile/src/MidiEventList.cpp
    src/midifile/src/MidiFile.cpp
    src/midifile/src/MidiMessage.cpp
    src/midifile/src/Options.cpp
)

target_include_directories(
    midifile
    PUBLIC
        src/midifile/include
)

# ---------------------------------------------------------------------------
# Merik GUI application
# ---------------------------------------------------------------------------

juce_add_gui_app(
    Merik
    PRODUCT_NAME "Merik"
    COMPANY_NAME "Erik Nieuwlands"
    BUNDLE_ID "nl.eriknieuwlands.merik"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Merik records the audio of an external sound module when you render a MIDI file to audio stems (Settings > Tools)."
)



target_sources(
    Merik
    PRIVATE
        src/main.cpp
        src/MidiFileReader.cpp
        src/TempoMap.cpp
        src/LyricsParser.cpp
        src/ChordParser.cpp
        src/FluidSynthEngine.cpp
        src/MerikPackage.cpp
        src/MerikBuilder.cpp
        src/RenderPlan.cpp
        src/CaptureSession.cpp
        src/RenderEngine.cpp
        src/RenderJob.cpp
        src/StemStreamer.cpp
        src/MidiTransformer.cpp
        src/MidiOutSink.cpp
        src/WebServer.cpp
        src/MdnsResponder.cpp
        src/MainComponent.cpp
        src/MainComponentAudio.cpp
        src/MainComponentPlayback.cpp
)

target_include_directories(
    Merik
    PRIVATE
        include
)

target_link_libraries(
    Merik
    PRIVATE
        MerikAssets
        midifile
        ${MERIK_FLUIDSYNTH_TARGET}

        juce::juce_audio_utils
        juce::juce_gui_extra
)

# Windows: Winsock voor de webserver; MSVC leest de bronbestanden als UTF-8.
if(WIN32)
    target_link_libraries(Merik PRIVATE ws2_32 psapi)
endif()

if(MSVC)
    target_compile_options(Merik PRIVATE /utf-8 /bigobj)
    target_compile_options(midifile PRIVATE /utf-8)
endif()

target_compile_definitions(
    Merik
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
)
