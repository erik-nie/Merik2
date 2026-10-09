#pragma once

#include "FluidSynthEngine.h"
#include "MidiFileReader.h"
#include "WebServer.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <chrono>
#include <memory>
#include <vector>

class MainComponent final
    : public juce::AudioAppComponent,
      public juce::FileDragAndDropTarget,
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

    void prepareToPlay(
        int samplesPerBlockExpected,
        double sampleRate) override;

    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& bufferToFill) override;

    void releaseResources() override;

private:
    struct SetlistData
    {
        juce::String name;
        std::vector<juce::File> songs;
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

    juce::ListBox setlistBox;
    juce::ListBox songBox;

    juce::TextButton newSetlistButton;
    juce::TextButton deleteSetlistButton;

    juce::ToggleButton doubleClickToggle;
    juce::ToggleButton normalizeToggle;
    juce::ToggleButton continuousToggle;

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
    juce::File currentSoundFont;

    bool isPlaying = false;
    bool positionSliderWasPlaying = false;
    int transpose = 0;

    void loadMidi();
    void selectSoundFont();
    void showAudioSettings();
    void play();
    void stop();

    void updateSongDisplay();
    void updateTransportDisplay();
    void updateChannelModel();

    void timerCallback() override;

    void loadMidiFile(const juce::File& file);

    void loadPersistentState();
    void savePersistentState();

    void refreshSetlistModel();
    void refreshSongModel();

    static juce::String formatTime(double seconds);

    static constexpr juce::uint32 merikBlueRGB = 0x00A4EB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};