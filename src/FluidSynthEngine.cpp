#include "FluidSynthEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace
{
    const char* getFamilyName(int family)
    {
        static constexpr const char* names[] = {
            "Drums",
            "Bass",
            "Guitars",
            "Keys",
            "Strings",
            "Winds",
            "FX",
            "Other"
        };

        if (family < 0 || family >= 8)
            return "Unknown";

        return names[family];
    }

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

int FluidSynthEngine::getAdjustedChannelVolume(int channel) const
{
    const std::scoped_lock lock(mutex_);
    return midiTransformer_.getAdjustedChannelVolume(channel);
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


FamilySettings FluidSynthEngine::getFamilySettings(int family) const
{
    std::scoped_lock lock(mutex_);

    if (family < 0 || family >= familyCount)
        return FamilySettings { 0, false };

    return familySettings_[static_cast<std::size_t>(family)];
}

std::array<FamilySettings, FluidSynthEngine::familyCount>
FluidSynthEngine::getAllFamilySettings() const
{
    std::scoped_lock lock(mutex_);
    return familySettings_;
}
void FluidSynthEngine::setFamilyVolume(int family, int volume)
{
    if (family < 0 || family >= familyCount)
    {
        std::clog << "[Mixer] Ongeldige family: "
                  << family << '\n';
        return;
    }

    std::scoped_lock lock(mutex_);

    auto& settings = familySettings_[static_cast<std::size_t>(family)];

    const int oldVolume = settings.volume;
    settings.volume = std::clamp(volume, 0, 127);

    updateFamilyFactorLocked(family);
    applyFamilyVolumeLocked(family);

    const float factor = settings.enabled
        ? static_cast<float>(settings.volume) / 127.0f
        : 0.0f;

    std::clog
        << "[Mixer] " << getFamilyName(family)
        << ": volume " << oldVolume
        << " -> " << settings.volume
        << ", enabled=" << (settings.enabled ? "true" : "false")
        << ", factor=" << factor
        << '\n';
}

void FluidSynthEngine::setFamilyEnabled(int family, bool enabled)
{
    if (family < 0 || family >= familyCount)
    {
        std::clog << "[Mixer] Ongeldige family: "
                  << family << '\n';
        return;
    }

    std::scoped_lock lock(mutex_);

    auto& settings = familySettings_[static_cast<std::size_t>(family)];
    const bool oldEnabled = settings.enabled;

    settings.enabled = enabled;

    updateFamilyFactorLocked(family);
    applyFamilyVolumeLocked(family);

    const float factor = settings.enabled
        ? static_cast<float>(settings.volume) / 127.0f
        : 0.0f;

    std::clog
        << "[Mixer] " << getFamilyName(family)
        << ": enabled " << (oldEnabled ? "true" : "false")
        << " -> " << (settings.enabled ? "true" : "false")
        << ", volume=" << settings.volume
        << ", factor=" << factor
        << '\n';
}

void FluidSynthEngine::updateFamilyFactorLocked(int family)
{
    const auto& settings =
        familySettings_[static_cast<std::size_t>(family)];

    const float factor = settings.enabled
        ? static_cast<float>(settings.volume) / 127.0f
        : 0.0f;

    midiTransformer_.setFamilyVolumeFactor(family, factor);
}

void FluidSynthEngine::applyFamilyVolumeLocked(int family)
{
    if (!synth_)
    {
        std::clog
            << "[Mixer] " << getFamilyName(family)
            << ": geen FluidSynth-instantie beschikbaar\n";
        return;
    }

    int updatedChannels = 0;

    for (int channel = 0; channel < 16; ++channel)
    {
        const auto& state = midiTransformer_.getChannelState(channel);

        if (state.family != family)
            continue;

        const int volume =
            midiTransformer_.getAdjustedChannelVolume(channel);

        const int expression =
            midiTransformer_.getAdjustedChannelExpression(channel);

        fluid_synth_cc(synth_, channel, 7, volume);
        fluid_synth_cc(synth_, channel, 11, expression);

        ++updatedChannels;

        std::clog
            << "[Mixer] " << getFamilyName(family)
            << ": channel=" << channel + 1
            << ", CC7=" << volume
            << ", CC11=" << expression
            << '\n';
    }

    std::clog
        << "[Mixer] " << getFamilyName(family)
        << ": bijgewerkte kanalen=" << updatedChannels
        << '\n';
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
    std::scoped_lock lock(mutex_);

    if (sampleRate <= 0.0)
        return;

    sampleRate_ = sampleRate;

    if (synth_)
    {
        destroySynth();
        createSynth();

        if (!soundFontPath_.empty())
        {
            soundFontId_ = fluid_synth_sfload(
                synth_,
                soundFontPath_.c_str(),
                1);

            if (soundFontId_ < 0)
            {
                std::clog
                    << "[FluidSynth] SoundFont opnieuw laden mislukt: "
                    << soundFontPath_
                    << '\n';
            }
        }
    }
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

    const auto length = lengthSamples();

    samplePosition =
        std::clamp<std::int64_t>(
            samplePosition,
            0,
            length);

    const bool wasPlaying = playing_;

    playing_ = false;

    rebuildSynthStateAt(samplePosition);

    currentSample_ = samplePosition;

    /*
        Keep playback running when the user was already playing,
        except when seeking to the exact end of the song.
    */
    playing_ =
        wasPlaying &&
        samplePosition < length;
}


void FluidSynthEngine::rebuildSynthStateAt(
    std::int64_t samplePosition)
{
    if (!song_ || !synth_)
    {
        nextEvent_ = 0;
        return;
    }

    const auto& events =
        song_->playbackEvents;

    /*
        We reconstrueren alleen de MIDI-state die op het
        seekpunt geldig is.

        BELANGRIJK:
        We sturen historische Note On-events NIET opnieuw
        naar FluidSynth. Dat zou alle oude noten tegelijk
        opnieuw starten en kan de polyfonie overschrijden.
    */

    struct ChannelState
    {
        // Bank Select
        int bankMsb = 0;
        int bankLsb = 0;

        bool hasBankMsb = false;
        bool hasBankLsb = false;

        // Program Change
        int program = 0;
        bool hasProgram = false;

        // Controllers
        std::array<int, 128> controllerValue {};
        std::array<bool, 128> controllerValid {};

        // Pitch bend
        int pitchBend = 8192;
        bool hasPitchBend = false;

        // Channel pressure
        int channelPressure = 0;
        bool hasChannelPressure = false;

        /*
            Aantal actieve Note Ons per pitch.

            Een bool zou niet voldoende zijn omdat dezelfde
            noot meerdere keren overlappend kan voorkomen.
        */
        std::array<int, 128> activeNoteCount {};

        /*
            Laatste velocity van een actieve noot.
        */
        std::array<int, 128> activeNoteVelocity {};
    };

    std::array<ChannelState, 16> state {};

    std::size_t index = 0;

    // ------------------------------------------------------------------------
    // 1. Bepaal de MIDI-state op het seekpunt
    // ------------------------------------------------------------------------

    while (index < events.size())
    {
        const auto eventPosition =
            eventSamplePosition(
                events[index]);

        if (eventPosition >= samplePosition)
            break;

        const auto& event =
            events[index];

        if (event.bytes.empty())
        {
            ++index;
            continue;
        }

        const auto status =
            event.bytes[0];

        // Meta/system events
        if (status >= 0xF0)
        {
            ++index;
            continue;
        }

        const int channel =
            static_cast<int>(
                status & 0x0F);

        if (channel < 0 || channel >= 16)
        {
            ++index;
            continue;
        }

        auto& channelState =
            state[static_cast<std::size_t>(channel)];

        const int command =
            status & 0xF0;

        // --------------------------------------------------------------------
        // Note Off
        // --------------------------------------------------------------------

        if (command == 0x80 &&
            event.bytes.size() >= 3)
        {
            const int note =
                static_cast<int>(
                    event.bytes[1] & 0x7F);

            if (channelState.activeNoteCount[note] > 0)
            {
                --channelState.activeNoteCount[note];
            }
        }

        // --------------------------------------------------------------------
        // Note On
        // --------------------------------------------------------------------

        else if (command == 0x90 &&
                 event.bytes.size() >= 3)
        {
            const int note =
                static_cast<int>(
                    event.bytes[1] & 0x7F);

            const int velocity =
                static_cast<int>(
                    event.bytes[2] & 0x7F);

            /*
                MIDI Note On velocity 0 betekent Note Off.
            */
            if (velocity == 0)
            {
                if (channelState.activeNoteCount[note] > 0)
                {
                    --channelState.activeNoteCount[note];
                }
            }
            else
            {
                ++channelState.activeNoteCount[note];

                channelState.activeNoteVelocity[note] =
                    velocity;
            }
        }

        // --------------------------------------------------------------------
        // Control Change
        // --------------------------------------------------------------------

        else if (command == 0xB0 &&
                 event.bytes.size() >= 3)
        {
            const int controller =
                static_cast<int>(
                    event.bytes[1] & 0x7F);

            const int value =
                static_cast<int>(
                    event.bytes[2] & 0x7F);

            channelState.controllerValue[controller] =
                value;

            channelState.controllerValid[controller] =
                true;

            if (controller == 0)
            {
                channelState.bankMsb =
                    value;

                channelState.hasBankMsb =
                    true;
            }
            else if (controller == 32)
            {
                channelState.bankLsb =
                    value;

                channelState.hasBankLsb =
                    true;
            }
        }

        // --------------------------------------------------------------------
        // Program Change
        // --------------------------------------------------------------------

        else if (command == 0xC0 &&
                 event.bytes.size() >= 2)
        {
            channelState.program =
                static_cast<int>(
                    event.bytes[1] & 0x7F);

            channelState.hasProgram =
                true;
        }

        // --------------------------------------------------------------------
        // Channel Pressure
        // --------------------------------------------------------------------

        else if (command == 0xD0 &&
                 event.bytes.size() >= 2)
        {
            channelState.channelPressure =
                static_cast<int>(
                    event.bytes[1] & 0x7F);

            channelState.hasChannelPressure =
                true;
        }

        // --------------------------------------------------------------------
        // Pitch Bend
        // --------------------------------------------------------------------

        else if (command == 0xE0 &&
                 event.bytes.size() >= 3)
        {
            const int lsb =
                static_cast<int>(
                    event.bytes[1] & 0x7F);

            const int msb =
                static_cast<int>(
                    event.bytes[2] & 0x7F);

            channelState.pitchBend =
                lsb | (msb << 7);

            channelState.hasPitchBend =
                true;
        }

        ++index;
    }

    /*
        index is nu het eerste event op of na het seekpunt.
        Dat event moet door de normale render-loop worden
        uitgevoerd.
    */
    nextEvent_ = index;

    // ------------------------------------------------------------------------
    // 2. Synth resetten
    // ------------------------------------------------------------------------

    resetSynth();

    // ------------------------------------------------------------------------
    // 3. MIDI-state herstellen
    //
    // Volgorde is belangrijk:
    //
    //   Bank Select
    //   Program Change
    //   Controllers
    //   Pitch Bend
    //   Channel Pressure
    //   actieve noten
    //
    // Vooral Program Change moet vóór CC7/CC11 komen omdat
    // MidiTransformer daarna weet bij welke family het kanaal hoort.
    // ------------------------------------------------------------------------

    for (int channel = 0;
         channel < 16;
         ++channel)
    {
        auto& channelState =
            state[static_cast<std::size_t>(channel)];

        // ------------------------------------------------------------
        // Bank Select MSB
        // ------------------------------------------------------------

        if (channelState.hasBankMsb)
        {
            RawMidiEvent event;

            event.bytes =
            {
                static_cast<std::uint8_t>(
                    0xB0 | channel),
                0,
                static_cast<std::uint8_t>(
                    channelState.bankMsb)
            };

            sendMidiEvent(event);
        }

        // ------------------------------------------------------------
        // Bank Select LSB
        // ------------------------------------------------------------

        if (channelState.hasBankLsb)
        {
            RawMidiEvent event;

            event.bytes =
            {
                static_cast<std::uint8_t>(
                    0xB0 | channel),
                32,
                static_cast<std::uint8_t>(
                    channelState.bankLsb)
            };

            sendMidiEvent(event);
        }

        // ------------------------------------------------------------
        // Program Change
        // ------------------------------------------------------------

        if (channelState.hasProgram)
        {
            RawMidiEvent event;

            event.bytes =
            {
                static_cast<std::uint8_t>(
                    0xC0 | channel),
                static_cast<std::uint8_t>(
                    channelState.program)
            };

            sendMidiEvent(event);
        }

        // ------------------------------------------------------------
        // Controllers
        //
        // CC7 en CC11 moeten pas ná Program Change komen zodat
        // MidiTransformer de juiste family kent.
        // ------------------------------------------------------------

        for (int controller = 0;
             controller < 128;
             ++controller)
        {
            if (!channelState.controllerValid[controller])
                continue;

            RawMidiEvent event;

            event.bytes =
            {
                static_cast<std::uint8_t>(
                    0xB0 | channel),
                static_cast<std::uint8_t>(
                    controller),
                static_cast<std::uint8_t>(
                    channelState.controllerValue[controller])
            };

            sendMidiEvent(event);
        }

        // ------------------------------------------------------------
        // Pitch Bend
        // ------------------------------------------------------------

        if (channelState.hasPitchBend)
        {
            RawMidiEvent event;

            event.bytes =
            {
                static_cast<std::uint8_t>(
                    0xE0 | channel),
                static_cast<std::uint8_t>(
                    channelState.pitchBend & 0x7F),
                static_cast<std::uint8_t>(
                    (channelState.pitchBend >> 7) & 0x7F)
            };

            sendMidiEvent(event);
        }

        // ------------------------------------------------------------
        // Channel Pressure
        // ------------------------------------------------------------

        if (channelState.hasChannelPressure)
        {
            RawMidiEvent event;

            event.bytes =
            {
                static_cast<std::uint8_t>(
                    0xD0 | channel),
                static_cast<std::uint8_t>(
                    channelState.channelPressure)
            };

            sendMidiEvent(event);
        }
    }

    // ------------------------------------------------------------------------
    // 4. Alleen noten die op het seekpunt nog actief zijn opnieuw
    //    aanslaan.
    //
    //    We doen dit pas nadat alle programma's/controllers zijn
    //    hersteld.
    // ------------------------------------------------------------------------

    for (int channel = 0;
         channel < 16;
         ++channel)
    {
        const auto& channelState =
            state[static_cast<std::size_t>(channel)];

        for (int note = 0;
             note < 128;
             ++note)
        {
            if (channelState.activeNoteCount[note] <= 0)
                continue;

            const int velocity =
                std::clamp(
                    channelState.activeNoteVelocity[note],
                    1,
                    127);

            /*
                Start één voice per actieve noot.

                Bij overlappende identieke Note Ons starten we
                bewust maar één voice. Dat voorkomt dat een seek
                een enorme hoeveelheid polyfonie consumeert.
            */
            fluid_synth_noteon(
                synth_,
                channel,
                note,
                velocity);
        }
    }
}

void FluidSynthEngine::setFamilyVolumeFactor(int family, float factor)
{
    if (family < 0 || family >= familyCount)
        return;

    if (!std::isfinite(factor))
        factor = 0.0f;

    factor = std::clamp(factor, 0.0f, 1.0f);

    std::scoped_lock lock(mutex_);

    familySettings_[static_cast<std::size_t>(family)].volume =
        static_cast<int>(std::lround(factor * 127.0f));

    updateFamilyFactorLocked(family);
    applyFamilyVolumeLocked(family);
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

    const auto lastEventSample =
        eventSamplePosition(song_->playbackEvents.back());

    constexpr double tailSeconds = 2.0;

    const auto tailSamples =
        static_cast<std::int64_t>(
            std::llround(sampleRate_ * tailSeconds));

    return lastEventSample + tailSamples;
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
    if (!synth_)
        return;

    fluid_synth_system_reset(synth_);

    constexpr int bufferSize = 512;
    constexpr double flushSeconds = 2.0;

    float left[bufferSize];
    float right[bufferSize];

    auto remaining = static_cast<int>(
        std::ceil(sampleRate_ * flushSeconds));

    while (remaining > 0)
    {
        const int count = std::min(
            remaining,
            bufferSize);

        fluid_synth_write_float(
            synth_,
            count,
            left,
            0,
            1,
            right,
            0,
            1);

        remaining -= count;
    }
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