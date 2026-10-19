#pragma once

#include "Song.h"
#include "MidiTransformer.h"
#include "StemSet.h"

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

    int reverbRoom = 50;             // 0..100  -> 0.0 .. 1.0
    int reverbDamp = 50;             // 0..100  -> 0.0 .. 1.0
    int reverbWidth = 50;            // 0..100
    int reverbLevel = 10;            // 0..100  -> 0.0 .. 1.0
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

    // Volume van dit nummer in dB, bovenop de faders: bij audio-stems de stemniveaus,
    // bij de interne SoundFont de gain, bij een extern MIDI-apparaat het CC7-volume
    // (daar kan het niet boven het maximum van het bestand zelf uit).
    void setSongVolumeDb(float db);
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

    // Klik: de tellen van het nummer (uit tempo en maatsoort) als korte tikjes.
    // Familie 8 (index 7) is de volumeknop en aan/uit van de klik. De offset
    // vertraagt de klik (ms) om bij een extern MIDI-apparaat uit te lijnen.
    void setClickOffsetMs(int milliseconds);

    // Audio-stems naast of in plaats van de MIDI. Een familie met een stem
    // wordt niet meer uit de MIDI gespeeld; de familiefaders regelen de stems.
    // Zet vóór loadSong(); nullptr = geen stems.
    void setStems(std::shared_ptr<const StemSet> stems);

    // Per familie (bit n = familie n) of de klank uit audio-stems komt.
    // Na render(): het audiosignaal van een familie met stem (0..6), links of
    // rechts (channel 0/1), numSamples lang; nullptr als de familie geen stem
    // heeft. Geldig tot de volgende render().
    [[nodiscard]] const float* stemBuffer(int family, int channel) const
    {
        if (family < 0 || family >= 7 || (stemMask_.load(std::memory_order_relaxed) & (1u << family)) == 0)
            return nullptr;

        return familyAudio_[static_cast<std::size_t>(family)][static_cast<std::size_t>(channel & 1)].data();
    }

    // Bit n = familie n kon sinds de vorige aanroep een deel niet op tijd van schijf lezen.
    [[nodiscard]] std::uint32_t takeBufferingMask() { return bufferingMask_.exchange(0, std::memory_order_relaxed); }

    [[nodiscard]] std::uint32_t stemFamilyMask() const { return stemMask_.load(std::memory_order_relaxed); }

    // Pieken per familie sinds de vorige aanroep (0..1). Audiofamilies en de
    // klik: het echte signaal. MIDI-families: activiteit uit de noten (de
    // synth mengt alles tot één stereomix), die in ~0,3 s uitdooft.
    [[nodiscard]] std::array<float, familyCount> takeMeters();

    // clickOut (optioneel, numSamples lang) krijgt de klik apart van de mix,
    // op dezelfde samplepositie als de MIDI/soundfont, zodat ze synchroon blijven.
    void render(
        float** output,
        int numChannels,
        int numSamples,
        float* clickOut = nullptr);

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

    void mixStemsLocked(float* clickOut, int numSamples, std::int64_t blockStart);
    std::array<std::array<std::vector<float>, 2>, 7> familyAudio_;
    [[nodiscard]] int channelFamilyLocked(int channel) const;
    [[nodiscard]] bool familyHasStem(int channel) const;

    std::shared_ptr<const StemSet> stems_;
    std::uint32_t stemFamilies_ = 0;
    std::atomic<std::uint32_t> stemMask_ { 0 };
    std::array<std::atomic<float>, familyCount> peakHold_ {};
    std::array<float, familyCount> midiLevel_ {};
    void notePeak(int family, float value);
    std::vector<float> stemGains_;
    std::vector<float> stemScratch_[2];
    std::atomic<std::uint32_t> bufferingMask_ { 0 };

    void hintStemsLocked(std::int64_t samplePosition);
    void applySongVolumeLocked();
    float songVolumeDb_ = 0.0f;
    void buildClickTrackLocked();
    void renderClickLocked(float* out, int numSamples, std::int64_t blockStart) const;

    std::vector<std::int64_t> clickBeats_;     // samplepositie van elke tel
    std::vector<std::uint8_t> clickAccent_;    // 1 = eerste tel van de maat
    int clickOffsetMs_ = 0;

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