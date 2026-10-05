#include "TempoMap.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

TempoMap::TempoMap(int ticksPerQuarterNote) : ppq(ticksPerQuarterNote)
{
    if (ppq <= 0)
        throw std::invalid_argument("ticksPerQuarterNote must be positive");

    tempoPoints.push_back({ 0, 0.0, 120.0 });
}

void TempoMap::addTempo(std::int64_t tick, double bpm)
{
    if (tick < 0 || bpm <= 0.0)
        return;

    tempoPoints.push_back({ tick, 0.0, bpm });
}

void TempoMap::finalise()
{
    std::stable_sort(tempoPoints.begin(), tempoPoints.end(),
        [](const TempoPoint& a, const TempoPoint& b) { return a.tick < b.tick; });

    // If multiple tempo events occur at the same tick, keep the last one.
    std::vector<TempoPoint> compact;
    for (const auto& point : tempoPoints)
    {
        if (!compact.empty() && compact.back().tick == point.tick)
            compact.back().bpm = point.bpm;
        else
            compact.push_back(point);
    }
    tempoPoints = std::move(compact);

    tempoPoints.front().seconds = 0.0;
    for (std::size_t i = 1; i < tempoPoints.size(); ++i)
    {
        const auto& previous = tempoPoints[i - 1];
        const auto deltaTicks = tempoPoints[i].tick - previous.tick;
        const auto secondsPerTick = 60.0 / (previous.bpm * static_cast<double>(ppq));
        tempoPoints[i].seconds = previous.seconds + static_cast<double>(deltaTicks) * secondsPerTick;
    }
}

double TempoMap::tickToSeconds(std::int64_t tick) const
{
    if (tick <= 0)
        return 0.0;

    const auto it = std::upper_bound(tempoPoints.begin(), tempoPoints.end(), tick,
        [](std::int64_t value, const TempoPoint& point) { return value < point.tick; });

    const auto& active = (it == tempoPoints.begin()) ? tempoPoints.front() : *std::prev(it);
    const auto deltaTicks = tick - active.tick;
    const auto secondsPerTick = 60.0 / (active.bpm * static_cast<double>(ppq));
    return active.seconds + static_cast<double>(deltaTicks) * secondsPerTick;
}

std::int64_t TempoMap::tickToSample(std::int64_t tick, double sampleRate) const
{
    return static_cast<std::int64_t>(std::llround(tickToSeconds(tick) * sampleRate));
}

const std::vector<TempoPoint>& TempoMap::points() const noexcept
{
    return tempoPoints;
}
