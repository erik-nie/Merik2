#pragma once

#include "Song.h"
#include "StemSet.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
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
        bool cancelled = false;
        bool tempoKnown = true;              // MIDI-bestand of tempo (manifest/tempo.json) aanwezig
    };

    // Voortgang van open(): mag vanaf een andere thread gelezen worden. Zet cancel
    // op true om het laden af te breken.
    struct Progress
    {
        std::atomic<float> fraction { 0.0f };
        std::atomic<int> stemsDone { 0 };
        std::atomic<int> stemsTotal { 0 };
        std::atomic<bool> cancel { false };
    };

    [[nodiscard]] bool isPackage(const juce::File& file);

    // Herkent een stem aan zijn bestandsnaam ("1-drums.flac", "drums.wav", "8-click.wav"):
    // familie 0..7 (7 = klik) of -1. De naam na het nummer, max. 8 tekens, via stemLabel().
    [[nodiscard]] int stemFamily(const juce::String& fileName);
    [[nodiscard]] juce::String stemLabel(const juce::String& fileName);

    // Snel: aantal stems in het manifest (0 als er geen zijn of het bestand ongeldig is).
    [[nodiscard]] int stemCount(const juce::File& package);

    // Alleen de MIDI (snel, voor de lijsten): uitgepakt of uit het manifest gegenereerd.
    [[nodiscard]] juce::File midiFor(const juce::File& package, juce::String& error);

    // chords.json en lyrics.json in het pakket (als ze er zijn) vervangen de
    // akkoorden en lyrics uit het MIDI-bestand.
    void applyScore(const juce::File& package, Song& song);

    // MIDI plus alle stems, omgezet naar deviceRate.
    // De stems worden op meerdere threads tegelijk gedecodeerd. progress is optioneel.
    [[nodiscard]] Contents open(const juce::File& package, double deviceRate, Progress* progress = nullptr);
}
