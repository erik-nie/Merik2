#include "MidiTransformer.h"

#include <algorithm>
#include <cmath>

MidiTransformer::MidiTransformer()
{
    familyVolumeFactors.fill(1.0f);
    reset();
}

void MidiTransformer::reset()
{
    for (auto& state : channelStates)
        state = MidiChannelState {};
}

void MidiTransformer::setNormalizeEnabled(bool enabled)
{
    normalizeEnabled = enabled;
}

RawMidiEvent MidiTransformer::transform(const RawMidiEvent& event)
{
    RawMidiEvent result = event;

    if (event.bytes.size() >= 1 && event.bytes.size() <= 3)
    {
        std::array<std::uint8_t, 3> bytes {};
        std::copy(event.bytes.begin(), event.bytes.end(), bytes.begin());
        transformInPlace(bytes, event.bytes.size());
        std::copy_n(bytes.begin(), event.bytes.size(), result.bytes.begin());
    }

    return result;
}

void MidiTransformer::transformInPlace(std::array<std::uint8_t, 3>& bytes,
                                       std::size_t size)
{
    if (size == 0)
        return;

    const std::uint8_t status = bytes[0];

    if (status >= 0xF0)
        return;

    const int channel = status & 0x0F;
    auto& state = channelStates[static_cast<std::size_t>(channel)];

    ++state.eventCounter;

    const std::uint8_t message = status & 0xF0;

    if (message == 0xC0)
    {
        if (size < 2)
            return;

        const int program = bytes[1] & 0x7F;

        state.program = program;
        state.family = getFamilyFromProgram(program);

        if (channel == 9)
            state.family = 0;

        return;
    }

    if (message == 0xB0)
    {
        if (size < 3)
            return;

        const int controller = bytes[1] & 0x7F;
        const int value = bytes[2] & 0x7F;

        if (controller == 7)
        {
            state.cc7 = value;
            bytes[2] = static_cast<std::uint8_t>(getAdjustedChannelVolume(channel));
        }
        else if (controller == 11)
        {
            state.cc11 = value;
            bytes[2] = static_cast<std::uint8_t>(getAdjustedChannelExpression(channel));
        }
    }
}

void MidiTransformer::setFamilyVolumeFactor(int family, float factor)
{
    if (family < 0 || family >= familyCount)
        return;

    if (!std::isfinite(factor))
        factor = 0.0f;

    familyVolumeFactors[static_cast<std::size_t>(family)] =
        std::clamp(factor, 0.0f, 1.0f);
}

float MidiTransformer::getFamilyVolumeFactor(int family) const
{
    if (family < 0 || family >= familyCount)
        return 1.0f;

    return familyVolumeFactors[static_cast<std::size_t>(family)];
}

int MidiTransformer::getAdjustedChannelVolume(int channel) const
{
    if (channel < 0 || channel >= 16)
        return 0;

    const auto& state =
        channelStates[static_cast<std::size_t>(channel)];

    // Stap 1: bereken het volume na de familieaanpassing.
    const int family =
        (channel == 9) ? 0 : state.family;

    int adjustedVolume = state.cc7;

    if (family >= 0 && family < familyCount)
    {
        adjustedVolume = scaleMidiValue(
            state.cc7,
            familyVolumeFactors[static_cast<std::size_t>(family)]);
    }

    // Stap 2: normalisatie is optioneel.
    if (!normalizeEnabled)
        return adjustedVolume;

    // Kanaal 10 (index 9) is de referentie.
    const auto& drumsState = channelStates[9];

    const int drumsVolume = scaleMidiValue(
        drumsState.cc7,
        familyVolumeFactors[0]);

    // Voorkom deling door nul.
    if (drumsVolume <= 0 || drumsVolume >= 127)
        return adjustedVolume;

    // Stap 3: normaliseer ten opzichte van het drumsvolume.
    return std::clamp(
        static_cast<int>(
            std::lround(
                static_cast<double>(adjustedVolume)
                * 127.0
                / static_cast<double>(drumsVolume))),
        0,
        127);
}

int MidiTransformer::getAdjustedChannelExpression(int channel) const
{
    if (channel < 0 || channel >= 16)
        return 0;

    const auto& state =
        channelStates[static_cast<std::size_t>(channel)];

    if (state.family < 0 || state.family >= familyCount)
        return state.cc11;

    return scaleMidiValue(
        state.cc11,
        familyVolumeFactors[static_cast<std::size_t>(state.family)]);
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

    if (program <= 23)
        return 3;

    if (program <= 31)
        return 2;

    if (program <= 39)
        return 1;

    if (program <= 55)
        return 4;

    if (program <= 79)
        return 5;

    if (program <= 95)
        return 3;

    if (program <= 103)
        return 6;

    if (program <= 119)
        return 3;

    return 6;
}