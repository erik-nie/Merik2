#include "MainComponent.h"

#include "ClickSettings.h"

#include <algorithm>

// ============================================================================
// AudioAppComponent
// ============================================================================

void MainComponent::prepareToPlay(
    int samplesPerBlockExpected,
    double sampleRate)
{
    audioSampleRate = sampleRate;

    synthEngine.setSampleRate(
        sampleRate);

    const auto size = static_cast<std::size_t>(std::max(samplesPerBlockExpected, 8192));
    clickBuffer.assign(size, 0.0f);
    mainBuffer[0].assign(size, 0.0f);
    mainBuffer[1].assign(size, 0.0f);

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

    const int numSamples = bufferToFill.numSamples;
    const auto count = static_cast<std::size_t>(numSamples);

    if (count > clickBuffer.size())   // zelden: groter blok dan verwacht
    {
        clickBuffer.resize(count);
        mainBuffer[0].resize(count);
        mainBuffer[1].resize(count);
    }

    float* mix[2] = { mainBuffer[0].data(), mainBuffer[1].data() };

    synthEngine.render(
        mix,
        2,
        numSamples,
        clickBuffer.data());

    // Stems die naar de hoofduitgang gaan horen bij de hoofdmix (ook voor "split").
    for (int family = 0; family < 7; ++family)
    {
        if (stemRoutes[static_cast<std::size_t>(family)].load() != 0)
            continue;

        if (const float* left = synthEngine.stemBuffer(family, 0))
        {
            juce::FloatVectorOperations::add(mix[0], left, numSamples);
            juce::FloatVectorOperations::add(mix[1], synthEngine.stemBuffer(family, 1), numSamples);
        }
    }

    // Hoofdmix naar het gekozen paar of kanaal.
    int mainRouteValue = mainRoute.load();
    int mainA = 0;
    int mainB = numChannels >= 2 ? 1 : -1;

    if (mainRouteValue >= ClickRoute::pairBase)
    {
        const int first = mainRouteValue - ClickRoute::pairBase;

        if (first + 1 < numChannels)
        {
            mainA = first;
            mainB = first + 1;
        }
    }
    else if (mainRouteValue >= ClickRoute::monoBase)
    {
        const int channel = mainRouteValue - ClickRoute::monoBase;

        if (channel < numChannels)
        {
            mainA = channel;
            mainB = -1;
        }
    }

    const auto channelPointer = [&](int channel)
    {
        return bufferToFill.buffer->getWritePointer(channel, bufferToFill.startSample);
    };

    const auto addMainPair = [&](const float* left, const float* right)
    {
        if (mainB >= 0)
        {
            juce::FloatVectorOperations::add(channelPointer(mainA), left, numSamples);
            juce::FloatVectorOperations::add(channelPointer(mainB), right, numSamples);
        }
        else
        {
            float* target = channelPointer(mainA);

            for (int i = 0; i < numSamples; ++i)
                target[i] += 0.5f * (left[i] + right[i]);
        }
    };

    if (mainB >= 0)
    {
        juce::FloatVectorOperations::copy(channelPointer(mainA), mix[0], numSamples);
        juce::FloatVectorOperations::copy(channelPointer(mainB), mix[1], numSamples);
    }
    else
    {
        float* target = channelPointer(mainA);

        for (int i = 0; i < numSamples; ++i)
            target[i] = 0.5f * (mix[0][i] + mix[1][i]);
    }

    // Stems met een eigen uitgang: stereo-paar of mono (links + rechts samen).
    for (int family = 0; family < 7; ++family)
    {
        const int stemRoute = stemRoutes[static_cast<std::size_t>(family)].load();
        const float* left = synthEngine.stemBuffer(family, 0);

        if (stemRoute == 0 || left == nullptr)
            continue;

        const float* right = synthEngine.stemBuffer(family, 1);

        if (stemRoute >= ClickRoute::pairBase && stemRoute - ClickRoute::pairBase + 1 < numChannels)
        {
            const int first = stemRoute - ClickRoute::pairBase;
            juce::FloatVectorOperations::add(channelPointer(first), left, numSamples);
            juce::FloatVectorOperations::add(channelPointer(first + 1), right, numSamples);
        }
        else if (stemRoute >= ClickRoute::monoBase && stemRoute < ClickRoute::pairBase
                 && stemRoute - ClickRoute::monoBase < numChannels)
        {
            float* target = channelPointer(stemRoute - ClickRoute::monoBase);

            for (int i = 0; i < numSamples; ++i)
                target[i] += 0.5f * (left[i] + right[i]);
        }
        else
        {
            // Uitgang bestaat niet (meer): terug naar de hoofduitgang.
            addMainPair(left, right);
        }
    }

    // Klik naar de gekozen uitgang(en).
    const int route = clickRoute.load();

    const auto addClick = [&](int channel)
    {
        if (channel >= 0 && channel < numChannels)
            juce::FloatVectorOperations::add(channelPointer(channel), clickBuffer.data(), numSamples);
    };

    const auto addToMain = [&]
    {
        addClick(mainA);
        addClick(mainB);
    };

    if (route == ClickRoute::split)
    {
        if (mainB >= 0)
        {
            // Klik links, muziek (mono) rechts.
            float* left = channelPointer(mainA);
            float* right = channelPointer(mainB);

            for (int i = 0; i < numSamples; ++i)
            {
                right[i] = 0.5f * (mix[0][i] + mix[1][i]);
                left[i] = clickBuffer[static_cast<std::size_t>(i)];
            }
        }
        else
        {
            addToMain();
        }
    }
    else if (route >= ClickRoute::pairBase)
    {
        const int first = route - ClickRoute::pairBase;

        if (first + 1 < numChannels)
        {
            addClick(first);
            addClick(first + 1);
        }
        else
        {
            addToMain();
        }
    }
    else if (route >= ClickRoute::monoBase)
    {
        const int channel = route - ClickRoute::monoBase;

        if (channel < numChannels)
            addClick(channel);
        else
            addToMain();
    }
    else if (route == ClickRoute::automatic)
    {
        // Eerste paar dat niet de hoofdmix is; anders in de mix.
        int pair = -1;

        for (int first = 0; first + 1 < numChannels; first += 2)
            if (first != mainA && first + 1 != mainA && first != mainB && first + 1 != mainB)
            {
                pair = first;
                break;
            }

        if (pair >= 0)
        {
            addClick(pair);
            addClick(pair + 1);
        }
        else
        {
            addToMain();
        }
    }
    else
    {
        addToMain();
    }

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