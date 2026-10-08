#include "MainComponent.h"
#include "WebServer.h"

#include <array>
#include <chrono>

namespace
{
    const juce::Colour backgroundColour =
        juce::Colour::fromRGB(25, 25, 25);

    const juce::Colour panelColour =
        juce::Colour::fromRGB(38, 38, 38);

    const juce::Colour panelLightColour =
        juce::Colour::fromRGB(48, 48, 48);

    const juce::Colour merikBlue =
        juce::Colour::fromRGB(0x2e, 0x9a, 0xfe);

    const juce::Colour textColour =
        juce::Colours::white;

    const juce::Colour secondaryTextColour =
        juce::Colour::fromRGB(190, 190, 190);

    const juce::Colour familyBlockColour =
        juce::Colour::fromRGB(32, 32, 32);

    const juce::Colour familyBorderColour =
        juce::Colour::fromRGB(65, 65, 65);

}

class MerikLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MerikLookAndFeel()
    {
        setColour(
            juce::TextButton::buttonColourId,
            panelLightColour);

        setColour(
            juce::TextButton::buttonOnColourId,
            merikBlue);

        setColour(
            juce::TextButton::textColourOffId,
            juce::Colours::white);

        setColour(
            juce::TextButton::textColourOnId,
            juce::Colours::white);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return makeFont(15.0f);
    }

    juce::Font getTextButtonFont(
        juce::TextButton&,
        int buttonHeight) override
    {
        return makeFont(
            juce::jlimit(
                13.0f,
                16.0f,
                buttonHeight * 0.45f));
    }

    juce::Font getComboBoxFont(
        juce::ComboBox&) override
    {
        return makeFont(14.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return makeFont(14.0f);
    }

    // -------------------------------------------------------------------------
    // Custom Merik slider
    // -------------------------------------------------------------------------

    void drawLinearSlider(
        juce::Graphics& g,
        int x,
        int y,
        int width,
        int height,
        float sliderPos,
        float minSliderPos,
        float maxSliderPos,
        juce::Slider::SliderStyle sliderStyle,
        juce::Slider& slider) override
    {
        juce::ignoreUnused(
            minSliderPos,
            maxSliderPos,
            sliderStyle);

        const float centreY =
            static_cast<float>(y) +
            static_cast<float>(height) * 0.5f;

        const float left =
            static_cast<float>(x);

        const float right =
            static_cast<float>(x + width);

        // -------------------------------------------------------------
        // Background track
        // -------------------------------------------------------------

        g.setColour(
            juce::Colour::fromRGB(70, 70, 70));

        g.fillRoundedRectangle(
            left,
            centreY - 3.0f,
            right - left,
            6.0f,
            3.0f);

        // -------------------------------------------------------------
        // Active track
        // -------------------------------------------------------------

        g.setColour(merikBlue);

        g.fillRoundedRectangle(
            left,
            centreY - 3.0f,
            sliderPos - left,
            6.0f,
            3.0f);

        // -------------------------------------------------------------
        // Thumb
        // -------------------------------------------------------------

        g.setColour(
            juce::Colours::white);

        g.fillEllipse(
            sliderPos - 8.0f,
            centreY - 8.0f,
            16.0f,
            16.0f);
    }

private:
    static juce::Font makeFont(float height)
    {
        return juce::Font(
            juce::FontOptions{}
                .withName("SF Pro Text")
                .withHeight(height));
    }
};


MerikLookAndFeel merikLookAndFeel;

// ============================================================================
// Basic list model
// ============================================================================

class BasicListModel : public juce::ListBoxModel
{
public:
    explicit BasicListModel(std::vector<juce::String> valuesToUse)
        : values(std::move(valuesToUse))
    {
    }

    int getNumRows() override
    {
        return static_cast<int>(values.size());
    }

    void paintListBoxItem(
        int rowNumber,
        juce::Graphics& g,
        int width,
        int height,
        bool rowIsSelected) override
    {
        if (rowNumber < 0 ||
            rowNumber >= static_cast<int>(values.size()))
        {
            return;
        }

        if (rowIsSelected)
            g.setColour(merikBlue);
        else if (rowNumber % 2 == 0)
            g.setColour(juce::Colour::fromRGB(47, 47, 47));
        else
            g.setColour(juce::Colour::fromRGB(39, 39, 39));

        g.fillRect(0, 0, width, height);

        g.setColour(textColour);
        g.setFont(juce::FontOptions(13.0f));

        g.drawText(
            values[static_cast<size_t>(rowNumber)],
            8,
            0,
            width - 16,
            height,
            juce::Justification::centredLeft);
    }

private:
    std::vector<juce::String> values;
};

namespace
{
class AudioSettingsWindow final : public juce::DialogWindow
{
public:
    AudioSettingsWindow(juce::Component* content,
                        juce::Component* owner)
        : juce::DialogWindow(
              "Audio Settings",
              backgroundColour,
              true)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);

        setContentOwned(content, true);

        centreAroundComponent(owner, 700, 500);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        delete this;
    }
};
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        0, 0,   // geen audio-input
        1, 2,   // 1-2 outputkanalen
        false,  // geen MIDI input
        false,  // geen MIDI output
        true,   // stereo pairs
        false); // advanced settings

    selector->setSize(700, 500);

    new AudioSettingsWindow(
        selector,
        this);
}
// ============================================================================
// SetlistModel
// ============================================================================

class MainComponent::SetlistModel : public BasicListModel
{
public:
    SetlistModel()
        : BasicListModel(
        {
            "First Setlist        176:40",
            "Party                160:58",
            "Misc                  15:28",
            "OnlyText              47:03",
            "Test12                149:37",
            "Karaoke               398:41",
            "Test1                 148:51",
            "Test2                 356:12"
        })
    {
    }
};

// ============================================================================
// SongModel
// ============================================================================

class MainComponent::SongModel : public BasicListModel
{
public:
    SongModel()
        : BasicListModel(
        {
            "It's Not XXXXXXX       Tom Jones",
            "Delilah                Tom Jones",
            "Sex Bomb               Tom Jones",
            "Africa                 Toto",
            "Rosanna                Toto",
            "Hold The Line          Toto",
            "Red Red Wine           UB40",
            "Kingston Town          UB40",
            "Can't Help Falling     UB40",
            "Summer Of '69          Bryan Adams",
            "Have You Ever Seen     CCR",
            "Brown Eyed Girl        Van Morrison",
            "Sweet Caroline         Neil Diamond"
        })
    {
    }
};

// ============================================================================
// ChannelModel
// ============================================================================

// ============================================================================
// ChannelModel
// ============================================================================
class MainComponent::ChannelModel
    : public juce::TableListBoxModel
{
public:
    ChannelModel()
    {
        lastProgram.fill(-1);
        lastEventCounter.fill(0);
        eventTimes.fill(Clock::now());
    }

    int getNumRows() override
    {
        return 16;
    }

    void setSong(std::shared_ptr<const Song> newSong)
    {
        song = std::move(newSong);

        lastProgram.fill(-1);

        if (!song)
            return;

        int programChangeCount = 0;

        for (const auto& event : song->playbackEvents)
        {
            if (event.bytes.size() < 2)
                continue;

            const auto status = event.bytes[0];

            // MIDI Program Change = 0xCn
            if ((status & 0xF0) != 0xC0)
                continue;

            const int channel = status & 0x0F;
            const int program = event.bytes[1] & 0x7F;

            if (channel >= 0 && channel < 16)
            {
                lastProgram[
                    static_cast<std::size_t>(channel)] =
                    program;

                ++programChangeCount;

                DBG("Program Change: channel "
                    + juce::String(channel + 1)
                    + " program "
                    + juce::String(program)
                    + " = "
                    + juce::String(getGMProgramName(program)));
            }
        }

        DBG("Total Program Changes: "
            + juce::String(programChangeCount));

        lastEventCounter.fill(0);
        eventTimes.fill(Clock::now());
    }

    void setSynthEngine(FluidSynthEngine* engine)
    {
        synthEngine = engine;

        lastEventCounter.fill(0);
        eventTimes.fill(Clock::now());
    }

    void paintRowBackground(
        juce::Graphics& g,
        int rowNumber,
        int width,
        int height,
        bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= 16)
            return;

        const auto index =
            static_cast<std::size_t>(rowNumber);

        // ------------------------------------------------------------
        // Kijk of er sinds de vorige repaint een MIDI-event is geweest.
        // ------------------------------------------------------------

        if (synthEngine != nullptr)
        {
            const auto state =
                synthEngine->getChannelState(rowNumber);

            if (state.eventCounter != lastEventCounter[index])
            {
                lastEventCounter[index] =
                    state.eventCounter;

                eventTimes[index] = Clock::now();
            }
        }

        // ------------------------------------------------------------
        // Normale achtergrondkleur
        // ------------------------------------------------------------

        const juce::Colour originalColour =
            rowIsSelected
                ? merikBlue
                : (rowNumber % 2 == 0
                    ? juce::Colour::fromRGB(47, 47, 47)
                    : juce::Colour::fromRGB(39, 39, 39));

        juce::Colour colour = originalColour;

        // ------------------------------------------------------------
        // 2 seconden blauw terugfaden
        // ------------------------------------------------------------

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - eventTimes[index]);

        constexpr auto fadeDuration =
            std::chrono::milliseconds { 2000 };

        if (elapsed.count() >= 0 &&
            elapsed < fadeDuration)
        {
            const float remaining =
                1.0f -
                static_cast<float>(elapsed.count()) /
                    static_cast<float>(fadeDuration.count());

            colour =
                originalColour.interpolatedWith(
                    merikBlue,
                    remaining);
        }

        g.setColour(colour);
        g.fillRect(0, 0, width, height);
    }

    void paintCell(
        juce::Graphics& g,
        int rowNumber,
        int columnId,
        int width,
        int height,
        bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= 16)
            return;

        juce::String value;

        switch (columnId)
        {
            // --------------------------------------------------------
            // Active
            // --------------------------------------------------------

            case 1:
                value = "✓";
                break;

            // --------------------------------------------------------
            // Channel
            // --------------------------------------------------------

            case 2:
                value = juce::String(rowNumber + 1);
                break;

            // --------------------------------------------------------
            // CC7
            // --------------------------------------------------------

            case 3:
            {
                if (synthEngine != nullptr)
                {
                    const auto state =
                        synthEngine->getChannelState(rowNumber);

                    value = juce::String(state.cc7);
                }
                else
                {
                    value = "-";
                }

                break;
            }

            // --------------------------------------------------------
            // CC11
            // --------------------------------------------------------

            case 4:
            {
                if (synthEngine != nullptr)
                {
                    const auto state =
                        synthEngine->getChannelState(rowNumber);

                    value = juce::String(state.cc11);
                }
                else
                {
                    value = "-";
                }

                break;
            }

            // --------------------------------------------------------
            // Program
            // --------------------------------------------------------

            case 5:
            {
                int program = -1;

                if (synthEngine != nullptr)
                {
                    const auto state =
                        synthEngine->getChannelState(rowNumber);

                    program = state.program;
                }

                if (program < 0)
                {
                    program =
                        lastProgram[
                            static_cast<std::size_t>(rowNumber)];
                }

                if (program >= 0 && program < 128)
                {
                    value =
                        juce::String(program + 1)
                        + " "
                        + getGMProgramName(program);
                }
                else
                {
                    value = "-";
                }

                break;
            }

            // --------------------------------------------------------
            // Family
            // --------------------------------------------------------

            case 6:
            {
                if (synthEngine != nullptr)
                {
                    const auto state =
                        synthEngine->getChannelState(rowNumber);

                    if (state.family >= 0)
                    {
                        value =
                            juce::String(state.family + 1);
                    }
                    else
                    {
                        value = "-";
                    }
                }
                else
                {
                    value = "-";
                }

                break;
            }

            default:
                return;
        }

        g.setColour(
            rowIsSelected
                ? juce::Colours::white
                : juce::Colour::fromRGB(210, 210, 210));

        g.setFont(
            juce::FontOptions(12.0f));

        g.drawText(
            value,
            6,
            0,
            width - 12,
            height,
            juce::Justification::centredLeft,
            true);
    }

private:
    using Clock = std::chrono::steady_clock;

    std::shared_ptr<const Song> song;

    std::array<int, 16> lastProgram {};

    FluidSynthEngine* synthEngine = nullptr;

    std::array<std::uint64_t, 16> lastEventCounter {};
    std::array<Clock::time_point, 16> eventTimes {};

    static const char* getGMProgramName(int program)
    {
        static constexpr const char* names[128] =
        {
            "Acoustic Grand Piano",
            "Bright Acoustic Piano",
            "Electric Grand Piano",
            "Honky-tonk Piano",
            "Electric Piano 1",
            "Electric Piano 2",
            "Harpsichord",
            "Clavinet",
            "Celesta",
            "Glockenspiel",
            "Music Box",
            "Vibraphone",
            "Marimba",
            "Xylophone",
            "Tubular Bells",
            "Dulcimer",

            "Drawbar Organ",
            "Percussive Organ",
            "Rock Organ",
            "Church Organ",
            "Reed Organ",
            "Accordion",
            "Harmonica",
            "Tango Accordion",

            "Acoustic Guitar (nylon)",
            "Acoustic Guitar (steel)",
            "Electric Guitar (jazz)",
            "Electric Guitar (clean)",
            "Electric Guitar (muted)",
            "Overdriven Guitar",
            "Distortion Guitar",
            "Guitar Harmonics",

            "Acoustic Bass",
            "Electric Bass (finger)",
            "Electric Bass (pick)",
            "Fretless Bass",
            "Slap Bass 1",
            "Slap Bass 2",
            "Synth Bass 1",
            "Synth Bass 2",

            "Violin",
            "Viola",
            "Cello",
            "Contrabass",
            "Tremolo Strings",
            "Pizzicato Strings",
            "Orchestral Harp",
            "Timpani",

            "String Ensemble 1",
            "String Ensemble 2",
            "Synth Strings 1",
            "Synth Strings 2",
            "Choir Aahs",
            "Voice Oohs",
            "Synth Choir",
            "Orchestra Hit",

            "Trumpet",
            "Trombone",
            "Tuba",
            "Muted Trumpet",
            "French Horn",
            "Brass Section",
            "Synth Brass 1",
            "Synth Brass 2",

            "Soprano Sax",
            "Alto Sax",
            "Tenor Sax",
            "Baritone Sax",
            "Oboe",
            "English Horn",
            "Bassoon",
            "Clarinet",

            "Piccolo",
            "Flute",
            "Recorder",
            "Pan Flute",
            "Blown Bottle",
            "Shakuhachi",
            "Whistle",
            "Ocarina",

            "Lead 1 (square)",
            "Lead 2 (sawtooth)",
            "Lead 3 (calliope)",
            "Lead 4 (chiff)",
            "Lead 5 (charang)",
            "Lead 6 (voice)",
            "Lead 7 (fifths)",
            "Lead 8 (bass + lead)",

            "Pad 1 (new age)",
            "Pad 2 (warm)",
            "Pad 3 (polysynth)",
            "Pad 4 (warm)",
            "Pad 5 (bowed)",
            "Pad 6 (metallic)",
            "Pad 7 (halo)",
            "Pad 8 (sweep)",

            "FX 1 (rain)",
            "FX 2 (soundtrack)",
            "FX 3 (crystal)",
            "FX 4 (atmosphere)",
            "FX 5 (brightness)",
            "FX 6 (goblins)",
            "FX 7 (echoes)",
            "FX 8 (sci-fi)",

            "Sitar",
            "Banjo",
            "Shamisen",
            "Koto",
            "Kalimba",
            "Bagpipe",
            "Fiddle",
            "Shanai",

            "Tinkle Bell",
            "Agogo",
            "Steel Drums",
            "Woodblock",
            "Taiko Drum",
            "Melodic Tom",
            "Synth Drum",
            "Reverse Cymbal",

            "Guitar Fret Noise",
            "Breath Noise",
            "Seashore",
            "Bird Tweet",
            "Telephone Ring",
            "Helicopter",
            "Applause",
            "Gunshot"
        };

        if (program < 0 || program >= 128)
            return "";

        return names[program];
    }
};

void MainComponent::updateChannelModel()
{
    if (channelModel != nullptr)
        channelModel->setSong(currentSong);

    channelTable.updateContent();
}

void MainComponent::paintOverChildren(juce::Graphics& g)
{
    for (const auto& block : familyBlockBounds)
    {
        auto r = block.reduced(1, 1);

        g.setColour(familyBorderColour);

        g.drawRoundedRectangle(
            r.toFloat(),
            1.0f, // radius
            4.0f);
    }
}


void MainComponent::timerCallback()
{
    updateTransportDisplay();
    channelTable.repaint();
    webServer.setPositionSamples(synthEngine.positionSamples());
}
// ============================================================================
// MainComponent
// ============================================================================

MainComponent::MainComponent()
{
    setOpaque(true);
    setLookAndFeel(&merikLookAndFeel);

    juce::PropertiesFile::Options options;
    options.applicationName     = "Merik2";
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName          = "Merik2";
    options.storageFormat       = juce::PropertiesFile::storeAsXML;

    appProperties.setStorageParameters(options);

    setOpaque(true);
    setLookAndFeel(&merikLookAndFeel);

    webServer.start(8080);

    // -------------------------------------------------------------------------
    // MIDI Control
    // -------------------------------------------------------------------------

    midiControlLabel.setText(
        "MIDI Control",
        juce::dontSendNotification);

    midiControlLabel.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    addAndMakeVisible(midiControlLabel);

    midiControlBox.addItem("Default", 1);
    midiControlBox.addItem("None", 2);
    midiControlBox.setSelectedId(1);

    addAndMakeVisible(midiControlBox);

    // -------------------------------------------------------------------------
    // MIDI Out
    // -------------------------------------------------------------------------

    midiOutLabel.setText(
        "MIDI Out",
        juce::dontSendNotification);

    midiOutLabel.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    addAndMakeVisible(midiOutLabel);

    midiOutBox.addItem("Default", 1);
    midiOutBox.addItem("None", 2);
    midiOutBox.setSelectedId(1);

    addAndMakeVisible(midiOutBox);

    // -------------------------------------------------------------------------
    // SoundFont
    // -------------------------------------------------------------------------

    soundFontLabel.setText(
        "SoundFont",
        juce::dontSendNotification);

    soundFontLabel.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    addAndMakeVisible(soundFontLabel);

    soundFontButton.setButtonText(
        "Select SoundFont...");

    soundFontButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    soundFontButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    soundFontButton.onClick = [this]
    {
        selectSoundFont();
    };

    addAndMakeVisible(soundFontButton);

    settingsButton.setButtonText("Settings");
    settingsButton.setTooltip("Audio Settings");

    settingsButton.setColour(
        juce::TextButton::buttonColourId,
        merikBlue);

    settingsButton.setColour(
        juce::TextButton::buttonOnColourId,
        merikBlue);

    settingsButton.setColour(
        juce::TextButton::textColourOffId,
        juce::Colours::white);

    settingsButton.setColour(
        juce::TextButton::textColourOnId,
        juce::Colours::white);

    settingsButton.onClick = [this]
    {
        showAudioSettings();
    };

    addAndMakeVisible(settingsButton);

    // -------------------------------------------------------------------------
    // Family controls
    // -------------------------------------------------------------------------

    const std::array<juce::String, 8> familyNames =
    {
        "1 Drums CH10",
        "2 Bass",
        "3 Guitars",
        "4 Keys",
        "5 Strings",
        "6 Winds",
        "7 FX",
        "8 Other"
    };

    for (std::size_t i = 0; i < familyControls.size(); ++i)
    {
        auto& control = familyControls[i];

        // Family name / mute button
        control.labelButton.setButtonText(familyNames[i]);

        control.labelButton.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour(45, 45, 45));

        control.labelButton.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour(65, 65, 65));

        control.labelButton.setColour(
            juce::TextButton::textColourOffId,
            juce::Colours::white);

        control.labelButton.setColour(
            juce::TextButton::textColourOnId,
            juce::Colours::white);

        control.labelButton.setClickingTogglesState(true);

        control.labelButton.setConnectedEdges(
            juce::Button::ConnectedOnLeft |
            juce::Button::ConnectedOnRight);

        addAndMakeVisible(control.labelButton);

        // Numerical value
        control.valueLabel.setText(
            "100",
            juce::dontSendNotification);

        control.valueLabel.setColour(
            juce::Label::textColourId,
            secondaryTextColour);

        control.valueLabel.setFont(
            juce::FontOptions(11.0f));

        control.valueLabel.setJustificationType(
            juce::Justification::centred);

        addAndMakeVisible(control.valueLabel);

        // Slider
        control.slider.setSliderStyle(
            juce::Slider::LinearHorizontal);

        // Belangrijk: geen waarde/textbox van de Slider zelf
        control.slider.setTextBoxStyle(
            juce::Slider::NoTextBox,
            false,
            0,
            0);

        control.slider.setRange(0.0, 127.0, 1.0);

        control.slider.setValue(
            100.0,
            juce::dontSendNotification);

        control.slider.setColour(
            juce::Slider::trackColourId,
            merikBlue);

        control.slider.setColour(
            juce::Slider::thumbColourId,
            juce::Colours::white);

        control.slider.setColour(
            juce::Slider::backgroundColourId,
            juce::Colour(70, 70, 70));

        control.slider.onValueChange = [this, i]
        {
            auto& family = familyControls[i];

            family.valueLabel.setText(
                juce::String(
                    juce::roundToInt(
                        family.slider.getValue())),
                juce::dontSendNotification);
        };

        addAndMakeVisible(control.slider);
    }
    
    // -------------------------------------------------------------------------
    // Player
    // -------------------------------------------------------------------------

    songTitle.setText(
        "No MIDI loaded",
        juce::dontSendNotification);

    songTitle.setColour(
        juce::Label::textColourId,
        textColour);

    songTitle.setFont(
        juce::Font(
            juce::FontOptions{}
                .withName("SF Pro Display")
                .withHeight(47.0f)));

    songTitle.setJustificationType(
        juce::Justification::centred);

    addAndMakeVisible(songTitle);

    nextSong.setText(
        "Next: -",
        juce::dontSendNotification);

    nextSong.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    nextSong.setFont(
        juce::FontOptions(14.0f));

    nextSong.setJustificationType(
        juce::Justification::centred);

    addAndMakeVisible(nextSong);

    elapsedTime.setText(
        "00:00",
        juce::dontSendNotification);

    elapsedTime.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    elapsedTime.setFont(
        juce::FontOptions(12.0f));

    addAndMakeVisible(elapsedTime);

    totalTime.setText(
        "00:00",
        juce::dontSendNotification);

    totalTime.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    totalTime.setJustificationType(
        juce::Justification::centredRight);

    addAndMakeVisible(totalTime);

    positionSlider.setSliderStyle(
        juce::Slider::LinearHorizontal);

    positionSlider.setRange(
        0.0,
        1.0,
        0.001);

    positionSlider.setValue(0.0);

    positionSlider.setColour(
        juce::Slider::trackColourId,
        merikBlue);

    positionSlider.setColour(
        juce::Slider::thumbColourId,
        juce::Colours::white);

    positionSlider.setColour(
        juce::Slider::backgroundColourId,
        juce::Colour::fromRGB(70, 70, 70));

    addAndMakeVisible(positionSlider);

    // -------------------------------------------------------------------------
    // Transport
    // -------------------------------------------------------------------------

    loadButton.setButtonText("Load MIDI");

    loadButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    loadButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    loadButton.onClick = [this]
    {
        loadMidi();
    };

    addAndMakeVisible(loadButton);

    playButton.setButtonText("Play");

    playButton.setColour(
        juce::TextButton::buttonColourId,
        merikBlue);

    playButton.setColour(
        juce::TextButton::textColourOffId,
        juce::Colours::white);

    playButton.onClick = [this]
    {
        play();
    };

    addAndMakeVisible(playButton);

    stopButton.setButtonText("Stop");

    stopButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    stopButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    stopButton.onClick = [this]
    {
        stop();
    };

    addAndMakeVisible(stopButton);

    nextButton.setButtonText("Next");

    nextButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    nextButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    addAndMakeVisible(nextButton);

    transposeDownButton.setButtonText("-");

    transposeDownButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    transposeDownButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    transposeDownButton.onClick = [this]
    {
        --transpose;

        transposeValue.setText(
            juce::String(transpose),
            juce::dontSendNotification);
    };

    addAndMakeVisible(transposeDownButton);

    transposeValue.setText(
        "0",
        juce::dontSendNotification);

    transposeValue.setColour(
        juce::Label::textColourId,
        textColour);

    transposeValue.setFont(
        juce::FontOptions(14.0f));

    transposeValue.setJustificationType(
        juce::Justification::centred);

    addAndMakeVisible(transposeValue);

    transposeUpButton.setButtonText("+");

    transposeUpButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    transposeUpButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    transposeUpButton.onClick = [this]
    {
        ++transpose;

        transposeValue.setText(
            juce::String(transpose),
            juce::dontSendNotification);
    };

    addAndMakeVisible(transposeUpButton);

    panicButton.setButtonText("Panic");

    panicButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    panicButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    addAndMakeVisible(panicButton);

    lyricsButton.setButtonText("Lyrics");

    lyricsButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    lyricsButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    addAndMakeVisible(lyricsButton);

    channelsButton.setButtonText("Channels");

    channelsButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    channelsButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    addAndMakeVisible(channelsButton);

    // -------------------------------------------------------------------------
    // Lists
    // -------------------------------------------------------------------------

    setlistsTitle.setText(
        "Setlists",
        juce::dontSendNotification);

    setlistsTitle.setColour(
        juce::Label::textColourId,
        textColour);

    setlistsTitle.setFont(
        juce::FontOptions(14.0f));

    addAndMakeVisible(setlistsTitle);

    songsTitle.setText(
        "Songs",
        juce::dontSendNotification);

    songsTitle.setColour(
        juce::Label::textColourId,
        textColour);

    songsTitle.setFont(
        juce::FontOptions(14.0f));

    addAndMakeVisible(songsTitle);

    totalTimeTitle.setText(
        "Total time",
        juce::dontSendNotification);

    totalTimeTitle.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    addAndMakeVisible(totalTimeTitle);

    setlistModel = std::make_unique<SetlistModel>();
    songModel = std::make_unique<SongModel>();

    setlistBox.setModel(setlistModel.get());
    songBox.setModel(songModel.get());

    setlistBox.setColour(
        juce::ListBox::backgroundColourId,
        panelColour);

    songBox.setColour(
        juce::ListBox::backgroundColourId,
        panelColour);

    addAndMakeVisible(setlistBox);
    addAndMakeVisible(songBox);

    newSetlistButton.setButtonText("+ Setlist");
    deleteSetlistButton.setButtonText("− Setlist");

    addButton.setButtonText("+");
    removeButton.setButtonText("−");

    for (auto* button :
         {
             &newSetlistButton,
             &deleteSetlistButton,
             &addButton,
             &removeButton
         })
    {
        button->setColour(
            juce::TextButton::buttonColourId,
            panelLightColour);

        button->setColour(
            juce::TextButton::textColourOffId,
            textColour);

        addAndMakeVisible(button);
    }

    doubleClickToggle.setButtonText(
        "Dbl Click Plays");

    normalizeToggle.setButtonText(
        "Normalize");

    continuousToggle.setButtonText(
        "Continuous Play");

    for (auto* toggle :
         {
             &doubleClickToggle,
             &normalizeToggle,
             &continuousToggle
         })
    {
        toggle->setColour(
            juce::ToggleButton::textColourId,
            secondaryTextColour);

        toggle->setColour(
            juce::ToggleButton::tickColourId,
            merikBlue);

        addAndMakeVisible(toggle);
    }

    // -------------------------------------------------------------------------
    // Channel table
    // -------------------------------------------------------------------------

    channelModel =
        std::make_unique<ChannelModel>();
    channelModel->setSynthEngine(&synthEngine);

    channelTable.setModel(
        channelModel.get());

    channelTable.getHeader().addColumn(
        "Active", 1, 55);

    channelTable.getHeader().addColumn(
        "CH", 2, 45);

    channelTable.getHeader().addColumn(
        "CC7 Volume", 3, 110);

    channelTable.getHeader().addColumn(
        "CC11 Expression", 4, 125);

    channelTable.getHeader().addColumn(
        "Program", 5, 210);

    channelTable.getHeader().addColumn(
        "Family 11", 6, 110);

    channelTable.setColour(
        juce::ListBox::backgroundColourId,
        panelColour);

    channelTable.setColour(
        juce::ListBox::outlineColourId,
        juce::Colour::fromRGB(65, 65, 65));

    channelTable.setRowHeight(18);

    addAndMakeVisible(channelTable);

    // -------------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------------

    setAudioChannels(0, 2);

    // Restore last SoundFont
    if (auto* properties = appProperties.getUserSettings())
    {
        const auto path =
            properties->getValue("soundFontPath");

        if (path.isNotEmpty())
        {
            const juce::File file(path);

            if (file.existsAsFile())
            {
                std::string error;

                if (synthEngine.loadSoundFont(
                        file.getFullPathName().toStdString(),
                        error))
                {
                    currentSoundFont = file;

                    auto name =
                        currentSoundFont.getFileName();

                    if (name.length() > 27)
                        name = name.substring(0, 24) + "...";

                    soundFontButton.setButtonText(name);
                }
                else
                {
                    DBG("Could not restore SoundFont: "
                        + juce::String(error));
                }
            }
        }
    }

    startTimerHz(20);

    setSize(1100, 700);
}

// ============================================================================
// Destructor
// ============================================================================

MainComponent::~MainComponent()
{
    stopTimer();

    synthEngine.stop();

    shutdownAudio();
}

// ============================================================================
// Painting
// ============================================================================

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    auto bounds = getLocalBounds();

    // Header now contains:
    // row 1 = MIDI Control / MIDI Out / SoundFont
    // row 2 = family controls

    auto header =
        bounds.removeFromTop(150);

    g.setColour(panelColour);
    g.fillRect(header);

    g.setColour(
        juce::Colour::fromRGB(65, 65, 65));

    g.fillRect(
        0,
        header.getBottom() - 1,
        getWidth(),
        1);

    auto player =
        bounds.removeFromTop(205);

    g.setColour(panelColour);
    g.fillRect(player);

    g.setColour(
        juce::Colour::fromRGB(65, 65, 65));

    g.fillRect(
        0,
        player.getBottom() - 1,
        getWidth(),
        1);

    auto lists =
        bounds.removeFromTop(215);

    g.setColour(panelColour);
    g.fillRect(lists);

    g.setColour(
        juce::Colour::fromRGB(65, 65, 65));

    g.fillRect(
        lists.getCentreX(),
        lists.getY(),
        1,
        lists.getHeight());

    g.setColour(backgroundColour);
    g.fillRect(bounds);
}

// ============================================================================
// Layout
// ============================================================================

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    // -------------------------------------------------------------------------
    // Header
    // -------------------------------------------------------------------------

    auto header =
        bounds.removeFromTop(150).reduced(10);

    // First row: MIDI Control / MIDI Out / SoundFont / Settings

    auto topRow =
        header.removeFromTop(50);

    auto midiControlArea =
        topRow.removeFromLeft(155);

    midiControlLabel.setBounds(
        midiControlArea.removeFromTop(18));

    midiControlBox.setBounds(
        midiControlArea.removeFromTop(28));

    topRow.removeFromLeft(12);

    auto midiOutArea =
        topRow.removeFromLeft(155);

    midiOutLabel.setBounds(
        midiOutArea.removeFromTop(18));

    midiOutBox.setBounds(
        midiOutArea.removeFromTop(28));

    topRow.removeFromLeft(12);

    auto soundFontArea =
        topRow.removeFromLeft(285);

    soundFontLabel.setBounds(
        soundFontArea.removeFromTop(18));

    auto soundFontRow =
        soundFontArea.removeFromTop(28);

    soundFontButton.setBounds(
        soundFontRow.removeFromLeft(190));

    soundFontRow.removeFromLeft(5);

    settingsButton.setBounds(
        soundFontRow.removeFromLeft(90));

    // Second row: family controls

    header.removeFromTop(5);

    auto familyArea = header;

    const int familyGap = 6;
    const int familyWidth =
        (familyArea.getWidth() - familyGap * 7) / 8;

    for (std::size_t i = 0; i < familyControls.size(); ++i)
    {
        auto area = familyArea.removeFromLeft(familyWidth);

        familyBlockBounds[i] = area;

        auto& control = familyControls[i];

        control.labelButton.setBounds(
            area.removeFromTop(22));

        area.removeFromTop(5);
        control.valueLabel.setBounds(
            area.removeFromTop(18));

        area.removeFromTop(-10);

        control.slider.setBounds(
            area.reduced(4, 0));

        if (i < familyControls.size() - 1)
            familyArea.removeFromLeft(familyGap);
    }

    // -------------------------------------------------------------------------
    // Player
    // -------------------------------------------------------------------------

    auto player =
        bounds.removeFromTop(205).reduced(12);

    auto titleArea =
        player.removeFromTop(55);

    songTitle.setBounds(titleArea);

    nextSong.setBounds(
        player.removeFromTop(25));

    auto progressArea =
        player.removeFromTop(38);

    elapsedTime.setBounds(
        progressArea.removeFromLeft(45));

    totalTime.setBounds(
        progressArea.removeFromRight(45));

    positionSlider.setBounds(
        progressArea.reduced(5, 8));

    auto transport =
        player.removeFromTop(42);

    loadButton.setBounds(
        transport.removeFromLeft(95));

    transport.removeFromLeft(6);

    playButton.setBounds(
        transport.removeFromLeft(90));

    transport.removeFromLeft(6);

    stopButton.setBounds(
        transport.removeFromLeft(85));

    transport.removeFromLeft(6);

    nextButton.setBounds(
        transport.removeFromLeft(70));

    transport.removeFromLeft(12);

    transposeDownButton.setBounds(
        transport.removeFromLeft(32));

    transposeValue.setBounds(
        transport.removeFromLeft(35));

    transposeUpButton.setBounds(
        transport.removeFromLeft(32));

    transport.removeFromLeft(12);

    panicButton.setBounds(
        transport.removeFromLeft(65));

    transport.removeFromLeft(6);

    lyricsButton.setBounds(
        transport.removeFromLeft(65));

    transport.removeFromLeft(6);

    channelsButton.setBounds(
        transport.removeFromLeft(75));

    // -------------------------------------------------------------------------
    // Lists
    // -------------------------------------------------------------------------

    auto lists =
        bounds.removeFromTop(215).reduced(10);

    auto setlistArea =
        lists.removeFromLeft(
            juce::jmax(
                250,
                lists.getWidth() / 3));

    auto songArea = lists;

    setlistsTitle.setBounds(
        setlistArea.removeFromTop(25));

    auto setlistButtons =
        setlistArea.removeFromBottom(30);

    newSetlistButton.setBounds(
        setlistButtons.removeFromLeft(85));

    setlistButtons.removeFromLeft(5);

    deleteSetlistButton.setBounds(
        setlistButtons.removeFromLeft(95));

    setlistBox.setBounds(
        setlistArea);

    songsTitle.setBounds(
        songArea.removeFromTop(25));

    auto songButtons =
        songArea.removeFromBottom(30);

    addButton.setBounds(
        songButtons.removeFromLeft(35));

    songButtons.removeFromLeft(5);

    removeButton.setBounds(
        songButtons.removeFromLeft(35));

    songButtons.removeFromLeft(15);

    doubleClickToggle.setBounds(
        songButtons.removeFromLeft(120));

    normalizeToggle.setBounds(
        songButtons.removeFromLeft(95));

    continuousToggle.setBounds(
        songButtons.removeFromLeft(120));

    songBox.setBounds(
        songArea);

    // -------------------------------------------------------------------------
    // Channel table
    // -------------------------------------------------------------------------

    auto channelArea =
        bounds.reduced(0);

    channelTable.setBounds(
        channelArea);
}