#pragma once

#include "DropListBox.h"
#include "MidiMapping.h"
#include "MidiOutSink.h"
#include "FluidSynthEngine.h"
#include "MidiFileReader.h"
#include "WebServer.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <functional>
#include <chrono>
#include <memory>
#include <vector>
#include <atomic>

class MainComponent final
    : public juce::AudioAppComponent,
      public juce::FileDragAndDropTarget,
      public juce::DragAndDropContainer,
      private juce::MidiInputCallback,
      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(
        const juce::StringArray& files) override;

    void filesDropped(
        const juce::StringArray& files,
        int x,
        int y) override;

    void paintOverChildren(juce::Graphics& g) override;

    // Kopieer-cursor (+) tijdens het slepen van een song met Command/Ctrl.
    juce::MouseCursor getMouseCursor() override;

    void prepareToPlay(
        int samplesPerBlockExpected,
        double sampleRate) override;

    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& bufferToFill) override;

    void releaseResources() override;

private:
    // Een nummer in een setlist. Het bestand blijft altijd bestaan onder
    // de motorkap; displayName is alleen de getoonde naam.
    struct SongEntry
    {
        juce::File file;
        juce::String displayName;   // leeg = bestandsnaam tonen
        int transpose = 0;          // standaard transpose bij laden

        [[nodiscard]] juce::String shownName(bool withExtension) const
        {
            if (displayName.isNotEmpty())
                return displayName;

            return withExtension ? file.getFileName()
                                 : file.getFileNameWithoutExtension();
        }
    };

    struct SetlistData
    {
        juce::String name;
        std::vector<SongEntry> songs;
    };

    struct FamilyControl
    {
        juce::TextButton labelButton;
        juce::Label valueLabel;
        juce::Slider slider;
    };

    std::vector<SetlistData> setlists;
    int selectedSetlist = 0;

    std::array<juce::Rectangle<int>, 8> familyBlockBounds;
    juce::Rectangle<int> transposeBounds;

    WebServer webServer;

    using Clock = std::chrono::steady_clock;

    std::array<std::uint64_t, 16> channelEventCounters {};
    std::array<Clock::time_point, 16> channelEventTimes {};

    MidiFileReader midiReader;
    FluidSynthEngine synthEngine;

    std::shared_ptr<const Song> currentSong;

    double audioSampleRate = 48000.0;
    bool audioReady = false;

    juce::Label midiControlLabel;
    juce::ComboBox midiControlBox;

    juce::Label midiOutLabel;
    juce::ComboBox midiOutBox;

    juce::Label soundFontLabel;
    juce::TextButton soundFontButton;
    juce::TextButton settingsButton;
    juce::TooltipWindow tooltipWindow { this, 400 };

    std::array<FamilyControl, 8> familyControls;

    juce::Label songTitle;
    juce::Label nextSong;
    juce::Label elapsedTime;
    juce::Label totalTime;

    juce::Slider positionSlider;

    juce::TextButton loadButton;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton nextButton;

    juce::TextButton transposeDownButton;
    juce::Label transposeValue;
    juce::TextButton transposeUpButton;

    juce::TextButton panicButton;
    juce::TextButton lyricsButton;
    juce::TextButton channelsButton;

    juce::Label setlistsTitle;
    juce::Label songsTitle;
    juce::Label totalTimeTitle;

    DropListBox setlistBox;
    DropListBox songBox;

    juce::TextButton newSetlistButton;
    juce::TextButton deleteSetlistButton;

    juce::TextButton doubleClickToggle;
    juce::TextButton normalizeToggle;
    juce::TextButton continuousToggle;

    juce::TextButton addButton;
    juce::TextButton removeButton;

    juce::TableListBox channelTable;

    class SetlistModel;
    class SongModel;
    class ChannelModel;

    std::unique_ptr<SetlistModel> setlistModel;
    std::unique_ptr<SongModel> songModel;
    std::unique_ptr<ChannelModel> channelModel;

    juce::ApplicationProperties appProperties;

    juce::File currentMidiFile;
    juce::String currentDisplayName;   // leeg = bestandsnaam
    juce::File currentSoundFont;

    std::atomic<bool> isPlaying{false};
    bool positionSliderWasPlaying = false;
    bool positionSliderDragging = false;
    bool currentSongFromSetlist = false;
    bool wasEnginePlaying = false;
    juce::String webControlKey { "merik" };

    // MIDI out: de interne synth ("Soundfont player") of een extern apparaat.
    std::unique_ptr<MidiOutSink> midiOutSink;
    juce::StringArray midiOutIdentifiers;       // index = combo-id - 2
    juce::String desiredMidiOut;                // leeg = Soundfont player
    juce::String openedMidiOut;                 // apparaat dat nu echt open is
    juce::MidiDeviceListConnection midiDeviceListConnection;
    void refreshMidiOutputs();

    // MIDI control in (bijv. nanoKONTROL2): faders = family-volumes,
    // knoppen = Play/Stop/Next/Panic, net als in Merik 1.
    std::unique_ptr<juce::MidiInput> midiIn;
    juce::StringArray midiInIdentifiers;        // index = combo-id - 2
    juce::String desiredMidiIn;                 // leeg = geen
    juce::String openedMidiIn;
    std::atomic<int> lastMidiInCc { -1 };
    std::atomic<int> lastMidiInValue { 0 };
    void refreshMidiInputs();
    void applyMidiInput();
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void panic();

    // Toewijzing van de controller (instelbaar in Settings > Midi).
    MidiMapping midiMapping;
    void loadMidiMapping();
    void saveMidiMapping();
    void saveSynthSettings(const SynthSettings& settings);

    // LEDs van de controller (nanoKONTROL2 in "External" LED-modus):
    // Next en Stop altijd aan, Play aan bij spelen en knipperend bij pauze,
    // de R-knoppen volgen aan/uit van de families.
    juce::ComboBox ledOutBox;
    std::unique_ptr<juce::MidiOutput> ledOut;
    juce::StringArray ledIdentifiers;           // index = combo-id - 3
    juce::String ledChoice { "auto" };          // "auto", "none" of een identifier
    juce::String openedLedOut;
    std::array<int, 128> ledLast {};
    void refreshLedOutputs();
    void applyLedOutput();
    void updateControllerLeds();
    void ledsAllOff();
    void applyMidiOutput();
    int transpose = 0;
    void setTranspose(int semitones);

    void loadMidi();
    void selectSoundFont();
    void showAudioSettings();

    // Houdt de kopieën van de toggles in het settingsvenster gelijk
    // met die in het hoofdscherm (leeg zolang het venster dicht is).
    std::function<void()> syncSettingsToggles;
    juce::Component::SafePointer<juce::Component> settingsWindow;
    void play();
    void stop();

    void updateSongDisplay();
    void updateTransportDisplay();
    void updateChannelModel();

    void syncFamilySettingsToWebServer();
    void timerCallback() override;

    void loadMidiFile(const juce::File& file, bool fromSetlist = false,
                      const SongEntry* entry = nullptr);
    void playNextInSetlist();
    void skipToNextSong();
    [[nodiscard]] const SongEntry* nextSetlistEntry() const;

    // Setlists en songs: slepen, kopiëren en contextmenu's.
    void moveSetlist(int from, int insertBefore);
    void moveSongWithinSetlist(int from, int insertBefore);
    void moveOrCopySong(int songRow, int targetSetlist, bool copy);
    void showSetlistMenu(int row);
    void showSongMenu(int row);
    void promptRename(const juce::String& title, const juce::String& current,
                      std::function<void(const juce::String&)> onAccepted);
    void removeSetlistAt(int row);
    void removeSongAt(int row);
    void addSetlist();

    void loadPersistentState();
    void savePersistentState();

    void refreshSetlistModel();
    void refreshSongModel();

    static juce::String formatTime(double seconds);

    static constexpr juce::uint32 merikBlueRGB = 0x00A4EB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};