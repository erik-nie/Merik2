#pragma once

#include "BeatIndicator.h"
#include "DropListBox.h"
#include "ListSplitter.h"
#include "UpdateChecker.h"
#include "MidiMapping.h"
#include "MidiOutSink.h"
#include "FluidSynthEngine.h"
#include "MidiFileReader.h"
#include "LevelMeter.h"
#include "SourcePill.h"
#include "UpdatePill.h"
#include "IconButton.h"
#include "RenderEngine.h"
#include "RenderJob.h"
#include "WebServer.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <functional>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

class PackageLoader;
class LoadingOverlay;

class MainComponent final
    : public juce::AudioAppComponent,
      public juce::FileDragAndDropTarget,
      public juce::DragAndDropContainer,
      public juce::KeyListener,
      private juce::MidiInputCallback,
      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // Laadt een bestand (bijv. via 'Open met' of de opdrachtregel).
    void openFile(const juce::File& file);

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
        int volumeDb = 0;           // volume van dit nummer (delta in dB)

        // Achtergrondinformatie over het bestand (onthouden in de instellingen).
        double lengthSeconds = -1.0;    // < 0: nog niet bekend
        int hasLyrics = -1;             // -1 onbekend, 0 nee, 1 ja
        int hasChords = -1;
        int hasMidi = -1;               // MIDI-noten in het bestand
        int hasAudio = -1;              // audio-stems in het bestand
        juce::int64 fileTime = 0;       // wijzigingstijd waarvoor dit geldt
        bool missing = false;           // bestand bestaat niet (meer)

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
        SourcePill sourcePill;      // AUDIO / MIDI / CLICK
        LevelMeter meter;
        juce::Slider slider;
    };

    std::vector<SetlistData> setlists;
    int selectedSetlist = 0;

    std::array<juce::Rectangle<int>, 8> familyBlockBounds;
    juce::Rectangle<int> transposeBounds;
    juce::Rectangle<int> volumeBounds;

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
    juce::Label setlistIndexLabel;   // bijv. "6/10"
    BeatIndicator beatIndicator;     // maatsoort + ledjes in de maat
    juce::Label elapsedTime;
    juce::Label totalTime;

    juce::Slider positionSlider;

    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton nextButton;

    juce::TextButton transposeDownButton;
    juce::Label transposeValue;
    juce::TextButton transposeUpButton;
    juce::TextButton volumeDownButton;
    juce::Label volumeValue;
    juce::TextButton volumeUpButton;

    juce::TextButton panicButton;
    juce::TextButton lyricsButton;
    juce::TextButton channelsButton;

    juce::Label setlistsTitle;
    juce::Label songsTitle;
    juce::Label totalTimeTitle;

    DropListBox setlistBox;
    DropListBox songBox;

    juce::TextButton newSetlistButton;

    juce::TextButton doubleClickToggle;
    juce::TextButton normalizeToggle;
    juce::TextButton continuousToggle;
    juce::TextButton updateCheckToggle;   // "Check for updates at startup"

    juce::TextButton addButton;
    IconButton playFileButton { IconButton::Icon::openFile };   // een bestand openen en eenmalig afspelen

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

    // Klik: waar naar toe (zie ClickSettings.h) en hulpbuffer voor het audioblok.
    std::atomic<int> mainRoute { 200 };   // ClickRoute::pairBase: uitgang 1+2
    std::atomic<int> clickRoute { 0 };
    std::array<std::atomic<int>, 7> stemRoutes {};   // per familie: 0 = hoofduitgang, anders ClickRoute-code
    std::vector<float> mainBuffer[2];
    int clickOffsetMs = 0;
    std::vector<float> clickBuffer;
    void saveClickSettings(int route, int offsetMs);
    static const std::array<juce::String, 8>& defaultFamilyLabels();
    void showFamilyNames(const std::array<std::string, 8>& names);

    bool keyPressed(const juce::KeyPress& key) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

    // Bewaart de audio-instellingen (apparaat, kanalen) zodra ze veranderen.
    struct DeviceListener final : juce::ChangeListener
    {
        std::function<void()> onChange;

        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            if (onChange)
                onChange();
        }
    };

    DeviceListener deviceListener;
    bool positionSliderWasPlaying = false;
    bool positionSliderDragging = false;
    bool currentSongFromSetlist = false;
    int playingRow = -1;            // rij van het geladen nummer in de setlist (dubbele nummers)
    int playingSetlist = -1;        // setlist waaruit het geladen nummer komt
    [[nodiscard]] int currentRowInPlayingSetlist() const;

    // Verdeling tussen setlists en songs; bij elke start 1/3 - 2/3.
    ListSplitter listSplitter;
    float setlistFraction = 1.0f / 3.0f;
    juce::Rectangle<int> listsBounds;
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
    int songVolumeDb = 0;        // volume van het huidige nummer (delta, dB)
    void setSongVolume(int db, bool remember = true);
    bool songHasStems = false;   // audio-stems kunnen niet transponeren
    void setTranspose(int semitones, bool remember = true);

    void loadMidi();
    void selectSoundFont();
    void showAudioSettings();

    // Houdt de kopieën van de toggles in het settingsvenster gelijk
    // met die in het hoofdscherm (leeg zolang het venster dicht is).
    std::function<void()> syncSettingsToggles;
    juce::Component::SafePointer<juce::Component> settingsWindow;
    juce::Component::SafePointer<juce::Component> makerWindow;
    void showMerikMaker();

    // Opnemen van MIDI via een externe module (Settings > Tools).
    std::unique_ptr<RenderEngine> renderEngine;
    std::unique_ptr<RenderJob> renderJob;
    RenderStatus lastRenderStatus;
    RenderPrefs renderPrefs;
    juce::Component::SafePointer<juce::Component> calibrationWindow;
    juce::Component::SafePointer<juce::Component> renderWindow;
    void showRenderWindow();
    void loadRenderPrefs();
    void saveRenderPrefs();
    juce::String startRender(const RenderOptions& options);
    void stopRender();
    void showRenderCalibration();
    void pollRender();
    void play();
    void stop();

    void updateSongDisplay();
    void updateTransportDisplay();
    void updateChannelModel();

    void syncFamilySettingsToWebServer();
    void timerCallback() override;

    void loadMidiFile(const juce::File& file, bool fromSetlist = false,
                      const SongEntry* entry = nullptr, int setlistRow = -1,
                      int setlistIndex = -1);

    // Een .merik wordt op een eigen thread geladen, met een voortgangsscherm.
    struct PendingLoad
    {
        juce::File file;
        bool fromSetlist = false;
        bool hasEntry = false;
        SongEntry entry;
        int row = -1;
        int index = -1;
    };

    void completeLoad(const PendingLoad& pending, const juce::File& midiPath,
                      std::shared_ptr<const StemSet> stems, bool tempoKnown);
    void packageLoadFinished(int id);
    void cancelPackageLoad();

    std::unique_ptr<PackageLoader> packageLoader;
    std::unique_ptr<LoadingOverlay> loadingOverlay;
    PendingLoad pendingLoad;
    bool pendingPlay = false;     // Play is gedrukt terwijl er nog geladen wordt
    int nextLoadId = 1;
    void playNextInSetlist(bool startPlaying = true);
    void skipToNextSong();
    [[nodiscard]] const SongEntry* nextSetlistEntry() const;

    // Setlists en songs: slepen, kopiëren en contextmenu's.
    void moveSetlist(int from, int insertBefore);
    void moveSongsWithinSetlist(juce::Array<int> rows, int insertBefore);
    void moveOrCopySongs(juce::Array<int> rows, int targetSetlist, bool copy);
    void duplicateSetlist(int row);
    void clearSetlist(int row);
    void showSetlistMenu(int row);
    void showSongMenu(int row);
    void promptRename(const juce::String& title, const juce::String& current,
                      std::function<void(const juce::String&)> onAccepted);
    void removeSetlistAt(int row);
    void removeSongs(juce::Array<int> rows);
    [[nodiscard]] juce::Array<int> selectedSongRows() const;
    void addSetlist();

    // Controle op een nieuwe versie (GitHub).
    void checkForUpdates(bool userInitiated);

    // Een nieuwe versie: eerst alleen een rode pil; de klik opent het venster.
    UpdatePill updatePill;
    juce::String pendingUpdateUrl, pendingUpdateVersion, pendingUpdateCurrent;
    void showUpdateDialog();
    void handleUpdateResult(const UpdateChecker::Result& result, const juce::String& currentVersion,
                            bool userInitiated);

    // Duur, lyrics en akkoorden van de nummers: op de achtergrond uitgelezen.
    std::mutex midiReadMutex;       // de MIDI-lezer wordt niet door twee threads tegelijk gebruikt
    std::unique_ptr<class SongInfoScanner> songScanner;
    void scanSongInfo();
    bool songInfoDirty = false;
    int songInfoSaveTicks = 0;
    void applySongInfo(const juce::File& file, double length, int lyrics, int chords, int midi, int audio);
    void updateMissingFlags();
    [[nodiscard]] bool hasNextSong() const;
    void confirmDialog(const juce::String& title, const juce::String& message,
                       const juce::String& okText, std::function<void()> onOk);

    void loadPersistentState();
    void savePersistentState();

    void refreshSetlistModel();
    void refreshSongModel();

    static juce::String formatTime(double seconds);

    static constexpr juce::uint32 merikBlueRGB = 0x00A4EB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};