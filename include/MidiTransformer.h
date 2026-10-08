#pragma once

#include "Song.h"

#include <array>
#include <cstdint>

struct MidiChannelState
{
    int program = -1;
    int family = -1;

    int cc7 = 127;
    int cc11 = 127;

    // Verandert bij ieder MIDI-event op dit kanaal.
    std::uint64_t eventCounter = 0;
};

class MidiTransformer final
{
public:
    MidiTransformer();

    void reset();

    RawMidiEvent transform(const RawMidiEvent& event);

    void setFamilyVolumeFactor(int family, float factor);

    [[nodiscard]] float getFamilyVolumeFactor(int family) const;

    [[nodiscard]] int getAdjustedChannelVolume(int channel) const;

    [[nodiscard]] const MidiChannelState& getChannelState(
        int channel) const;

private:
    std::array<MidiChannelState, 16> channelStates {};
    std::array<float, 16> familyVolumeFactors {};

    static int getFamilyFromProgram(int program);

    static int scaleMidiValue(int value, float factor);
};