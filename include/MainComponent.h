#pragma once

#include "FluidSynthEngine.h"

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

class MainComponent final
    : public juce::AudioAppComponent,
      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(
        int samplesPerBlockExpected,
        double sampleRate) override;

    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& bufferToFill) override;

    void releaseResources() override;

    void paint(juce::Graphics& g) override;

    void resized() override;

private:
    void timerCallback() override;

    void openMidi();

    void openSoundFont();

    void showAudioSettings();

    void updateTransport();

    static juce::String formatTime(
        std::int64_t milliseconds);

    FluidSynthEngine synth_;

    std::shared_ptr<const Song> song_;

    juce::TextButton openMidiButton { "Open MIDI" };
    juce::TextButton soundFontButton { "SoundFont" };
    juce::TextButton settingsButton { "Audio Settings" };

    juce::TextButton playButton { "▶" };
    juce::TextButton stopButton { "■" };

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label positionLabel;
    juce::Label bpmLabel;
    juce::Label soundFontLabel;

    juce::Slider positionSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};