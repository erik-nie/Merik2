#pragma once

#include "Song.h"
#include <cstdint>
#include <vector>

class TempoMap
{
public:
    explicit TempoMap(int ticksPerQuarterNote = 480);

    void addTempo(std::int64_t tick, double bpm);
    void finalise();
    [[nodiscard]] double tickToSeconds(std::int64_t tick) const;
    [[nodiscard]] std::int64_t tickToSample(std::int64_t tick, double sampleRate) const;
    [[nodiscard]] const std::vector<TempoPoint>& points() const noexcept;

private:
    int ppq;
    std::vector<TempoPoint> tempoPoints;
};
