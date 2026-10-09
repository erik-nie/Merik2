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

    auto& state = channelStates[static_cast<std::size_t>(channel)];

    ++state.eventCounter;

    const std::uint8_t message = status & 0xF0;

    if (message == 0xC0)
    {
        if (event.bytes.size() < 2)
            return result;

        const int program = event.bytes[1] & 0x7F;

        state.program = program;
        state.family = getFamilyFromProgram(program);

        if (channel == 9)
            state.family = 0;

        return result;
    }

    if (message == 0xB0)
    {
        if (event.bytes.size() < 3)
            return result;

        const int controller = event.bytes[1] & 0x7F;
        const int value = event.bytes[2] & 0x7F;

        if (controller == 7)
        {
            state.cc7 = value;

            result.bytes[2] = static_cast<std::uint8_t>(
                getAdjustedChannelVolume(channel));
        }
        else if (controller == 11)
        {
            state.cc11 = value;

            result.bytes[2] = static_cast<std::uint8_t>(
                getAdjustedChannelExpression(channel));
        }
    }

    return result;
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

    if (state.family < 0 || state.family >= familyCount)
        return state.cc7;

    return scaleMidiValue(
        state.cc7,
        familyVolumeFactors[static_cast<std::size_t>(state.family)]);
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