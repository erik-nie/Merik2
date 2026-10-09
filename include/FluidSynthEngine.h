#pragma once

#include "Song.h"
#include "MidiTransformer.h"

#include <fluidsynth.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

struct FamilySettings
{
    int volume = 127;
    bool enabled = true;
};

class FluidSynthEngine final
{
public:
    static constexpr int familyCount = MidiTransformer::familyCount;

    FluidSynthEngine();
    ~FluidSynthEngine();

    FluidSynthEngine(const FluidSynthEngine&) = delete;
    FluidSynthEngine& operator=(const FluidSynthEngine&) = delete;

    void setSampleRate(double sampleRate);

    bool loadSoundFont(
        const std::string& path,
        std::string& error);

    void loadSong(std::shared_ptr<const Song> song);

    void start();
    void stop();
    void seekSamples(std::int64_t samplePosition);

    [[nodiscard]] FamilySettings getFamilySettings(int family) const;

    [[nodiscard]] std::array<FamilySettings, familyCount>
    getAllFamilySettings() const;

    void setFamilyVolume(int family, int volume);
    void setFamilyEnabled(int family, bool enabled);

    void setFamilyVolumeFactor(int family, float factor);

    [[nodiscard]] bool isPlaying() const;

    [[nodiscard]] MidiChannelState getChannelState(int channel) const;

    [[nodiscard]] int getAdjustedChannelVolume(int channel) const;

    [[nodiscard]] std::int64_t positionSamples() const;
    [[nodiscard]] std::int64_t lengthSamples() const;

    void render(
        float** output,
        int numChannels,
        int numSamples);

    [[nodiscard]] std::string soundFontPath() const;

private:
    void destroySynth();
    void createSynth();

    void sendMidiEvent(const RawMidiEvent& event);
    void resetSynth();

    void rebuildSynthStateAt(std::int64_t samplePosition);

    void updateFamilyFactorLocked(int family);
    void applyFamilyVolumeLocked(int family);

    struct ChannelPlaybackState
    {
        int bankMsb = 0;
        int bankLsb = 0;
        int program = 0;

        int cc7 = 127;
        int cc11 = 127;
        int sustain = 0;

        int pitchBend = 8192;
        int channelPressure = 0;

        std::array<int, 128> controllers {};
        std::array<bool, 128> activeNotes {};
    };

    std::array<ChannelPlaybackState, 16> playbackState {};

    std::array<FamilySettings, familyCount> familySettings_ {};

    MidiTransformer midiTransformer_;

    [[nodiscard]] std::int64_t eventSamplePosition(
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