#include "MainComponent.h"

#include <algorithm>

// ============================================================================
// AudioAppComponent
// ============================================================================

void MainComponent::prepareToPlay(
    int,
    double sampleRate)
{
    audioSampleRate = sampleRate;

    synthEngine.setSampleRate(
        sampleRate);

    audioReady = true;

    if (currentSong)
    {
        synthEngine.loadSong(
            currentSong);
    }
}

// ============================================================================

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr ||
        bufferToFill.numSamples <= 0)
    {
        return;
    }

    const int numChannels =
        bufferToFill.buffer->getNumChannels();

    for (int channel = 0;
         channel < numChannels;
         ++channel)
    {
        bufferToFill.buffer->clear(
            channel,
            bufferToFill.startSample,
            bufferToFill.numSamples);
    }

    if (!audioReady ||
        !currentSong ||
        !isPlaying)
    {
        return;
    }

    const int channelsToRender =
        std::min(
            numChannels,
            2);

    if (channelsToRender <= 0)
        return;

    float* output[2] =
    {
        nullptr,
        nullptr
    };

    output[0] =
        bufferToFill.buffer->getWritePointer(
            0,
            bufferToFill.startSample);

    if (channelsToRender >= 2)
    {
        output[1] =
            bufferToFill.buffer->getWritePointer(
                1,
                bufferToFill.startSample);
    }
    else
    {
        output[1] = output[0];
    }

    synthEngine.render(
        output,
        channelsToRender,
        bufferToFill.numSamples);

    if (!synthEngine.isPlaying())
    {
        isPlaying = false;
    }
}

// ============================================================================

void MainComponent::releaseResources()
{
    audioReady = false;
}