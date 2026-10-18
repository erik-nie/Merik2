#pragma once

#include "Song.h"

#include <array>
#include <cstdint>
#include <vector>

// Verdeelt de MIDI van een nummer over 7 "passes", één per familie (1 Drums ...
// 7 Others), voor het opnemen van elke familie apart via een externe module.
// Een kanaal hoort bij de familie van zijn programma op het moment van de noot
// (kanaal 10 = Drums); omschakelen midden in het nummer wordt gevolgd.
struct ScheduledMidi
{
    double seconds = 0.0;                 // tijd in het nummer
    std::array<std::uint8_t, 3> data {};
    int size = 0;
};

struct RenderPlan
{
    static constexpr int passCount = 7;

    std::array<std::vector<ScheduledMidi>, passCount> passes;
    std::array<int, passCount> noteCount {};   // noten per familie (0 = pass overslaan)
    double lengthSeconds = 0.0;                // tijd van het laatste kanaalbericht

    [[nodiscard]] bool hasNotes(int family) const { return noteCount[static_cast<std::size_t>(family)] > 0; }

    // Alleen de eerste `seconds` seconden houden (voor een demo-render); families zonder
    // noten in dat stuk vervallen. Noten die nog klinken worden aan het eind uitgezet.
    void limitTo(double seconds);
};

[[nodiscard]] RenderPlan buildRenderPlan(const Song& song);
