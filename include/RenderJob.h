#pragma once

#include "FluidSynthEngine.h"
#include "RenderEngine.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

// Neemt MIDI-bestanden op via een externe module: per nummer 1 pass per familie
// (alleen families met noten), elk als WAV, daarna een .merik met een
// delay.json met de gemeten vertraging.
struct RenderOptions
{
    juce::String midiOutIdentifier;
    juce::String inputDevice;        // leeg = hetzelfde apparaat als de uitgang van Merik
    int inputRoute = 0;
    double delayMs = 0.0;            // gemeten vertraging MIDI-uit -> audio-in
    juce::File outputFolder;
    bool keepWav = false;            // de WAV's bewaren in een map naast de .merik
    bool toFlac = true;              // in de .merik als FLAC
    bool gmReset = false;            // GM-reset voor elke pass (uit: sommige software-synths verliezen er de drums door)
    bool demo = false;               // alleen de eerste 30 seconden van elke track
    std::vector<juce::File> midiFiles;

    // Intern: Merik's eigen SoundFont, sneller dan real-time en zonder hardware.
    bool internalSynth = false;
    std::string soundFontPath;
    SynthSettings synthSettings;
    bool normalize = false;
};

// Instellingen van het opnemen (worden onthouden).
struct RenderPrefs
{
    juce::String midiOutIdentifier;
    juce::String inputDevice;        // leeg = hetzelfde apparaat als de uitgang van Merik
    int inputRoute = 0;              // 0 = nog niet gekozen
    double delayMs = 0.0;
    juce::String outputFolder;
    bool keepWav = false;
    bool toFlac = true;
    bool gmReset = false;
    bool demo = false;
    bool internalSynth = false;      // Merik's SoundFont in plaats van een externe module
};

struct RenderEstimate
{
    int songs = 0;
    int passes = 0;
    double seconds = 0.0;            // echte tijd: elke pass duurt zo lang als het nummer
    juce::StringArray notes;         // bijv. bestanden zonder noten
};

struct RenderStatus
{
    bool running = false;
    bool finished = false;
    bool stopped = false;
    int song = 0, songs = 0;
    int pass = 0, passes = 0;
    juce::String songName, passName, step;
    double doneSeconds = 0.0, totalSeconds = 0.0;
    juce::StringArray log;           // resultaten en waarschuwingen
    juce::String error;
};

struct RenderPlan;

class RenderJob final : public juce::Thread
{
public:
    static constexpr double prerollSeconds = 0.6;
    static constexpr double tailSeconds = 3.0;

    static constexpr double demoSeconds = 30.0;

    [[nodiscard]] static RenderEstimate estimate(const std::vector<juce::File>& files, bool demo = false, bool internal = false);

    // Naam van het resultaat voor een MIDI-bestand: "Song.merik" of "Song (demo 30s).merik".
    [[nodiscard]] static juce::String outputBaseName(const juce::File& midiFile, bool demo);
    [[nodiscard]] static const char* familyName(int family);

    // engine mag nullptr zijn bij internal rendering.
    RenderJob(RenderEngine* engine, RenderOptions options);
    ~RenderJob() override;

    void requestStop() { stopRequested = true; }
    [[nodiscard]] RenderStatus status() const;

private:
    void run() override;
    bool renderPass(const RenderPlan& plan, int family, const juce::File& wavFile, juce::String& problem);
    void note(const juce::String& line);
    void update(const std::function<void(RenderStatus&)>& change);

    bool renderInternalPass(const std::shared_ptr<const Song>& song, double lengthSeconds, int family,
                            const juce::File& wavFile, juce::String& problem);

    RenderEngine* engine;
    std::unique_ptr<FluidSynthEngine> synth;
    const RenderOptions options;
    std::atomic<bool> stopRequested { false };

    mutable juce::CriticalSection lock;
    RenderStatus current;
    double completedSeconds = 0.0;
};
