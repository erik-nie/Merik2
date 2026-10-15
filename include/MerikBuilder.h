#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// Maakt een .merik uit een map: herkent de bestanden (zelfde regels als bij het
// afspelen), zet WAV desgewenst om naar FLAC en zipt alles zonder compressie.
namespace MerikBuilder
{
    struct Item
    {
        juce::File file;
        juce::String zipName;      // naam in het pakket
        juce::String description;  // regel voor het overzicht
        bool isWav = false;        // wav/aiff: kan naar FLAC
    };

    struct Plan
    {
        juce::File folder;
        std::vector<Item> items;
        juce::StringArray ignored;     // bestanden die niet herkend zijn
        juce::StringArray warnings;    // dingen om naar te kijken
        int stemCount = 0;
        bool hasMidi = false;
        bool hasTempo = false;

        [[nodiscard]] bool usable() const { return stemCount > 0 || hasMidi; }
    };

    [[nodiscard]] Plan scan(const juce::File& folder);

    struct Result
    {
        bool ok = false;
        bool cancelled = false;
        juce::String error;
        juce::int64 bytes = 0;
    };

    // progress: (0..1, tekst) -> false = afbreken. Draait op de aanroepende thread.
    [[nodiscard]] Result build(const Plan& plan, bool wavToFlac, const juce::File& output,
                               const std::function<bool(float, const juce::String&)>& progress);
}
