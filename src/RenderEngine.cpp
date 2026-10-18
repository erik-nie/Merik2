#include "RenderEngine.h"

RenderEngine::RenderEngine(juce::AudioDeviceManager& deviceManager)
    : manager(deviceManager)
{
}

RenderEngine::~RenderEngine()
{
    stop();
}

juce::StringArray RenderEngine::inputDeviceNames(juce::AudioDeviceManager& manager)
{
    if (auto* type = manager.getCurrentDeviceTypeObject())
        return type->getDeviceNames(true);

    return {};
}

juce::StringArray RenderEngine::availableInputChannels(juce::AudioDeviceManager& manager,
                                                       const juce::String& preferredDevice,
                                                       juce::String* usedDevice)
{
    auto* type = manager.getCurrentDeviceTypeObject();
    const auto setup = manager.getAudioDeviceSetup();

    if (type == nullptr)
        return {};

    // Staat het gevraagde apparaat al met ingangen open, dan zijn dat de namen.
    if (auto* current = manager.getCurrentAudioDevice())
    {
        const auto openInput = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : setup.outputDeviceName;
        const auto names = current->getInputChannelNames();

        if (!names.isEmpty() && (preferredDevice.isEmpty() || preferredDevice == openInput))
        {
            if (usedDevice != nullptr)
                *usedDevice = openInput;

            return names;
        }
    }

    // Anders: een (niet geopend) apparaat met alleen de ingang aanmaken. Een interface met
    // in- en uitgangen heeft dezelfde naam als de uitgang; bij systemen met aparte lijsten
    // zoeken we een ingangsapparaat waarvan de naam erbij past.
    if (preferredDevice.isEmpty() && setup.outputDeviceName.isEmpty())
        return {};

    static juce::String cachedKey;
    static juce::StringArray cachedNames;
    static juce::String cachedDevice;

    const auto key = type->getTypeName() + "|" + setup.outputDeviceName + "|" + preferredDevice;

    if (key != cachedKey)
    {
        cachedKey = key;
        cachedNames.clear();
        cachedDevice.clear();

        juce::StringArray candidates;

        if (preferredDevice.isNotEmpty())
        {
            candidates.add(preferredDevice);
        }
        else
        {
            candidates.add(setup.outputDeviceName);

            for (const auto& name : type->getDeviceNames(true))
                if (name != setup.outputDeviceName
                    && (name.containsIgnoreCase(setup.outputDeviceName) || setup.outputDeviceName.containsIgnoreCase(name)))
                    candidates.add(name);
        }

        for (const auto& candidate : candidates)
        {
            std::unique_ptr<juce::AudioIODevice> device(type->createDevice({}, candidate));

            if (device != nullptr && !device->getInputChannelNames().isEmpty())
            {
                cachedNames = device->getInputChannelNames();
                cachedDevice = candidate;
                break;
            }
        }
    }

    if (usedDevice != nullptr)
        *usedDevice = cachedDevice;

    return cachedNames;
}

bool RenderEngine::start(const juce::String& midiOutIdentifier, const juce::String& preferredInput, int inputRoute, juce::String& error)
{
    if (running)
        return true;

    auto* device = manager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        error = "There is no audio device. Choose one in Settings > Audio.";
        return false;
    }

    juce::String inputDevice;
    const auto names = availableInputChannels(manager, preferredInput, &inputDevice);

    if (names.isEmpty())
    {
        error = "There are no inputs on \"" + (preferredInput.isNotEmpty() ? preferredInput : device->getName())
              + "\". Choose another audio input device.";
        return false;
    }

    // Welke ingangskanalen.
    int first = -1;
    int count = 1;

    if (inputRoute >= pairBase)
    {
        first = inputRoute - pairBase;
        count = 2;
    }
    else if (inputRoute >= monoBase)
    {
        first = inputRoute - monoBase;
    }

    if (first < 0 || first + count > names.size())
    {
        error = "Choose the input channels (Settings > Tools).";
        return false;
    }

    if (!sink.open(midiOutIdentifier))
    {
        error = "Could not open the MIDI output.";
        return false;
    }

    // Het audioapparaat met precies deze ingangen aan; daarna terugzetten.
    savedSetup = manager.getAudioDeviceSetup();
    auto setup = savedSetup;
    setup.inputDeviceName = inputDevice;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange(first, count, true);

    const auto problem = manager.setAudioDeviceSetup(setup, true);
    setupChanged = true;

    if (problem.isNotEmpty())
    {
        error = "Could not open the audio input: " + problem;
        manager.setAudioDeviceSetup(savedSetup, true);
        setupChanged = false;
        sink.close();
        return false;
    }

    device = manager.getCurrentAudioDevice();

    if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() < count)
    {
        error = "The audio input could not be opened. On macOS, allow Merik to use the microphone "
                "(System Settings > Privacy & Security > Microphone).";
        manager.setAudioDeviceSetup(savedSetup, true);
        setupChanged = false;
        sink.close();
        return false;
    }

    channelCount = count;
    rate = device->getCurrentSampleRate();

    manager.addAudioCallback(this);
    running = true;
    return true;
}

void RenderEngine::stop()
{
    if (!running && !setupChanged)
        return;

    manager.removeAudioCallback(this);
    capture.reset();
    sink.allNotesOff(-1);
    sink.close();
    running = false;

    if (setupChanged)
    {
        manager.setAudioDeviceSetup(savedSetup, true);
        setupChanged = false;
    }
}

void RenderEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
        rate = device->getCurrentSampleRate();
}

void RenderEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOutputChannels; ++c)
        if (outputChannelData[c] != nullptr)
            std::fill(outputChannelData[c], outputChannelData[c] + numSamples, 0.0f);

    capture.process(inputChannelData, numInputChannels, numSamples, &sink);
}
