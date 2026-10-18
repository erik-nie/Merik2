#pragma once

#include "FluidSynthEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>

// Stuurt de MIDI-events van de engine naar een echte (of virtuele) MIDI-uitgang.
//
// Timing: de engine meldt per audioblok de samplepositie (beginBlock) en per
// event de samplepositie waarop het moet klinken. Daaruit wordt een vaste
// afbeelding "samplepositie -> kloktijd" afgeleid die de jitter van de
// audio-callbacks wegfiltert. Elk bericht krijgt zo een eigen tijdstip en
// wordt vooruit ingepland, op macOS direct bij CoreMIDI (die het met een
// tijdstempel op het juiste moment afgeeft), elders via juce::MidiOutput.
//
// De audio-thread mag niet blokkeren of alloceren: push() zet alleen een
// bericht in een vaste, lock-vrije wachtrij; een aparte thread levert af.
class MidiOutSink final : public ExternalMidiOutput,
                          private juce::Thread
{
public:
    MidiOutSink();
    ~MidiOutSink() override;

    // Opent het apparaat met deze juce-identifier. false = mislukt.
    bool open(const juce::String& deviceIdentifier);
    void close();

    [[nodiscard]] bool isOpen() const noexcept { return opened.load(); }

    // ExternalMidiOutput
    void beginBlock(std::int64_t sampleAtBlockStart, int numSamples, double sampleRate) override;
    void push(const std::uint8_t* data, int size, std::int64_t sampleTime) override;
    void pushNow(const std::uint8_t* data, int size) override;
    void allNotesOff(int channel) override;

    // Voorsprong op "nu" (ms). Houdt thread-jitter buiten de timing.
    static constexpr double leadMs = 20.0;

private:
    struct Message
    {
        double timeMs = 0.0;          // kloktijd (ms); 0 = meteen
        std::array<std::uint8_t, 8> data {};     // ook korte SysEx (bijv. GM-reset)
        int size = 0;
    };

    void enqueue(const std::uint8_t* data, int size, double timeMs);
    void sendNowDirect(const std::uint8_t* data, int size);
    void run() override;

    struct Native;                    // CoreMIDI (macOS)
    std::unique_ptr<Native> native;

    std::unique_ptr<juce::MidiOutput> juceOutput;   // fallback / andere platforms
    std::atomic<bool> opened { false };

    // Kloktoewijzing (alleen de audio-thread schrijft).
    bool haveBase = false;
    double baseMs = 0.0;              // kloktijd van sample 0
    double sampleRate = 48000.0;
    std::int64_t expectedNextSample = 0;
    double lastBlockMs = 0.0;

    static constexpr int capacity = 8192;
    std::array<Message, capacity> queue {};
    juce::AbstractFifo fifo { capacity };
    juce::WaitableEvent wake;
};
