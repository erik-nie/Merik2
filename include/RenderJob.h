#pragma once

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
    std::vector<juce::File> midiFiles;
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

    [[nodiscard]] static RenderEstimate estimate(const std::vector<juce::File>& files);
    [[nodiscard]] static const char* familyName(int family);

    RenderJob(RenderEngine& engine, RenderOptions options);
    ~RenderJob() override;

    void requestStop() { stopRequested = true; }
    [[nodiscard]] RenderStatus status() const;

private:
    void run() override;
    bool renderPass(const RenderPlan& plan, int family, const juce::File& wavFile, juce::String& problem);
    void note(const juce::String& line);
    void update(const std::function<void(RenderStatus&)>& change);

    RenderEngine& engine;
    const RenderOptions options;
    std::atomic<bool> stopRequested { false };

    mutable juce::CriticalSection lock;
    RenderStatus current;
    double completedSeconds = 0.0;
};
