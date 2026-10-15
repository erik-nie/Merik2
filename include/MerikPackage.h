#pragma once

#include "StemSet.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

// Een .merik-bestand: een zip met manifest.json, optioneel song.mid en
// audio-stems (wav/flac/mp3) per familie. Zie docs/merik-format.md.
namespace MerikPackage
{
    struct Contents
    {
        juce::File midiFile;                 // uitgepakt of gegenereerd (cache)
        std::shared_ptr<StemSet> stems;      // al op de samplefrequentie van het apparaat
        juce::String error;                  // leeg = gelukt
    };

    [[nodiscard]] bool isPackage(const juce::File& file);

    // Snel: aantal stems in het manifest (0 als er geen zijn of het bestand ongeldig is).
    [[nodiscard]] int stemCount(const juce::File& package);

    // Alleen de MIDI (snel, voor de lijsten): uitgepakt of uit het manifest gegenereerd.
    [[nodiscard]] juce::File midiFor(const juce::File& package, juce::String& error);

    // MIDI plus alle stems, omgezet naar deviceRate.
    [[nodiscard]] Contents open(const juce::File& package, double deviceRate);
}
