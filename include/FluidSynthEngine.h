#pragma once

#include "Song.h"

#include <fluidsynth.h>
#include "MidiTransformer.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class FluidSynthEngine final
{
public:
    FluidSynthEngine();
    ~FluidSynthEngine();

    FluidSynthEngine(const FluidSynthEngine&) = delete;
    FluidSynthEngine& operator=(const FluidSynthEngine&) = delete;

    void setSampleRate(double sampleRate);

    bool loadSoundFont(const std::string& path,
                       std::string& error);

    void loadSong(std::shared_ptr<const Song> song);

    void start();
    void stop();

    void seekSamples(std::int64_t samplePosition);

    void setFamilyVolumeFactor(int family, float factor);

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] MidiChannelState getChannelState(int channel) const;
    [[nodiscard]] int getAdjustedChannelVolume(int channel) const;
    [[nodiscard]] std::int64_t positionSamples() const;

    [[nodiscard]] std::int64_t lengthSamples() const;

    void render(float** output,
                int numChannels,
                int numSamples);

    [[nodiscard]] std::string soundFontPath() const;

private:
    void destroySynth();

    void createSynth();

    void sendMidiEvent(const RawMidiEvent& event);

    void resetSynth();

    MidiTransformer midiTransformer_;

    std::int64_t eventSamplePosition(
        const RawMidiEvent& event) const;

    fluid_settings_t* settings_ = nullptr;
    fluid_synth_t* synth_ = nullptr;

    int soundFontId_ = -1;

    double sampleRate_ = 48000.0;

    std::string soundFontPath_;

    std::shared_ptr<const Song> song_;

    std::size_t nextEvent_ = 0;

    std::int64_t currentSample_ = 0;

    bool playing_ = false;

    mutable std::mutex mutex_;
};