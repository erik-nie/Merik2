#pragma once

#include "Song.h"
#include "MidiTransformer.h"

#include <fluidsynth.h>

#include <array>
#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Doel voor MIDI-events buiten de interne synth (echte of virtuele MIDI-uitgang).
// Alle aanroepen komen onder de mutex van de engine; push() en beginBlock()
// vanuit de audio-thread en mogen dus niet blokkeren.
class ExternalMidiOutput
{
public:
    virtual ~ExternalMidiOutput() = default;

    // Begin van een audioblok: samplepositie (op de songtijdlijn) van de
    // eerste sample van het blok. Hiermee wordt de kloktoewijzing bijgehouden.
    virtual void beginBlock(std::int64_t sampleAtBlockStart, int numSamples, double sampleRate) = 0;

    // Bericht dat op deze samplepositie (songtijdlijn) moet klinken.
    virtual void push(const std::uint8_t* data, int size, std::int64_t sampleTime) = 0;

    // Bericht dat meteen verstuurd moet worden (reset, volume van de GUI, ...).
    virtual void pushNow(const std::uint8_t* data, int size) = 0;

    // Alle noten uit; channel -1 = alle kanalen.
    virtual void allNotesOff(int channel) = 0;
};

// Klank van de interne synth, in dezelfde eenheden als de sliders in Settings
// (zoals in Merik 1). De standaardwaarden geven het geluid van FluidSynth zelf.
// Chorus staat altijd uit en is dus geen instelling.
struct SynthSettings
{
    int gain = 50;                   // 0..100  -> 0.0 .. 1.0

    int reverbRoom = 20;             // 0..100  -> 0.0 .. 1.0
    int reverbDamp = 0;              // 0..100  -> 0.0 .. 1.0
    int reverbWidth = 50;            // 0..100
    int reverbLevel = 90;            // 0..100  -> 0.0 .. 1.0
};

struct FamilySettings
{
    int volume = 127;
    bool enabled = true;
};

class FluidSynthEngine final
{
public:
    static constexpr int familyCount = MidiTransformer::familyCount;

    FluidSynthEngine();
    ~FluidSynthEngine();

    FluidSynthEngine(const FluidSynthEngine&) = delete;
    FluidSynthEngine& operator=(const FluidSynthEngine&) = delete;

    void setSampleRate(double sampleRate);

    bool loadSoundFont(
        const std::string& path,
        std::string& error);

    void loadSong(std::shared_ptr<const Song> song);

    void start();
    void stop();
    void seekSamples(std::int64_t samplePosition);

    void setNormalizeEnabled(bool enabled);
    [[nodiscard]] FamilySettings getFamilySettings(int family) const;

    [[nodiscard]] std::array<FamilySettings, familyCount>
    getAllFamilySettings() const;

    void setFamilyVolume(int family, int volume);
    void setFamilyEnabled(int family, bool enabled);

    void setFamilyVolumeFactor(int family, float factor);

    // Kanaal dempen: nieuwe noten worden niet meer gestart en
    // klinkende noten stoppen direct. Andere events lopen door.
    void setChannelMuted(int channel, bool muted);

    void setSynthSettings(const SynthSettings& settings);
    [[nodiscard]] SynthSettings getSynthSettings() const;

    // nullptr = interne synth (Soundfont player); anders gaan alle
    // MIDI-events naar dit apparaat en blijft de interne synth stil.
    // De engine bezit het object niet; zet eerst nullptr voor het wordt vernietigd.
    void setExternalOutput(ExternalMidiOutput* output);

    // Panic: alle klinkende noten direct stoppen (intern en extern), zonder
    // het afspelen te stoppen.
    void panic();

    // Transponeert alle noten behalve kanaal 10 (drums), in halve tonen.
    void setTranspose(int semitones);

    [[nodiscard]] bool isPlaying() const;

    [[nodiscard]] MidiChannelState getChannelState(int channel) const;

    [[nodiscard]] int getAdjustedChannelVolume(int channel) const;

    [[nodiscard]] std::int64_t positionSamples() const;
    [[nodiscard]] std::int64_t lengthSamples() const;

    void render(
        float** output,
        int numChannels,
        int numSamples);

    [[nodiscard]] std::string soundFontPath() const;

private:
    void destroySynth();
    void createSynth();

    void sendMidiEvent(const RawMidiEvent& event);

    // Alle uitgaande events lopen hierlangs: interne synth of extern apparaat.
    void outNoteOn(int channel, int note, int velocity);
    void outNoteOff(int channel, int note);
    void outKeyPressure(int channel, int note, int value);
    void outCC(int channel, int controller, int value);
    void outProgram(int channel, int program);
    void outChannelPressure(int channel, int value);
    void outPitchBend(int channel, int value14);
    void outAllSoundsOff(int channel);
    void outExternal(std::uint8_t status, int data1, int data2, int size);

    [[nodiscard]] int transposedNote(int channel, int note) const;
    int pressNote(int channel, int note);
    int releaseNote(int channel, int note);
    void resetSynth();

    void rebuildSynthStateAt(std::int64_t samplePosition);

    void updateFamilyFactorLocked(int family);
    void applyFamilyVolumeLocked(int family);
    void applySynthSettingsLocked();

    // Volume-wijzigingen van de families worden verzameld en per audioblok
    // (extern: maximaal elke 30 ms) uitgestuurd. Een fader die snel beweegt
    // gaf anders per stap CC7 + CC11 op elk kanaal van de family, onder het
    // slot van de engine: dat blokkeerde de audio en overspoelde een
    // DIN-uitgang.
    std::atomic<std::uint32_t> dirtyFamilies_ { 0 };
    std::chrono::steady_clock::time_point lastFamilyApply_ {};
    void applyDirtyFamiliesLocked();
    SynthSettings synthSettings_;



    struct ChannelPlaybackState
    {
        int bankMsb = 0;
        int bankLsb = 0;
        int program = 0;

        int cc7 = 127;
        int cc11 = 127;
        int sustain = 0;

        int pitchBend = 8192;
        int channelPressure = 0;

        std::array<int, 128> controllers {};
        std::array<bool, 128> activeNotes {};
    };

    std::array<ChannelPlaybackState, 16> playbackState {};

    std::array<FamilySettings, familyCount> familySettings_ {};
    std::array<bool, 16> channelMuted_ {};
    int transpose_ = 0;

    ExternalMidiOutput* external_ = nullptr;
    int renderOffsetSamples_ = 0;   // positie van het huidige event in het audioblok
    std::int64_t renderBlockStart_ = 0;
    bool inRender_ = false;

    // Laatst naar het externe apparaat gestuurde waarden: herhalingen die
    // niets veranderen worden niet verstuurd (spaart DIN-bandbreedte).
    std::array<std::array<std::int16_t, 128>, 16> lastExternalCC_ {};
    std::array<std::int32_t, 16> lastExternalBend_ {};
    void clearExternalCache();

    // Klinkende (getransponeerde) noot per ingedrukte noot, zodat een
    // Note Off de juiste noot stopt ook als transpose tussendoor wijzigt.
    std::array<std::array<std::int8_t, 128>, 16> soundingNote_ {};

    MidiTransformer midiTransformer_;

    [[nodiscard]] std::int64_t eventSamplePosition(
        const RawMidiEvent& event) const;

    fluid_settings_t* settings_ = nullptr;
    fluid_synth_t* synth_ = nullptr;

    int soundFontId_ = -1;

    double sampleRate_ = 48000.0;

    std::string soundFontPath_;

    std::shared_ptr<const Song> song_;

    std::size_t nextEvent_ = 0;
    std::atomic<std::int64_t> currentSample_ { 0 };

    std::atomic<bool> playing_ { false };

    mutable std::mutex mutex_;
};