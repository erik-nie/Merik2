#pragma once

#include <array>
#include <atomic>

// Toewijzing van de MIDI-controller (standaard: Korg nanoKONTROL2) aan de
// bediening. Wordt gelezen op de MIDI-thread en aangepast vanuit Settings,
// vandaar de atomics. De standaardwaarden zijn die van Merik 1.
struct MidiMapping
{
    static constexpr int families = 8;

    std::array<std::atomic<int>, families> fader;   // CC -> family-volume
    std::array<std::atomic<int>, families> enable;  // CC ('R'-knop) -> family aan/uit
    std::atomic<int> play { 41 };
    std::atomic<int> stop { 42 };
    std::atomic<int> next { 44 };
    std::atomic<int> panic { 45 };

    MidiMapping()
    {
        for (int i = 0; i < families; ++i)
        {
            fader[static_cast<std::size_t>(i)] = i;
            enable[static_cast<std::size_t>(i)] = 64 + i;
        }
    }
};
