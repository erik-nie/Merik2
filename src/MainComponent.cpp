#include "MainComponent.h"

#include "MidiFileReader.h"

#include <algorithm>
#include <cmath>

MainComponent::MainComponent()
{
    setOpaque(true);

    addAndMakeVisible(openMidiButton);
    addAndMakeVisible(soundFontButton);
    addAndMakeVisible(settingsButton);

    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(positionLabel);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(soundFontLabel);

    addAndMakeVisible(positionSlider);

    titleLabel.setText(
        "MERIK",
        juce::dontSendNotification);

    titleLabel.setFont(
        juce::FontOptions(28.0f)
            .withStyle("Bold"));

    titleLabel.setColour(
        juce::Label::textColourId,
        juce::Colours::white);

    statusLabel.setText(
        "Ready",
        juce::dontSendNotification);

    statusLabel.setColour(
        juce::Label::textColourId,
        juce::Colours::lightgrey);

    positionLabel.setText(
        "00:00 / 00:00",
        juce::dontSendNotification);

    positionLabel.setFont(
        juce::FontOptions(24.0f)
            .withStyle("Bold"));

    positionLabel.setColour(
        juce::Label::textColourId,
        juce::Colours::white);

    positionLabel.setJustificationType(
        juce::Justification::centred);

    bpmLabel.setText(
        "BPM --",
        juce::dontSendNotification);

    bpmLabel.setColour(
        juce::Label::textColourId,
        juce::Colours::lightgrey);

    soundFontLabel.setText(
        "No SoundFont loaded",
        juce::dontSendNotification);

    soundFontLabel.setColour(
        juce::Label::textColourId,
        juce::Colours::lightgrey);

    positionSlider.setRange(
        0.0,
        1.0,
        0.000001);

    positionSlider.setSliderStyle(
        juce::Slider::LinearHorizontal);

    positionSlider.setTextBoxStyle(
        juce::Slider::NoTextBox,
        false,
        0,
        0);

    positionSlider.setEnabled(false);

    openMidiButton.onClick =
        [this]
        {
            openMidi();
        };

    soundFontButton.onClick =
        [this]
        {
            openSoundFont();
        };

    settingsButton.onClick =
        [this]
        {
            showAudioSettings();
        };

    playButton.onClick =
        [this]
        {
            if (synth_.isPlaying())
                synth_.stop();
            else
                synth_.start();

            updateTransport();
        };

    stopButton.onClick =
        [this]
        {
            synth_.stop();
            synth_.seekSamples(0);

            updateTransport();
        };

    setSize(1000, 650);

    /*
        Start the JUCE audio device.

        We request stereo output only.
        JUCE then owns the CoreAudio device.
    */
    setAudioChannels(0, 2);

    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();

    shutdownAudio();
}

void MainComponent::prepareToPlay(
    int samplesPerBlockExpected,
    double sampleRate)
{
    juce::ignoreUnused(samplesPerBlockExpected);

    synth_.setSampleRate(sampleRate);
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr)
        return;

    auto* buffer = bufferToFill.buffer;

    const int start =
        bufferToFill.startSample;

    const int numSamples =
        bufferToFill.numSamples;

    buffer->clear(
        start,
        numSamples);

    float* outputs[2] =
    {
        buffer->getNumChannels() > 0
            ? buffer->getWritePointer(0, start)
            : nullptr,

        buffer->getNumChannels() > 1
            ? buffer->getWritePointer(1, start)
            : nullptr
    };

    synth_.render(
        outputs,
        std::min(2, buffer->getNumChannels()),
        numSamples);
}

void MainComponent::releaseResources()
{
}

void MainComponent::paint(
    juce::Graphics& g)
{
    g.fillAll(
        juce::Colour::fromRGB(
            20,
            22,
            27));

    auto bounds =
        getLocalBounds();

    auto header =
        bounds.removeFromTop(76);

    g.setColour(
        juce::Colour::fromRGB(
            28,
            31,
            38));

    g.fillRect(header);

    auto main =
        bounds.reduced(32);

    g.setColour(
        juce::Colour::fromRGB(
            32,
            35,
            42));

    g.fillRoundedRectangle(
        main.toFloat(),
        12.0f);

    auto centre =
        main.reduced(28);

    g.setColour(
        juce::Colour::fromRGB(
            42,
            45,
            52));

    g.fillRoundedRectangle(
        centre.toFloat(),
        10.0f);
}

void MainComponent::resized()
{
    auto area =
        getLocalBounds();

    auto header =
        area.removeFromTop(76)
            .reduced(24, 12);

    titleLabel.setBounds(
        header.removeFromLeft(180));

    statusLabel.setBounds(
        header.removeFromRight(180));

    auto controls =
        area.reduced(32);

    openMidiButton.setBounds(
        controls.removeFromTop(40)
            .removeFromLeft(120));

    controls.removeFromTop(12);

    soundFontButton.setBounds(
        controls.removeFromTop(36)
            .removeFromLeft(120));

    controls.removeFromTop(8);

    settingsButton.setBounds(
        controls.removeFromTop(36)
            .removeFromLeft(160));

    controls.removeFromTop(35);

    titleLabel.setBounds(
        controls.removeFromTop(45));

    positionLabel.setBounds(
        controls.removeFromTop(55));

    controls.removeFromTop(12);

    positionSlider.setBounds(
        controls.removeFromTop(28));

    controls.removeFromTop(18);

    auto transport =
        controls.removeFromTop(55);

    playButton.setBounds(
        transport.removeFromLeft(80));

    transport.removeFromLeft(10);

    stopButton.setBounds(
        transport.removeFromLeft(80));

    controls.removeFromTop(20);

    bpmLabel.setBounds(
        controls.removeFromTop(30));

    soundFontLabel.setBounds(
        controls.removeFromTop(30));
}

void MainComponent::timerCallback()
{
    updateTransport();
}

void MainComponent::updateTransport()
{
    const auto position =
        synth_.positionSamples();

    const auto length =
        synth_.lengthSamples();

    const double sampleRate =
        48000.0;

    const auto positionMs =
        static_cast<std::int64_t>(
            position / sampleRate * 1000.0);

    const auto lengthMs =
        static_cast<std::int64_t>(
            length / sampleRate * 1000.0);

    positionLabel.setText(
        formatTime(positionMs)
        + " / "
        + formatTime(lengthMs),
        juce::dontSendNotification);

    const bool playing =
        synth_.isPlaying();

    statusLabel.setText(
        playing ? "Playing" : "Ready",
        juce::dontSendNotification);

    playButton.setButtonText(
        playing ? "Ⅱ" : "▶");

    if (length > 0)
    {
        positionSlider.setValue(
            static_cast<double>(position)
            / static_cast<double>(length),
            juce::dontSendNotification);
    }

    const auto fontPath =
        synth_.soundFontPath();

    if (fontPath.empty())
    {
        soundFontLabel.setText(
            "No SoundFont loaded",
            juce::dontSendNotification);
    }
    else
    {
        soundFontLabel.setText(
            juce::File(fontPath).getFileName(),
            juce::dontSendNotification);
    }
}

void MainComponent::openMidi()
{
    auto chooser =
        std::make_shared<juce::FileChooser>(
            "Open MIDI file",
            juce::File{},
            "*.mid;*.midi");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles,

        [this, chooser]
        (const juce::FileChooser& fc)
        {
            const auto file =
                fc.getResult();

            if (!file.existsAsFile())
                return;

            try
            {
                auto song =
                    std::make_shared<Song>(
                        MidiFileReader{}.read(
                            file.getFullPathName()
                                .toStdString()));

                song_ = song;

                synth_.loadSong(song_);

                titleLabel.setText(
                    file.getFileNameWithoutExtension(),
                    juce::dontSendNotification);

                statusLabel.setText(
                    "MIDI loaded",
                    juce::dontSendNotification);

                bpmLabel.setText(
                    song_->tempoMap.empty()
                        ? "BPM --"
                        : "BPM "
                          + juce::String(
                              song_->tempoMap.front().bpm,
                              1),
                    juce::dontSendNotification);

                positionSlider.setEnabled(true);

                updateTransport();
            }
            catch (const std::exception& e)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "MIDI error",
                    e.what());
            }
        });
}

void MainComponent::openSoundFont()
{
    auto chooser =
        std::make_shared<juce::FileChooser>(
            "Select SoundFont",
            juce::File{},
            "*.sf2;*.SF2");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles,

        [this, chooser]
        (const juce::FileChooser& fc)
        {
            const auto file =
                fc.getResult();

            if (!file.existsAsFile())
                return;

            std::string error;

            if (!synth_.loadSoundFont(
                    file.getFullPathName().toStdString(),
                    error))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "SoundFont error",
                    error);
            }

            updateTransport();
        });
}

void MainComponent::showAudioSettings()
{
    auto* selector =
        new juce::AudioDeviceSelectorComponent(
            deviceManager,
            0,
            0,
            2,
            2,
            false,
            false,
            false,
            false);

    selector->setSize(
        650,
        420);

    auto* window =
        new juce::DialogWindow(
            "Audio Settings",
            juce::Colours::darkgrey,
            true);

    window->setContentOwned(
        selector,
        true);

    window->centreWithSize(
        650,
        420);

    window->setResizable(
        true,
        false);

    window->setVisible(true);
}

juce::String MainComponent::formatTime(
    std::int64_t milliseconds)
{
    const auto minutes =
        milliseconds / 60000;

    const auto seconds =
        (milliseconds / 1000) % 60;

    return juce::String::formatted(
        "%02lld:%02lld",
        static_cast<long long>(minutes),
        static_cast<long long>(seconds));
}