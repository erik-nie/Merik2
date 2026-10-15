#pragma once

#include <cstdint>
#include <vector>

// Audio-stems van een .merik-bestand, al omgezet naar de samplefrequentie van
// het audioapparaat. Een stem hoort bij een familie (0 = Drums ... 6 = Others).
struct StemTrack
{
    int family = 0;
    float gain = 1.0f;
    std::int64_t offset = 0;    // samples; positief = audio begint later dan de MIDI
    std::vector<float> left;
    std::vector<float> right;
};

struct StemSet
{
    std::vector<StemTrack> tracks;
    std::int64_t lengthSamples = 0;

    [[nodiscard]] std::uint32_t familyMask() const
    {
        std::uint32_t mask = 0;

        for (const auto& track : tracks)
            mask |= 1u << track.family;

        return mask;
    }
};
