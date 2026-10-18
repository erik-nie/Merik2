#pragma once

#include "CaptureSession.h"
#include "MidiOutSink.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

// Het apparaat voor het opnemen van een externe module: opent de MIDI-uitgang,
// zet de gekozen ingangskanalen van het audioapparaat aan en hangt een callback
// aan dat MIDI verstuurt en opneemt (zie CaptureSession). start() en stop()
// horen op de hoofdthread (ze herconfigureren het audioapparaat).
class RenderEngine final : private juce::AudioIODeviceCallback
{
public:
    // Ingangsroute: 2000 + n = kanalen n en n+1 (stereo), 1000 + n = alleen kanaal n (mono).
    static constexpr int pairBase = 2000;
    static constexpr int monoBase = 1000;

    explicit RenderEngine(juce::AudioDeviceManager& deviceManager);
    ~RenderEngine() override;

    // false: fout in `error`.
    bool start(const juce::String& midiOutIdentifier, const juce::String& inputDevice, int inputRoute, juce::String& error);
    void stop();

    [[nodiscard]] bool isRunning() const noexcept { return running; }
    [[nodiscard]] double sampleRate() const noexcept { return rate.load(); }
    [[nodiscard]] int channels() const noexcept { return channelCount; }

    [[nodiscard]] CaptureSession& session() noexcept { return capture; }
    [[nodiscard]] MidiOutSink& midi() noexcept { return sink; }

    // Namen van alle ingangsapparaten (bijv. "BlackHole 2ch" of de interface).
    [[nodiscard]] static juce::StringArray inputDeviceNames(juce::AudioDeviceManager& manager);

    // De ingangskanalen van een ingangsapparaat, ook als het nu niet openstaat. Leeg
    // `preferredDevice` = hetzelfde apparaat als de uitgang van Merik. `usedDevice` krijgt
    // de naam van het apparaat dat gebruikt wordt. Alleen op de hoofdthread.
    [[nodiscard]] static juce::StringArray availableInputChannels(juce::AudioDeviceManager& manager,
                                                                  const juce::String& preferredDevice = {},
                                                                  juce::String* usedDevice = nullptr);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}

    juce::AudioDeviceManager& manager;
    juce::AudioDeviceManager::AudioDeviceSetup savedSetup;
    bool setupChanged = false;
    bool running = false;

    MidiOutSink sink;
    CaptureSession capture;
    std::atomic<double> rate { 48000.0 };
    int channelCount = 1;
};
