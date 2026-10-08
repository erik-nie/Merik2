#include "MainComponent.h"

#include <cmath>
#include <exception>

// ============================================================================
// Load MIDI
// ============================================================================

void MainComponent::loadMidi()
{
    auto chooser =
        std::make_shared<juce::FileChooser>(
            "Select MIDI file",
            currentMidiFile.existsAsFile()
                ? currentMidiFile.getParentDirectory()
                : juce::File(),
            "*.mid;*.midi;*.MID;*.MIDI;*.kar;*.KAR");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            const auto file =
                fc.getResult();

            if (!file.existsAsFile())
                return;

            try
            {
                const double sampleRate =
                    audioReady
                        ? audioSampleRate
                        : 48000.0;

                const Song song =
                    midiReader.read(
                        file.getFullPathName().toStdString(),
                        sampleRate);

                currentSong =
                    std::make_shared<Song>(
                        std::move(song));

                currentMidiFile = file;

                webServer.setSong(currentSong);

                isPlaying = false;

                synthEngine.stop();

                synthEngine.setSampleRate(
                    sampleRate);

                synthEngine.loadSong(
                    currentSong);

                updateChannelModel();

                updateSongDisplay();
            }
            catch (const std::exception& e)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Could not load MIDI",
                    e.what());
            }
        });
}

// ============================================================================
// SoundFont
// ============================================================================

void MainComponent::selectSoundFont()
{
    auto chooser =
        std::make_shared<juce::FileChooser>(
            "Select SoundFont",
            currentSoundFont.existsAsFile()
                ? currentSoundFont.getParentDirectory()
                : juce::File(),
            "*.sf2;*.SF2");

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            const auto file =
                fc.getResult();

            if (!file.existsAsFile())
                return;

            std::string error;

            if (!synthEngine.loadSoundFont(
                    file.getFullPathName().toStdString(),
                    error))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Could not load SoundFont",
                    juce::String(error));

                return;
            }

            currentSoundFont = file;

            if (auto* properties = appProperties.getUserSettings())
            {
                properties->setValue(
                    "soundFontPath",
                    currentSoundFont.getFullPathName());

                properties->saveIfNeeded();
            }

            auto name =
                currentSoundFont.getFileName();

            if (name.length() > 27)
            {
                name =
                    name.substring(0, 24) + "...";
            }

            soundFontButton.setButtonText(name);
        });
}

// ============================================================================
// Play
// ============================================================================

void MainComponent::play()
{
    if (!currentSong)
        return;

    if (!audioReady)
        return;

    synthEngine.start();

    isPlaying =
        synthEngine.isPlaying();

    if (isPlaying)
    {
        playButton.setButtonText(
            "▶  Playing");

        playButton.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour::fromRGB(
                0,
                164,
                235));
    }
}

// ============================================================================
// Stop
// ============================================================================

void MainComponent::stop()
{
    synthEngine.stop();

    isPlaying = false;

    playButton.setButtonText(
        "▶  Play");

    playButton.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour::fromRGB(
            0,
            164,
            235));
}

// ============================================================================
// Song display
// ============================================================================

void MainComponent::updateSongDisplay()
{
    if (!currentSong ||
        !currentMidiFile.existsAsFile())
    {
        return;
    }

    auto name =
        currentMidiFile.getFileNameWithoutExtension();

    songTitle.setText(
        name,
        juce::dontSendNotification);

    nextSong.setText(
        "Next: -",
        juce::dontSendNotification);

    updateTransportDisplay();
}

// ============================================================================
// Transport display
// ============================================================================

void MainComponent::updateTransportDisplay()
{
    if (!currentSong)
    {
        elapsedTime.setText(
            "00:00",
            juce::dontSendNotification);

        totalTime.setText(
            "00:00",
            juce::dontSendNotification);

        positionSlider.setValue(
            0.0,
            juce::dontSendNotification);

        return;
    }

    const auto lengthSamples =
        synthEngine.lengthSamples();

    const auto positionSamples =
        synthEngine.positionSamples();

    const double sampleRate =
        audioSampleRate > 0.0
            ? audioSampleRate
            : 48000.0;

    const double positionSeconds =
        static_cast<double>(
            positionSamples)
        / sampleRate;

    const double lengthSeconds =
        static_cast<double>(
            lengthSamples)
        / sampleRate;

    elapsedTime.setText(
        formatTime(positionSeconds),
        juce::dontSendNotification);

    totalTime.setText(
        formatTime(lengthSeconds),
        juce::dontSendNotification);

    const double fraction =
        lengthSamples > 0
            ? static_cast<double>(
                  positionSamples)
              / static_cast<double>(
                  lengthSamples)
            : 0.0;

    positionSlider.setValue(
        juce::jlimit(
            0.0,
            1.0,
            fraction),
        juce::dontSendNotification);

    const bool enginePlaying =
        synthEngine.isPlaying();

    if (!enginePlaying && isPlaying)
    {
        isPlaying = false;

        playButton.setButtonText(
            "▶  Play");

        playButton.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour::fromRGB(
                0,
                164,
                235));
    }
}



// ============================================================================
// Time formatting
// ============================================================================

juce::String MainComponent::formatTime(
    double seconds)
{
    if (!std::isfinite(seconds) ||
        seconds < 0.0)
    {
        seconds = 0.0;
    }

    const auto totalSeconds =
        static_cast<int>(
            std::floor(seconds));

    const int minutes =
        totalSeconds / 60;

    const int remainingSeconds =
        totalSeconds % 60;

    return juce::String(minutes)
        + ":"
        + juce::String(
              remainingSeconds)
              .paddedLeft('0', 2);
}