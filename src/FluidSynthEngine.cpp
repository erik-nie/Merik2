#include "FluidSynthEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
void checkPointer(const void* pointer, const char* message)
{
    if (!pointer)
        throw std::runtime_error(message);
}
}

MidiChannelState FluidSynthEngine::getChannelState(int channel) const
{
    const std::scoped_lock lock(mutex_);

    return midiTransformer_.getChannelState(channel);
}

FluidSynthEngine::FluidSynthEngine()
{
    createSynth();
}

FluidSynthEngine::~FluidSynthEngine()
{
    std::scoped_lock lock(mutex_);

    destroySynth();
}

void FluidSynthEngine::createSynth()
{
    if (settings_ || synth_)
        return;

    settings_ = new_fluid_settings();
    checkPointer(
        settings_,
        "FluidSynth: new_fluid_settings() failed");

    if (fluid_settings_setnum(
            settings_,
            "synth.sample-rate",
            sampleRate_) != FLUID_OK)
    {
        destroySynth();
        throw std::runtime_error(
            "FluidSynth: could not set sample rate");
    }

    synth_ = new_fluid_synth(settings_);

    if (!synth_)
    {
        destroySynth();

        throw std::runtime_error(
            "FluidSynth: new_fluid_synth() failed");
    }

    fluid_synth_set_polyphony(synth_, 256);
    fluid_synth_set_gain(synth_, 0.5);
}

void FluidSynthEngine::destroySynth()
{
    if (synth_)
    {
        delete_fluid_synth(synth_);
        synth_ = nullptr;
    }

    if (settings_)
    {
        delete_fluid_settings(settings_);
        settings_ = nullptr;
    }

    soundFontId_ = -1;
}

void FluidSynthEngine::setSampleRate(double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    std::scoped_lock lock(mutex_);

    sampleRate_ = sampleRate;

    if (settings_)
    {
        fluid_settings_setnum(
            settings_,
            "synth.sample-rate",
            sampleRate_);
    }

    if (synth_)
        fluid_synth_set_sample_rate(
            synth_,
            sampleRate_);
}

bool FluidSynthEngine::loadSoundFont(
    const std::string& path,
    std::string& error)
{
    std::scoped_lock lock(mutex_);

    error.clear();

    if (path.empty())
    {
        error = "No SoundFont selected.";
        return false;
    }

    if (!synth_)
    {
        try
        {
            createSynth();
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    const int newSoundFont =
        fluid_synth_sfload(
            synth_,
            path.c_str(),
            1);

    if (newSoundFont < 0)
    {
        error =
            "FluidSynth could not load SoundFont:\n" +
            path;

        return false;
    }

    if (soundFontId_ >= 0)
    {
        fluid_synth_sfunload(
            synth_,
            soundFontId_,
            1);
    }

    soundFontId_ = newSoundFont;
    soundFontPath_ = path;

    resetSynth();

    return true;
}

void FluidSynthEngine::loadSong(
    std::shared_ptr<const Song> song)
{
    if (!song)
        return;

    std::scoped_lock lock(mutex_);

    song_ = std::move(song);

    currentSample_ = 0;
    nextEvent_ = 0;
    playing_ = false;

    resetSynth();
}

void FluidSynthEngine::start()
{
    std::scoped_lock lock(mutex_);

    if (!song_)
        return;

    if (currentSample_ >= lengthSamples())
    {
        currentSample_ = 0;
        nextEvent_ = 0;
        resetSynth();
    }

    playing_ = true;
}

void FluidSynthEngine::stop()
{
    std::scoped_lock lock(mutex_);

    playing_ = false;
}

void FluidSynthEngine::seekSamples(
    std::int64_t samplePosition)
{
    std::scoped_lock lock(mutex_);

    if (!song_)
        return;

    samplePosition =
        std::clamp<std::int64_t>(
            samplePosition,
            0,
            lengthSamples());

    playing_ = false;

    currentSample_ = samplePosition;

    resetSynth();

    nextEvent_ = static_cast<std::size_t>(
        std::lower_bound(
            song_->playbackEvents.begin(),
            song_->playbackEvents.end(),
            samplePosition,
            [this](const RawMidiEvent& event,
                    std::int64_t position)
            {
                return eventSamplePosition(event)
                       < position;
            })
        - song_->playbackEvents.begin());

    /*
        For now we deliberately do not reconstruct the complete
        synth envelope state before a seek.

        That will be added when the transport seek bar is implemented.
        Starting from zero is fully sample accurate.
    */
}

bool FluidSynthEngine::isPlaying() const
{
    std::scoped_lock lock(mutex_);

    return playing_;
}

std::int64_t FluidSynthEngine::positionSamples() const
{
    std::scoped_lock lock(mutex_);

    return currentSample_;
}

std::int64_t FluidSynthEngine::lengthSamples() const
{
    if (!song_ || song_->playbackEvents.empty())
        return 0;

    const auto last =
        song_->playbackEvents.back();

    return eventSamplePosition(last);
}

std::int64_t FluidSynthEngine::eventSamplePosition(
    const RawMidiEvent& event) const
{
    if (!song_)
        return event.samplePosition;

    if (song_->sampleRate <= 0.0 ||
        std::abs(song_->sampleRate - sampleRate_) < 0.001)
    {
        return event.samplePosition;
    }

    return static_cast<std::int64_t>(
        std::llround(
            static_cast<double>(event.samplePosition)
            * sampleRate_
            / song_->sampleRate));
}

void FluidSynthEngine::resetSynth()
{
    midiTransformer_.reset();
    
    if (!synth_)
        return;

    fluid_synth_system_reset(synth_);
}

void FluidSynthEngine::sendMidiEvent(
    const RawMidiEvent& event)
{
    if (!synth_ || event.bytes.empty())
        return;

    // Every MIDI event goes through the transformer first.
    //
    // Program Changes update the channel/family state.
    // CC7 and CC11 are transformed according to the
    // current family volume factor.
    const RawMidiEvent transformedEvent =
        midiTransformer_.transform(event);

    const auto& bytes = transformedEvent.bytes;

    if (bytes.empty())
        return;

    const auto status = bytes[0];

    /*
        Meta events and system events aren't sent to
        FluidSynth in this first engine implementation.
    */
    if (status >= 0xF0)
        return;

    if (bytes.size() < 2)
        return;

    const int channel =
        static_cast<int>(status & 0x0F);

    const int command =
        status & 0xF0;

    const int data1 =
        std::clamp(
            static_cast<int>(bytes[1]),
            0,
            127);

    switch (command)
    {
        case 0x80:
        {
            if (bytes.size() >= 3)
            {
                fluid_synth_noteoff(
                    synth_,
                    channel,
                    data1);
            }

            break;
        }

        case 0x90:
        {
            if (bytes.size() >= 3)
            {
                const int velocity =
                    std::clamp(
                        static_cast<int>(bytes[2]),
                        0,
                        127);

                fluid_synth_noteon(
                    synth_,
                    channel,
                    data1,
                    velocity);
            }

            break;
        }

        case 0xA0:
        {
            if (bytes.size() >= 3)
            {
                const int value =
                    std::clamp(
                        static_cast<int>(bytes[2]),
                        0,
                        127);

                fluid_synth_key_pressure(
                    synth_,
                    channel,
                    data1,
                    value);
            }

            break;
        }

        case 0xB0:
        {
            if (bytes.size() >= 3)
            {
                const int value =
                    std::clamp(
                        static_cast<int>(bytes[2]),
                        0,
                        127);

                fluid_synth_cc(
                    synth_,
                    channel,
                    data1,
                    value);
            }

            break;
        }

        case 0xC0:
        {
            fluid_synth_program_change(
                synth_,
                channel,
                data1);

            break;
        }

        case 0xD0:
        {
            fluid_synth_channel_pressure(
                synth_,
                channel,
                data1);

            break;
        }

        case 0xE0:
        {
            if (bytes.size() >= 3)
            {
                const int lsb =
                    std::clamp(
                        static_cast<int>(bytes[1]),
                        0,
                        127);

                const int msb =
                    std::clamp(
                        static_cast<int>(bytes[2]),
                        0,
                        127);

                const int bend =
                    lsb | (msb << 7);

                fluid_synth_pitch_bend(
                    synth_,
                    channel,
                    bend);
            }

            break;
        }

        default:
            break;
    }
}

void FluidSynthEngine::render(
    float** output,
    int numChannels,
    int numSamples)
{
    if (!output || numChannels <= 0 || numSamples <= 0)
        return;

    std::scoped_lock lock(mutex_);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (output[channel])
            std::fill(
                output[channel],
                output[channel] + numSamples,
                0.0f);
    }

    if (!synth_ || !song_ || !playing_)
        return;

    int rendered = 0;

    while (rendered < numSamples)
    {
        const auto blockPosition =
            currentSample_ + rendered;

        while (
            nextEvent_ < song_->playbackEvents.size() &&
            eventSamplePosition(
                song_->playbackEvents[nextEvent_])
                <= blockPosition)
        {
            sendMidiEvent(
                song_->playbackEvents[nextEvent_]);

            ++nextEvent_;
        }

        std::int64_t samplesUntilEvent =
            static_cast<std::int64_t>(numSamples - rendered);

        if (nextEvent_ < song_->playbackEvents.size())
        {
            const auto nextEventSample =
                eventSamplePosition(
                    song_->playbackEvents[nextEvent_]);

            if (nextEventSample > blockPosition)
            {
                samplesUntilEvent =
                    std::min(
                        samplesUntilEvent,
                        nextEventSample - blockPosition);
            }
        }

        const int chunk =
            static_cast<int>(
                std::max<std::int64_t>(
                    1,
                    samplesUntilEvent));

        if (numChannels >= 2)
        {
            fluid_synth_write_float(
                synth_,
                chunk,

                output[0],
                rendered,
                1,

                output[1],
                rendered,
                1);
        }
        else
        {
            fluid_synth_write_float(
                synth_,
                chunk,

                output[0],
                rendered,
                1,

                output[0],
                rendered,
                1);
        }

        rendered += chunk;
    }

    currentSample_ += numSamples;

    if (currentSample_ >= lengthSamples())
    {
        currentSample_ = lengthSamples();
        playing_ = false;
    }
}

std::string FluidSynthEngine::soundFontPath() const
{
    std::scoped_lock lock(mutex_);

    return soundFontPath_;
}