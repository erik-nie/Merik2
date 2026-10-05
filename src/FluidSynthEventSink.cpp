#include "FluidSynthEventSink.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstdint>
#include <stdexcept>

namespace
{
void checkPointer(const void* pointer, const char* what)
{
    if (!pointer)
        throw std::runtime_error(what);
}
}

FluidSynthEventSink::FluidSynthEventSink(const std::string& soundFontPath,
                                         double sampleRate)
    : sampleRate_(sampleRate)
{
    if (soundFontPath.empty())
        throw std::invalid_argument("FluidSynthEventSink: empty SoundFont path");

    settings_ = new_fluid_settings();
    checkPointer(settings_, "FluidSynth: new_fluid_settings() failed");

    // Merik2 currently reads MIDI at 48 kHz. Keep FluidSynth on the same rate.
    if (fluid_settings_setnum(settings_, "synth.sample-rate", sampleRate_) != FLUID_OK)
        throw std::runtime_error("FluidSynth: could not set synth.sample-rate");

#if defined(__APPLE__)
    // Use Apple's native CoreAudio driver on macOS.
    if (fluid_settings_setstr(settings_, "audio.driver", "coreaudio") != FLUID_OK)
        throw std::runtime_error("FluidSynth: could not select CoreAudio");
#endif

    // A modest buffer keeps latency reasonable while remaining robust for
    // this first real-time playback implementation.
    fluid_settings_setint(settings_, "audio.period-size", 64);
    fluid_settings_setint(settings_, "audio.periods", 8);

    synth_ = new_fluid_synth(settings_);
    checkPointer(synth_, "FluidSynth: new_fluid_synth() failed");

    soundFontId_ = fluid_synth_sfload(synth_, soundFontPath.c_str(), 1);
    if (soundFontId_ < 0)
        throw std::runtime_error("FluidSynth: could not load SoundFont: " + soundFontPath);

    // The audio driver starts the live audio output when it is created.
    audioDriver_ = new_fluid_audio_driver(settings_, synth_);
    if (!audioDriver_)
        throw std::runtime_error("FluidSynth: new_fluid_audio_driver() failed");
}


std::string FluidSynthEventSink::formatTime(std::chrono::milliseconds position,
                                         bool includeMilliseconds)
{
    const auto totalMs = position.count();
    const auto minutes = totalMs / 60000;
    const auto seconds = (totalMs / 1000) % 60;
    const auto milliseconds = totalMs % 1000;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << minutes
           << std::setw(2) << seconds;

    if (includeMilliseconds)
        output << '.' << std::setw(3) << milliseconds;

    return output.str();
}

FluidSynthEventSink::~FluidSynthEventSink()
{
    // The audio driver uses the synth, so destroy it first.
    if (audioDriver_)
        delete_fluid_audio_driver(audioDriver_);

    if (synth_)
        delete_fluid_synth(synth_);

    if (settings_)
        delete_fluid_settings(settings_);
}

void FluidSynthEventSink::onSecond(std::chrono::milliseconds)
{
    // Intentionally empty for now.
}

void FluidSynthEventSink::onLyric(const LyricEvent& lyric)
{
        const auto time = std::chrono::milliseconds {
        static_cast<std::int64_t>(lyric.seconds * 1000.0 + 0.5)
    };

    //const std::scoped_lock lock(outputMutex);
    std::cout << formatTime(time, true) << ": \"" << lyric.text << "\"\n";
}

void FluidSynthEventSink::onMidiEvent(const RawMidiEvent& event)
{
    sendMidiBytes(event.bytes);
}

void FluidSynthEventSink::sendMidiBytes(const std::vector<std::uint8_t>& bytes)
{
    if (!synth_ || bytes.empty())
        return;

    const auto status = bytes[0];

    // System Common / Real-Time messages are not currently needed by the
    // first playback implementation. MIDI channel messages are handled below.
    if (status >= 0xF8)
        return;

    if (status >= 0xF0)
        return;

    const int channel = static_cast<int>(status & 0x0F);
    const int command = status & 0xF0;

    if (bytes.size() < 2)
        return;

    const int data1 = std::clamp(static_cast<int>(bytes[1]), 0, 127);

    switch (command)
    {
    case 0x80: // Note Off
        if (bytes.size() >= 3)
            fluid_synth_noteoff(synth_, channel, data1);
        break;

    case 0x90: // Note On (velocity 0 is treated as Note Off by FluidSynth)
        if (bytes.size() >= 3)
        {
            const int velocity = std::clamp(static_cast<int>(bytes[2]), 0, 127);
            fluid_synth_noteon(synth_, channel, data1, velocity);
        }
        break;

    case 0xA0: // Polyphonic Key Pressure
        if (bytes.size() >= 3)
        {
            const int value = std::clamp(static_cast<int>(bytes[2]), 0, 127);
            fluid_synth_key_pressure(synth_, channel, data1, value);
        }
        break;

    case 0xB0: // Control Change
        if (bytes.size() >= 3)
        {
            const int value = std::clamp(static_cast<int>(bytes[2]), 0, 127);
            fluid_synth_cc(synth_, channel, data1, value);
        }
        break;

    case 0xC0: // Program Change
        fluid_synth_program_change(synth_, channel, data1);
        break;

    case 0xD0: // Channel Pressure
        fluid_synth_channel_pressure(synth_, channel, data1);
        break;

    case 0xE0: // Pitch Bend
        if (bytes.size() >= 3)
        {
            const int lsb = std::clamp(static_cast<int>(bytes[1]), 0, 127);
            const int msb = std::clamp(static_cast<int>(bytes[2]), 0, 127);
            const int bend = lsb | (msb << 7);
            fluid_synth_pitch_bend(synth_, channel, bend);
        }
        break;

    default:
        break;
    }
}
