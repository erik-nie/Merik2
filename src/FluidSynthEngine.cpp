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
            "Others",
            "Click"
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
    for (auto& channel : soundingNote_)
        channel.fill(-1);

    clearExternalCache();

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
    applySynthSettingsLocked();
}

void FluidSynthEngine::applySynthSettingsLocked()
{
    if (synth_ == nullptr)
        return;

    const auto& v = synthSettings_;
    const auto unit = [](int value) { return std::clamp(value, 0, 100) / 100.0; };

    fluid_synth_set_gain(synth_, static_cast<float>(unit(v.gain)));

    fluid_synth_set_reverb_group_roomsize(synth_, -1, unit(v.reverbRoom));
    fluid_synth_set_reverb_group_damp(synth_, -1, unit(v.reverbDamp));
    fluid_synth_set_reverb_group_width(synth_, -1, std::clamp(v.reverbWidth, 0, 100));
    fluid_synth_set_reverb_group_level(synth_, -1, unit(v.reverbLevel));

    // Chorus staat altijd uit.
    fluid_synth_set_chorus_group_nr(synth_, -1, 0);
    fluid_synth_set_chorus_group_level(synth_, -1, 0.0);
    fluid_synth_chorus_on(synth_, -1, 0);
}

void FluidSynthEngine::setSynthSettings(const SynthSettings& settings)
{
    std::scoped_lock lock(mutex_);

    synthSettings_ = settings;
    applySynthSettingsLocked();
}

SynthSettings FluidSynthEngine::getSynthSettings() const
{
    std::scoped_lock lock(mutex_);
    return synthSettings_;
}

void FluidSynthEngine::applyDirtyFamiliesLocked()
{
    const auto dirty = dirtyFamilies_.load(std::memory_order_relaxed);

    if (dirty == 0)
        return;

    // Extern: hooguit eens per 30 ms (de laatste waarde blijft gewoon staan
    // en gaat bij de volgende gelegenheid mee).
    if (external_ != nullptr)
    {
        const auto now = std::chrono::steady_clock::now();

        if (now - lastFamilyApply_ < std::chrono::milliseconds(30))
            return;

        lastFamilyApply_ = now;
    }

    dirtyFamilies_.fetch_and(~dirty, std::memory_order_relaxed);

    for (int family = 0; family < familyCount; ++family)
        if ((dirty & (1u << family)) != 0)
            applyFamilyVolumeLocked(family);
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

    settings.volume = std::clamp(volume, 0, 127);

    updateFamilyFactorLocked(family);
    dirtyFamilies_.fetch_or(1u << family, std::memory_order_relaxed);
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
    settings.enabled = enabled;

    updateFamilyFactorLocked(family);
    dirtyFamilies_.fetch_or(1u << family, std::memory_order_relaxed);
}

void FluidSynthEngine::setChannelMuted(int channel, bool muted)
{
    if (channel < 0 || channel >= 16)
        return;

    std::scoped_lock lock(mutex_);

    channelMuted_[static_cast<std::size_t>(channel)] = muted;

    if (muted && synth_)
        outAllSoundsOff(channel);
}

void FluidSynthEngine::setTranspose(int semitones)
{
    std::scoped_lock lock(mutex_);

    transpose_ = std::clamp(semitones, -24, 24);
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
        return;
    }

    for (int channel = 0; channel < 16; ++channel)
    {
        const auto& state = midiTransformer_.getChannelState(channel);

        if (state.family != family)
            continue;

        const int volume =
            midiTransformer_.getAdjustedChannelVolume(channel);

        const int expression =
            midiTransformer_.getAdjustedChannelExpression(channel);

        outCC(channel, 7, volume);
        outCC(channel, 11, expression);
    }
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
    buildClickTrackLocked();

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
    buildClickTrackLocked();

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
            if (!channelMuted_[static_cast<std::size_t>(channel)])
                outNoteOn(channel, pressNote(channel, note), velocity);
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
    dirtyFamilies_.fetch_or(1u << family, std::memory_order_relaxed);
}

bool FluidSynthEngine::isPlaying() const
{
    return playing_.load(std::memory_order_relaxed);
}

std::int64_t FluidSynthEngine::positionSamples() const
{
    return currentSample_.load(std::memory_order_relaxed);
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

void FluidSynthEngine::setNormalizeEnabled(bool enabled)
{
    midiTransformer_.setNormalizeEnabled(enabled);
}

void FluidSynthEngine::resetSynth()
{
    if (!synth_)
        return;

    // Eerst alle voices direct laten stoppen, daarna resetten en de
    // reverb/chorus-staart uitrenderen. Een te korte flush liet na
    // stop + play nog restgeluid van het vorige nummer horen.
    fluid_synth_all_sounds_off(synth_, -1);
    fluid_synth_system_reset(synth_);

    if (external_ != nullptr)
        external_->allNotesOff(-1);

    for (auto& channel : soundingNote_)
        channel.fill(-1);

    clearExternalCache();

    // Een extern apparaat klinkt niet via FluidSynth: geen staart uitrenderen.
    if (external_ != nullptr)
        return;

    constexpr int bufferSize = 512;
    constexpr double flushSeconds = 2.0;

    float left[bufferSize];
    float right[bufferSize];

    auto remaining = static_cast<int>(
        std::ceil(sampleRate_ * flushSeconds));

    while (remaining > 0)
    {
        const int count = std::min(remaining, bufferSize);

        fluid_synth_write_float(
            synth_, count, left, 0, 1, right, 0, 1);

        remaining -= count;
    }
}

void FluidSynthEngine::outExternal(std::uint8_t status, int data1, int data2, int size)
{
    const std::uint8_t data[3] = {
        status,
        static_cast<std::uint8_t>(data1 & 0x7F),
        static_cast<std::uint8_t>(data2 & 0x7F)
    };

    if (inRender_)
        external_->push(data, size, renderBlockStart_ + renderOffsetSamples_);
    else
        external_->pushNow(data, size);
}

void FluidSynthEngine::clearExternalCache()
{
    for (auto& channel : lastExternalCC_)
        channel.fill(-1);

    lastExternalBend_.fill(-1);
}

void FluidSynthEngine::outNoteOn(int channel, int note, int velocity)
{
    if (external_ != nullptr)
        outExternal(static_cast<std::uint8_t>(0x90 | channel), note, velocity, 3);
    else
        fluid_synth_noteon(synth_, channel, note, velocity);
}

void FluidSynthEngine::outNoteOff(int channel, int note)
{
    if (external_ != nullptr)
        outExternal(static_cast<std::uint8_t>(0x80 | channel), note, 0, 3);
    else
        fluid_synth_noteoff(synth_, channel, note);
}

void FluidSynthEngine::outKeyPressure(int channel, int note, int value)
{
    if (external_ != nullptr)
        outExternal(static_cast<std::uint8_t>(0xA0 | channel), note, value, 3);
    else
        fluid_synth_key_pressure(synth_, channel, note, value);
}

void FluidSynthEngine::outCC(int channel, int controller, int value)
{
    if (external_ != nullptr)
    {
        // Doorlopende controllers (volume, expression, modulation, ...)
        // worden vaak herhaald zonder dat de waarde verandert.
        const bool continuous =
            controller == 1 || controller == 7 || controller == 10 || controller == 11
            || (controller >= 71 && controller <= 74) || controller == 91 || controller == 93;

        auto& last = lastExternalCC_[static_cast<std::size_t>(channel)][static_cast<std::size_t>(controller)];

        if (continuous && last == value)
            return;

        last = static_cast<std::int16_t>(value);

        outExternal(static_cast<std::uint8_t>(0xB0 | channel), controller, value, 3);
    }
    else
        fluid_synth_cc(synth_, channel, controller, value);
}

void FluidSynthEngine::outProgram(int channel, int program)
{
    if (external_ != nullptr)
        outExternal(static_cast<std::uint8_t>(0xC0 | channel), program, 0, 2);
    else
        fluid_synth_program_change(synth_, channel, program);
}

void FluidSynthEngine::outChannelPressure(int channel, int value)
{
    if (external_ != nullptr)
        outExternal(static_cast<std::uint8_t>(0xD0 | channel), value, 0, 2);
    else
        fluid_synth_channel_pressure(synth_, channel, value);
}

void FluidSynthEngine::outPitchBend(int channel, int value14)
{
    if (external_ != nullptr)
    {
        auto& last = lastExternalBend_[static_cast<std::size_t>(channel)];

        if (last == value14)
            return;

        last = value14;

        outExternal(static_cast<std::uint8_t>(0xE0 | channel), value14 & 0x7F, (value14 >> 7) & 0x7F, 3);
    }
    else
    {
        fluid_synth_pitch_bend(synth_, channel, value14);
    }
}

void FluidSynthEngine::outAllSoundsOff(int channel)
{
    if (external_ != nullptr)
        external_->allNotesOff(channel);
    else
        fluid_synth_all_sounds_off(synth_, channel);
}

void FluidSynthEngine::setExternalOutput(ExternalMidiOutput* output)
{
    std::scoped_lock lock(mutex_);

    if (external_ != nullptr)
        external_->allNotesOff(-1);

    external_ = output;
    clearExternalCache();

    // Alles wat nog klinkt in de interne synth stopt; het nieuwe doel
    // krijgt programma's, volumes en controllers van het huidige punt.
    if (synth_ != nullptr)
    {
        fluid_synth_all_sounds_off(synth_, -1);

        if (song_ != nullptr)
            rebuildSynthStateAt(currentSample_.load());
    }
}

void FluidSynthEngine::panic()
{
    std::scoped_lock lock(mutex_);

    if (synth_ != nullptr)
        fluid_synth_all_sounds_off(synth_, -1);

    if (external_ != nullptr)
        external_->allNotesOff(-1);

    for (auto& channel : soundingNote_)
        channel.fill(-1);

    clearExternalCache();
}

int FluidSynthEngine::transposedNote(int channel, int note) const
{
    // Kanaal 10 (index 9) is percussie: niet transponeren.
    if (channel == 9)
        return note;

    return std::clamp(note + transpose_, 0, 127);
}

int FluidSynthEngine::pressNote(int channel, int note)
{
    const int actual = transposedNote(channel, note);

    soundingNote_[static_cast<std::size_t>(channel)]
                 [static_cast<std::size_t>(note)] = static_cast<std::int8_t>(actual);

    return actual;
}

int FluidSynthEngine::releaseNote(int channel, int note)
{
    auto& slot = soundingNote_[static_cast<std::size_t>(channel)]
                              [static_cast<std::size_t>(note)];

    const int actual = slot >= 0 ? slot : transposedNote(channel, note);
    slot = -1;

    return actual;
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
    //
    // Gebeurt op de audio-thread: geen heap-allocaties.
    if (event.bytes.empty())
        return;

    std::array<std::uint8_t, 3> bytes {};
    const std::size_t size = std::min<std::size_t>(event.bytes.size(), 3);
    std::copy_n(event.bytes.begin(), size, bytes.begin());

    midiTransformer_.transformInPlace(bytes, size);

    const auto status = bytes[0];

    /*
        Meta events and system events aren't sent to
        FluidSynth in this first engine implementation.
    */
    if (status >= 0xF0)
        return;

    if (size < 2)
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
            if (size >= 3)
                outNoteOff(channel, releaseNote(channel, data1));

            break;
        }

        case 0x90:
        {
            if (size >= 3)
            {
                const int velocity =
                    std::clamp(static_cast<int>(bytes[2]), 0, 127);

                if (velocity == 0)
                    outNoteOff(channel, releaseNote(channel, data1));
                else if (!channelMuted_[static_cast<std::size_t>(channel)])
                    outNoteOn(channel, pressNote(channel, data1), velocity);
            }

            break;
        }

        case 0xA0:
        {
            if (size >= 3)
                outKeyPressure(channel,
                               transposedNote(channel, data1),
                               std::clamp(static_cast<int>(bytes[2]), 0, 127));

            break;
        }

        case 0xB0:
        {
            if (size >= 3)
                outCC(channel, data1, std::clamp(static_cast<int>(bytes[2]), 0, 127));

            break;
        }

        case 0xC0:
        {
            outProgram(channel, data1);
            break;
        }

        case 0xD0:
        {
            outChannelPressure(channel, data1);
            break;
        }

        case 0xE0:
        {
            if (size >= 3)
            {
                const int lsb = std::clamp(static_cast<int>(bytes[1]), 0, 127);
                const int msb = std::clamp(static_cast<int>(bytes[2]), 0, 127);

                outPitchBend(channel, lsb | (msb << 7));
            }

            break;
        }

        default:
            break;
    }
}

void FluidSynthEngine::setClickOffsetMs(int milliseconds)
{
    std::scoped_lock lock(mutex_);

    clickOffsetMs_ = std::clamp(milliseconds, -100, 500);
}

void FluidSynthEngine::buildClickTrackLocked()
{
    clickBeats_.clear();
    clickAccent_.clear();

    if (!song_ || song_->playbackEvents.empty())
        return;

    const auto& song = *song_;
    const double ppq = static_cast<double>(std::max(1, song.ticksPerQuarterNote));

    const auto tickToSample = [&](double tick)
    {
        double seconds = tick * 0.5 / ppq;   // 120 bpm zonder tempokaart

        if (!song.tempoMap.empty())
        {
            std::size_t i = 0;

            while (i + 1 < song.tempoMap.size()
                   && static_cast<double>(song.tempoMap[i + 1].tick) <= tick)
                ++i;

            const auto& point = song.tempoMap[i];
            seconds = point.seconds
                    + (tick - static_cast<double>(point.tick)) * 60.0 / (point.bpm * ppq);
        }

        return static_cast<std::int64_t>(std::llround(seconds * sampleRate_));
    };

    const double endTick = static_cast<double>(song.playbackEvents.back().tick);

    std::vector<TimeSignaturePoint> signatures = song.timeSignatures;

    if (signatures.empty())
        signatures.push_back(TimeSignaturePoint {});

    for (std::size_t s = 0; s < signatures.size(); ++s)
    {
        const auto& sig = signatures[s];
        const int numerator = std::max(1, sig.numerator);
        const int denominator = std::max(1, sig.denominator);

        // 6/8, 9/8 en 12/8: een klik per punt-kwartnoot.
        const bool compound = denominator == 8 && numerator >= 6 && numerator % 3 == 0;
        const double unit = ppq * 4.0 / static_cast<double>(denominator);
        const double step = compound ? unit * 3.0 : unit;
        const int perMeasure = compound ? numerator / 3 : numerator;

        const double from = static_cast<double>(sig.tick);
        const double to = s + 1 < signatures.size()
            ? static_cast<double>(signatures[s + 1].tick)
            : endTick;

        for (long long beat = 0; from + static_cast<double>(beat) * step < to; ++beat)
        {
            clickBeats_.push_back(tickToSample(from + static_cast<double>(beat) * step));
            clickAccent_.push_back(beat % perMeasure == 0 ? 1 : 0);
        }
    }
}

void FluidSynthEngine::renderClickLocked(float* out, int numSamples, std::int64_t blockStart) const
{
    const auto& family = familySettings_[7];

    if (!family.enabled || family.volume <= 0 || clickBeats_.empty())
        return;

    const float level = 0.7f * static_cast<float>(family.volume) / 127.0f;

    const double rate = sampleRate_;
    const auto voiceLength = static_cast<std::int64_t>(0.045 * rate);
    const auto offset = static_cast<std::int64_t>(std::llround(
        static_cast<double>(clickOffsetMs_ + (external_ != nullptr ? 20 : 0)) * 0.001 * rate));

    // Eerste tel die op dit moment nog kan klinken.
    auto it = std::lower_bound(clickBeats_.begin(), clickBeats_.end(),
                               blockStart - voiceLength - offset);

    constexpr double twoPi = 6.283185307179586;

    for (; it != clickBeats_.end(); ++it)
    {
        const auto start = *it + offset;

        if (start >= blockStart + numSamples)
            break;

        const bool accent = clickAccent_[static_cast<std::size_t>(it - clickBeats_.begin())] != 0;
        const double frequency = accent ? 1760.0 : 1175.0;

        const auto first = std::max<std::int64_t>(start, blockStart);
        const auto last = std::min<std::int64_t>(start + voiceLength, blockStart + numSamples);

        for (auto position = first; position < last; ++position)
        {
            const double t = static_cast<double>(position - start) / rate;
            const double attack = std::min(1.0, t / 0.001);
            const double envelope = attack * std::exp(-t / 0.008);

            out[position - blockStart] += level
                * static_cast<float>(envelope * std::sin(twoPi * frequency * t));
        }
    }
}

void FluidSynthEngine::render(
    float** output,
    int numChannels,
    int numSamples,
    float* clickOut)
{
    if (!output || numChannels <= 0 || numSamples <= 0)
        return;

    std::scoped_lock lock(mutex_);

    if (clickOut != nullptr)
        std::fill(clickOut, clickOut + numSamples, 0.0f);

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

    // Voor een extern apparaat: begin van dit blok op de songtijdlijn, zodat
    // elk event een eigen, vooruit ingepland tijdstip krijgt.
    renderBlockStart_ = currentSample_.load();
    inRender_ = true;

    if (external_ != nullptr)
        external_->beginBlock(renderBlockStart_, numSamples, sampleRate_);

    applyDirtyFamiliesLocked();

    while (rendered < numSamples)
    {
        const auto blockPosition =
            currentSample_ + rendered;

        renderOffsetSamples_ = rendered;

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

    renderOffsetSamples_ = 0;
    inRender_ = false;

    if (clickOut != nullptr)
        renderClickLocked(clickOut, numSamples, renderBlockStart_);

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