#pragma once

#include "PlaybackInterfaces.h"

#include "MidiTransformer.h"

#include <fluidsynth.h>
#include <string>

class FluidSynthEventSink final : public IPlaybackEventSink
{
public:
    explicit FluidSynthEventSink(const std::string& soundFontPath,
                                 double sampleRate = 48000.0);
    ~FluidSynthEventSink() override;

    FluidSynthEventSink(const FluidSynthEventSink&) = delete;
    FluidSynthEventSink& operator=(const FluidSynthEventSink&) = delete;

    void onSecond(std::chrono::milliseconds position) override;
    void onLyric(const LyricEvent& lyric) override;
    void onMidiEvent(const RawMidiEvent& event) override;

private:
    void sendMidiBytes(const std::vector<std::uint8_t>& bytes);
    MidiTransformer midiTransformer_;

    static std::string formatTime(std::chrono::milliseconds position,
                                  bool includeMilliseconds);
    fluid_settings_t* settings_ = nullptr;
    fluid_synth_t* synth_ = nullptr;
    fluid_audio_driver_t* audioDriver_ = nullptr;
    int soundFontId_ = -1;
    double sampleRate_ = 48000.0;
};
