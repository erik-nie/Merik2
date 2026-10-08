#include "MidiTransformer.h"

#include <algorithm>
#include <cmath>

MidiTransformer::MidiTransformer()
{
    familyVolumeFactors.fill(1.27f);
    reset();
}

void MidiTransformer::reset()
{
    for (auto& state : channelStates)
        state = MidiChannelState {};
}

RawMidiEvent MidiTransformer::transform(const RawMidiEvent& event)
{
    RawMidiEvent result = event;

    if (event.bytes.empty())
        return result;

    const std::uint8_t status = event.bytes[0];

    if (status >= 0xF0)
        return result;

    const int channel = status & 0x0F;

    if (channel < 0 || channel >= 16)
        return result;

    const std::size_t channelIndex = static_cast<std::size_t>(channel);
    auto& state = channelStates[channelIndex];

    ++state.eventCounter;

    const std::uint8_t message = status & 0xF0;

    // Program Change
    if (message == 0xC0)
    {
        if (event.bytes.size() < 2)
            return result;

        const int program = event.bytes[1] & 0x7F;

        state.program = program;
        state.family = getFamilyFromProgram(program);

        // MIDI kanaal 10 = drums.
        // MIDI channels zijn 0-based, dus kanaal 9 = CH10.
        if (channel == 9)
            state.family = 0;

        return result;
    }

    // Control Change
    if (message == 0xB0)
    {
        if (event.bytes.size() < 3)
            return result;

        const int controller = event.bytes[1] & 0x7F;
        const int originalValue = event.bytes[2] & 0x7F;

        if (controller == 7)
        {
            // Bewaar altijd de originele CC7.
            // De family-slider wordt daar bovenop toegepast.
            state.cc7 = originalValue;

            const int family = state.family;

            if (family >= 0 && family < 16)
            {
                const float factor =
                    familyVolumeFactors[static_cast<std::size_t>(family)];

                result.bytes[2] =
                    static_cast<std::uint8_t>(
                        scaleMidiValue(originalValue, factor));
            }
        }
        else if (controller == 11)
        {
            state.cc11 = originalValue;

            const int family = state.family;

            if (family >= 0 && family < 16)
            {
                const float factor =
                    familyVolumeFactors[static_cast<std::size_t>(family)];

                result.bytes[2] =
                    static_cast<std::uint8_t>(
                        scaleMidiValue(originalValue, factor));
            }
        }

        return result;
    }

    return result;
}

void MidiTransformer::setFamilyVolumeFactor(int family, float factor)
{
    if (family < 0 || family >= 16)
        return;

    familyVolumeFactors[static_cast<std::size_t>(family)] =
        std::clamp(factor, 0.0f, 1.0f);
}

float MidiTransformer::getFamilyVolumeFactor(int family) const
{
    if (family < 0 || family >= 16)
        return 1.0f;

    return familyVolumeFactors[static_cast<std::size_t>(family)];
}

int MidiTransformer::getAdjustedChannelVolume(int channel) const
{
    if (channel < 0 || channel >= 16)
        return 0;

    const auto& state =
        channelStates[static_cast<std::size_t>(channel)];

    if (state.family < 0 || state.family >= 16)
        return state.cc7;

    const float factor =
        familyVolumeFactors[static_cast<std::size_t>(state.family)];

    return scaleMidiValue(state.cc7, factor);
}

const MidiChannelState& MidiTransformer::getChannelState(int channel) const
{
    static const MidiChannelState invalidState {};

    if (channel < 0 || channel >= 16)
        return invalidState;

    return channelStates[static_cast<std::size_t>(channel)];
}

int MidiTransformer::scaleMidiValue(int value, float factor)
{
    return std::clamp(
        static_cast<int>(
            std::lround(static_cast<float>(value) * factor)),
        0,
        127);
}

int MidiTransformer::getFamilyFromProgram(int program)
{
    if (program < 0 || program >= 128)
        return -1;

    // Exact dezelfde indeling als de Python Merik-versie.

    if (program <= 23)
        return 3; // Keys

    if (program <= 31)
        return 2; // Guitars

    if (program <= 39)
        return 1; // Bass

    if (program <= 55)
        return 4; // Strings

    if (program <= 79)
        return 5; // Winds

    if (program <= 95)
        return 3; // Keys

    if (program <= 103)
        return 6; // FX

    if (program <= 119)
        return 3; // Keys

    return 6; // FX
}