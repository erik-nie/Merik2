#include "MidiTransformer.h"

#include <algorithm>
#include <cmath>

MidiTransformer::MidiTransformer()
{
    // Family volume factors start at unity:
    // 100% of the original MIDI volume.
    familyVolumeFactors.fill(1.0f);
    reset();
}

void MidiTransformer::reset()
{
    // Reset alleen de actuele MIDI-kanaalstatus.
    // De ingestelde family-volume-factoren blijven behouden.
    for (auto& state : channelStates)
        state = MidiChannelState {};
}

RawMidiEvent MidiTransformer::transform(const RawMidiEvent& event)
{
    RawMidiEvent result = event;

    if (event.bytes.empty())
        return result;

    const std::uint8_t status = event.bytes[0];

    // MIDI system messages worden niet per kanaal verwerkt.
    if (status >= 0xF0)
        return result;

    const int channel = status & 0x0F;

    if (channel < 0 || channel >= 16)
        return result;

    const std::size_t channelIndex =
        static_cast<std::size_t>(channel);

    auto& state = channelStates[channelIndex];

    // Ieder MIDI-event op dit kanaal activeert de GUI.
    ++state.eventCounter;

    const std::uint8_t message = status & 0xF0;

    // ------------------------------------------------------------
    // Program Change
    // ------------------------------------------------------------

    if (message == 0xC0)
    {
        if (event.bytes.size() < 2)
            return result;

        const int program =
            event.bytes[1] & 0x7F;

        state.program = program;
        state.family = getFamilyFromProgram(program);

        return result;
    }

    // ------------------------------------------------------------
    // Control Change
    // ------------------------------------------------------------

    if (message == 0xB0)
    {
        if (event.bytes.size() < 3)
            return result;

        const int controller =
            event.bytes[1] & 0x7F;

        const int originalValue =
            event.bytes[2] & 0x7F;

        if (controller == 7)
        {
            state.cc7 = originalValue;

            const int family = state.family;

            if (family >= 0 && family < 16)
            {
                const float factor =
                    familyVolumeFactors[
                        static_cast<std::size_t>(family)];

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
                    familyVolumeFactors[
                        static_cast<std::size_t>(family)];

                result.bytes[2] =
                    static_cast<std::uint8_t>(
                        scaleMidiValue(originalValue, factor));
            }
        }

        return result;
    }

    return result;
}

void MidiTransformer::setFamilyVolumeFactor(
    int family,
    float factor)
{
    if (family < 0 || family >= 16)
        return;

    familyVolumeFactors[
        static_cast<std::size_t>(family)] =
        std::clamp(factor, 0.0f, 1.0f);
}

float MidiTransformer::getFamilyVolumeFactor(int family) const
{
    if (family < 0 || family >= 16)
        return 1.0f;

    return familyVolumeFactors[
        static_cast<std::size_t>(family)];
}

const MidiChannelState& MidiTransformer::getChannelState(
    int channel) const
{
    static const MidiChannelState invalidState {};

    if (channel < 0 || channel >= 16)
        return invalidState;

    return channelStates[
        static_cast<std::size_t>(channel)];
}

int MidiTransformer::scaleMidiValue(
    int value,
    float factor)
{
    return std::clamp(
        static_cast<int>(
            std::lround(
                static_cast<float>(value) * factor)),
        0,
        127);
}

int MidiTransformer::getFamilyFromProgram(int program)
{
    if (program < 0 || program >= 128)
        return -1;

    // Voorlopige GM-family indeling:
    // 0-7    Piano
    // 8-15   Chromatic Percussion
    // 16-23  Organ
    // ...
    return program / 8;
}