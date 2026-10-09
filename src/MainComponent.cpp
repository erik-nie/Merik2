#include "MainComponent.h"
#include "WebServer.h"
#include "BinaryData.h"
#include <functional>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace
{
    const juce::Colour backgroundColour =
        juce::Colour::fromRGB(25, 25, 25);

    const juce::Colour panelColour =
        juce::Colour::fromRGB(38, 38, 38);

    const juce::Colour panelDarkColour =
        juce::Colour::fromRGB(28, 28, 28);

        const juce::Colour panelLightColour =
        juce::Colour::fromRGB(58, 58, 58);

    const juce::Colour merikBlue =
        juce::Colour::fromRGB(0, 100, 255);

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
        notoSansTypeface =
            juce::Typeface::createSystemTypefaceFor(
                BinaryData::NotoSansRegular_ttf,
                BinaryData::NotoSansRegular_ttfSize);
        notoSansMediumTypeface =
            juce::Typeface::createSystemTypefaceFor(
                BinaryData::NotoSansMedium_ttf,
                BinaryData::NotoSansMedium_ttfSize);

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
        int) override
    {
        return getMediumFont(20.0f);
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

    void drawTableHeaderColumn(
        juce::Graphics& g,
        juce::TableHeaderComponent& header,
        const juce::String& columnName,
        int columnId,
        int width,
        int height,
        bool isMouseOver,
        bool isMouseDown,
        int columnFlags) override
    {
        juce::ignoreUnused(
            header,
            columnId,
            isMouseOver,
            isMouseDown,
            columnFlags);

        g.setColour(juce::Colour::fromRGB(75, 75, 75));
        g.fillRect(0, 0, width, height);

        g.setColour(juce::Colour::fromRGB(110, 110, 110));
        g.fillRect(width - 1, 0, 1, height);

        g.setColour(juce::Colours::white);
        g.setFont(getMediumFont(15.0f));

        g.drawText(
            columnName,
            6,
            0,
            width - 12,
            height,
            juce::Justification::centredLeft,
            true);
    }

    void drawTableHeaderBackground(
        juce::Graphics& g,
        juce::TableHeaderComponent& header) override
    {
        juce::ignoreUnused(header);

        g.setColour(juce::Colour::fromRGB(75, 75, 75));
        g.fillAll();
    }

    void drawButtonBackground(
        juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(
            shouldDrawButtonAsHighlighted,
            shouldDrawButtonAsDown);

        auto bounds =
            button.getLocalBounds().toFloat().reduced(0.5f);

        const bool isOn =
            button.getToggleState();

        // Actieve/ingeschakelde knop = MerikBlue
        // Normale knop = één egale donkere kleur
        const auto colour =
            isOn
                ? merikBlue
                : panelLightColour;

        g.setColour(colour);

        g.fillRoundedRectangle(
            bounds,
            7.0f);
    }

    juce::Typeface::Ptr getNotoSansTypeface() const
    {
        return notoSansTypeface;
    }

    juce::Font getMediumFont(float height) const
    {
        return juce::Font(
            juce::FontOptions(notoSansMediumTypeface)
                .withHeight(height));
    }

    juce::Font getMerikFont(float height)
    {
        return makeFont(height);
    }

    private:
        juce::Typeface::Ptr notoSansTypeface;
        juce::Typeface::Ptr notoSansMediumTypeface;

        juce::Font makeFont(float height) const
        {
            return juce::Font(
                juce::FontOptions(notoSansTypeface)
                    .withHeight(height));
        }
};

MerikLookAndFeel merikLookAndFeel;

static juce::Font makeNotoFont(float height)
{
    return juce::Font(
        juce::FontOptions(
            merikLookAndFeel.getNotoSansTypeface())
            .withHeight(height));
}


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
        g.setFont(makeNotoFont(13.0f));

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
// ============================================================================
// SetlistModel
// ============================================================================

class MainComponent::SetlistModel
    : public juce::ListBoxModel
{
public:
    SetlistModel(
        std::vector<MainComponent::SetlistData>* setlistsToUse,
        std::function<void(int)> selectionCallbackToUse)
        : setlists(setlistsToUse),
          selectionCallback(std::move(selectionCallbackToUse))
    {
    }

    int getNumRows() override
    {
        if (setlists == nullptr)
            return 0;

        return static_cast<int>(setlists->size());
    }

    void paintListBoxItem(
        int rowNumber,
        juce::Graphics& g,
        int width,
        int height,
        bool rowIsSelected) override
    {
        if (setlists == nullptr ||
            rowNumber < 0 ||
            rowNumber >= static_cast<int>(setlists->size()))
        {
            return;
        }

        if (rowIsSelected)
        {
            g.setColour(merikBlue);
            g.fillAll();
        }
        else if ((rowNumber & 1) != 0)
        {
            g.setColour(panelDarkColour);
            g.fillAll();
        }

        g.setColour(textColour);
        g.setFont(makeNotoFont(15.0f));

        g.drawText(
            (*setlists)[static_cast<std::size_t>(rowNumber)].name,
            10,
            0,
            width - 20,
            height,
            juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        if (selectionCallback)
            selectionCallback(lastRowSelected);
    }

private:
    std::vector<MainComponent::SetlistData>* setlists = nullptr;

    std::function<void(int)> selectionCallback;
};


// ============================================================================
// SongModel
// ============================================================================

class MainComponent::SongModel
    : public juce::ListBoxModel
{
public:
    SongModel(
        std::vector<MainComponent::SetlistData>* setlistsToUse,
        int* selectedSetlistToUse)
        : setlists(setlistsToUse),
          selectedSetlist(selectedSetlistToUse)
    {
    }

    int getNumRows() override
    {
        auto* songs = getCurrentSongs();

        if (songs == nullptr)
            return 0;

        return static_cast<int>(songs->size());
    }

    void paintListBoxItem(
        int rowNumber,
        juce::Graphics& g,
        int width,
        int height,
        bool rowIsSelected) override
    {
        auto* songs = getCurrentSongs();

        if (songs == nullptr ||
            rowNumber < 0 ||
            rowNumber >= static_cast<int>(songs->size()))
        {
            return;
        }

        if (rowIsSelected)
        {
            g.setColour(merikBlue);
            g.fillAll();
        }
        else if ((rowNumber & 1) != 0)
        {
            g.setColour(panelDarkColour);
            g.fillAll();
        }

        g.setColour(textColour);
        g.setFont(makeNotoFont(15.0f));

        const auto number =
            juce::String(rowNumber + 1);

        g.drawText(
            number,
            10,
            0,
            30,
            height,
            juce::Justification::centredRight);

        g.drawText(
            (*songs)[static_cast<std::size_t>(rowNumber)].getFileName(),
            48,
            0,
            width - 58,
            height,
            juce::Justification::centredLeft);
    }

    bool addFile(const juce::File& file)
    {
        auto* songs = getCurrentSongs();

        if (songs == nullptr ||
            !file.existsAsFile())
        {
            return false;
        }

        const auto extension =
            file.getFileExtension().toLowerCase();

        if (extension != ".mid" &&
            extension != ".midi" &&
            extension != ".kar")
        {
            return false;
        }

        const auto alreadyExists =
            std::find_if(
                songs->begin(),
                songs->end(),
                [&file](const juce::File& existing)
                {
                    return existing == file;
                });

        if (alreadyExists != songs->end())
            return false;

        songs->push_back(file);
        return true;
    }

    bool removeFile(int row)
    {
        auto* songs = getCurrentSongs();

        if (songs == nullptr ||
            row < 0 ||
            row >= static_cast<int>(songs->size()))
        {
            return false;
        }

        songs->erase(
            songs->begin() + row);

        return true;
    }

    juce::File getFile(int row) const
    {
        const auto* songs = getCurrentSongs();

        if (songs == nullptr ||
            row < 0 ||
            row >= static_cast<int>(songs->size()))
        {
            return {};
        }

        return (*songs)[static_cast<std::size_t>(row)];
    }

    void setDoubleClickCallback(
        std::function<void(const juce::File&)> callbackToUse)
    {
        doubleClickCallback = std::move(callbackToUse);
    }

    void listBoxItemDoubleClicked(
        int row,
        const juce::MouseEvent&) override
    {
        if (!doubleClickCallback)
            return;

        const auto file = getFile(row);

        if (file != juce::File())
            doubleClickCallback(file);
    }

private:
    // BELANGRIJK:
    // Dit is een vector van SetlistData, niet van juce::File.
    std::vector<MainComponent::SetlistData>* setlists = nullptr;

    int* selectedSetlist = nullptr;

    std::function<void(const juce::File&)> doubleClickCallback;

    std::vector<juce::File>* getCurrentSongs()
    {
        if (setlists == nullptr ||
            selectedSetlist == nullptr)
        {
            return nullptr;
        }

        if (*selectedSetlist < 0 ||
            *selectedSetlist >=
                static_cast<int>(setlists->size()))
        {
            return nullptr;
        }

        return &(*setlists)
            [static_cast<std::size_t>(*selectedSetlist)]
            .songs;
    }

    const std::vector<juce::File>* getCurrentSongs() const
    {
        if (setlists == nullptr ||
            selectedSetlist == nullptr)
        {
            return nullptr;
        }

        if (*selectedSetlist < 0 ||
            *selectedSetlist >=
                static_cast<int>(setlists->size()))
        {
            return nullptr;
        }

        return &(*setlists)
            [static_cast<std::size_t>(*selectedSetlist)]
            .songs;
    }
};

namespace
{
juce::String familyName(int family)
{
    static const std::array<juce::String, 8> names =
    {
        "1 Drums",
        "2 Bass",
        "3 Guitars",
        "4 Keys",
        "5 Strings",
        "6 Winds",
        "7 FX",
        "8 Other"
    };

    return family >= 0 && family < static_cast<int>(names.size())
        ? names[static_cast<std::size_t>(family)]
        : "-";
}
}

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
        // Vaste donkere achtergrond
        // ------------------------------------------------------------

        const juce::Colour normalColour = panelDarkColour;

        const juce::Colour selectedColour =
            merikBlue;

        juce::Colour colour =
            rowIsSelected
                ? selectedColour
                : normalColour;

        // ------------------------------------------------------------
        // 2 seconden blauw terugfaden na MIDI-event
        // ------------------------------------------------------------

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - eventTimes[index]);

        constexpr auto fadeDuration =
            std::chrono::milliseconds { 2000 };

        if (!rowIsSelected &&
            elapsed.count() >= 0 &&
            elapsed < fadeDuration)
        {
            const float remaining =
                1.0f -
                static_cast<float>(elapsed.count()) /
                    static_cast<float>(fadeDuration.count());

            colour =
                normalColour.interpolatedWith(
                    merikBlue,
                    remaining);
        }

        // ------------------------------------------------------------
        // Rijachtergrond
        // ------------------------------------------------------------

        g.setColour(colour);
        g.fillRect(0, 0, width, height);

        // ------------------------------------------------------------
        // Dunne lichtgrijze scheidingslijn
        // ------------------------------------------------------------

        g.setColour(
            juce::Colour::fromRGB(45, 45, 45));

        g.fillRect(
            0,
            height - 1,
            width,
            1);
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

                    const int adjustedValue =
                        synthEngine->getAdjustedChannelVolume(rowNumber);

                    value = juce::String(state.cc7)
                        + " → "
                        + juce::String(adjustedValue);;
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
                            familyName(state.family);
                            //juce::String(state.family + 1);
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

        g.setFont(makeNotoFont(14.0f));

        g.drawText(
            value,
            6,
            0,
            width - 12,
            height,
            juce::Justification::centredLeft,
            true);

        // Verticale scheidingslijn
        g.setColour(
            juce::Colour::fromRGB(45, 45, 45));

        g.fillRect(
            width - 1,
            0,
            1,
            height);
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
            4.0f); // width
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

    webServer.setFamilyVolumeCallback(
        [this](int family, float factor)
        {
            synthEngine.setFamilyVolumeFactor(family, factor);
        });

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
        
        control.labelButton.setToggleState(true, juce::dontSendNotification);

        control.labelButton.onClick = [this, i]
        {
            const bool enabled =
                familyControls[i].labelButton.getToggleState();

            synthEngine.setFamilyEnabled(
                static_cast<int>(i),
                enabled);

            DBG(
                "Family "
                + juce::String(static_cast<int>(i))
                + (enabled ? " enabled" : " disabled"));
        };

        control.labelButton.setConnectedEdges(
            juce::Button::ConnectedOnLeft |
            juce::Button::ConnectedOnRight);

        addAndMakeVisible(control.labelButton);

        // Numerical value
        control.valueLabel.setText(
            "127",
            juce::dontSendNotification);

        control.valueLabel.setColour(
            juce::Label::textColourId,
            secondaryTextColour);

        control.valueLabel.setFont(makeNotoFont(12.0f));

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
            127.0,
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

            const int value =
                juce::roundToInt(
                    family.slider.getValue());

            family.valueLabel.setText(
                juce::String(value),
                juce::dontSendNotification);

            const float factor =
                static_cast<float>(value) / 127.0f;

            synthEngine.setFamilyVolumeFactor(
                static_cast<int>(i),
                factor);
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

    songTitle.setFont(makeNotoFont(60.0f));

    songTitle.setJustificationType(
        juce::Justification::centred);

    addAndMakeVisible(songTitle);

    nextSong.setText(
        "Next: -",
        juce::dontSendNotification);

    nextSong.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    nextSong.setFont(makeNotoFont(15.0f));

    nextSong.setJustificationType(
        juce::Justification::centred);

    addAndMakeVisible(nextSong);

    elapsedTime.setText(
        "00:00",
        juce::dontSendNotification);

    elapsedTime.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    elapsedTime.setFont(makeNotoFont(14.0f));

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

    positionSlider.setValue(
        0.0,
        juce::dontSendNotification);

    positionSlider.setTextBoxStyle(
        juce::Slider::NoTextBox,
        false,
        0,
        0);

    positionSlider.setColour(
        juce::Slider::trackColourId,
        merikBlue);

    positionSlider.setColour(
        juce::Slider::thumbColourId,
        juce::Colours::white);

    positionSlider.setColour(
        juce::Slider::backgroundColourId,
        juce::Colour::fromRGB(70, 70, 70));

    positionSlider.onDragStart = [this]
    {
        positionSliderWasPlaying =
            synthEngine.isPlaying();

        /*
            Stop tijdens het slepen. De slider zelf blijft bewegen,
            maar we doen pas een echte seek wanneer de gebruiker
            de muisknop loslaat.
        */
        synthEngine.stop();

        isPlaying = false;
    };

    positionSlider.onDragEnd = [this]
    {
        if (!currentSong)
            return;

        const auto lengthSamples =
            synthEngine.lengthSamples();

        if (lengthSamples <= 0)
            return;

        const double normalizedPosition =
            juce::jlimit(
                0.0,
                1.0,
                positionSlider.getValue());

        const auto targetSamples =
            static_cast<std::int64_t>(
                std::llround(
                    normalizedPosition *
                    static_cast<double>(lengthSamples)));

        synthEngine.seekSamples(targetSamples);

        if (positionSliderWasPlaying &&
            targetSamples < lengthSamples)
        {
            synthEngine.start();
            isPlaying = true;
        }
        else
        {
            isPlaying = false;
        }

        positionSliderWasPlaying = false;

        updateTransportDisplay();
    };

    addAndMakeVisible(positionSlider);    

    // -------------------------------------------------------------------------
    // Transport
    // -------------------------------------------------------------------------

    loadButton.setButtonText("Load");

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

    transposeValue.setFont(makeNotoFont(16.0f));

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

    lyricsButton.onClick = []
    {
        juce::URL("http://localhost:8080").launchInDefaultBrowser();
    };

    addAndMakeVisible(lyricsButton);

    channelsButton.setButtonText("Channels");

    channelsButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    channelsButton.setColour(
        juce::TextButton::buttonOnColourId,
        merikBlue);

    channelsButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    channelsButton.setColour(
        juce::TextButton::textColourOnId,
        juce::Colours::white);

    channelsButton.setClickingTogglesState(true);
    channelsButton.setToggleState(
        true,
        juce::dontSendNotification);

    channelsButton.onClick = [this]
    {
        channelTable.setVisible(
            channelsButton.getToggleState());

        resized();
    };

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
        merikLookAndFeel.getMediumFont(25.0f));

    setlistsTitle.setColour(
        juce::Label::backgroundColourId,
        juce::Colour::fromRGB(75, 75, 75));

    setlistsTitle.setJustificationType(
        juce::Justification::centredLeft);

    addAndMakeVisible(setlistsTitle);

    songsTitle.setText(
        "Songs",
        juce::dontSendNotification);

    songsTitle.setColour(
        juce::Label::textColourId,
        textColour);

    songsTitle.setFont(
        merikLookAndFeel.getMediumFont(25.0f));

    songsTitle.setColour(
        juce::Label::backgroundColourId,
        juce::Colour::fromRGB(75, 75, 75));

    songsTitle.setJustificationType(
        juce::Justification::centredLeft);

    addAndMakeVisible(songsTitle);

    totalTimeTitle.setText(
        "Total time",
        juce::dontSendNotification);

    totalTimeTitle.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    addAndMakeVisible(totalTimeTitle);


    doubleClickToggle.setButtonText(
        "Dbl Click Plays");

    normalizeToggle.setButtonText(
        "Normalize");

    continuousToggle.setButtonText(
        "Continuous Play");

    // Eerst opgeslagen toestand laden.
    // Daarna pas de ListBox-modellen maken.
    loadPersistentState();

    setlistModel =
        std::make_unique<SetlistModel>(
            &setlists,
            [this](int row)
            {
                if (row < 0 ||
                    row >= static_cast<int>(setlists.size()))
                {
                    return;
                }

                selectedSetlist = row;

                refreshSongModel();
                savePersistentState();
            });

    songModel =
        std::make_unique<SongModel>(
            &setlists,
            &selectedSetlist);

    setlistBox.setModel(setlistModel.get());
    songBox.setModel(songModel.get());

    refreshSetlistModel();
    refreshSongModel();

    songModel->setDoubleClickCallback(
        [this](const juce::File& file)
        {
            loadMidiFile(file);

            if (doubleClickToggle.getToggleState())
                play();
        });


    songModel->setDoubleClickCallback(
        [this](const juce::File& file)
        {
            loadMidiFile(file);

            if (doubleClickToggle.getToggleState())
                play();
        });

    setlistBox.setColour(
        juce::ListBox::backgroundColourId,
        panelDarkColour);

    songBox.setColour(
        juce::ListBox::backgroundColourId,
        panelDarkColour);



    addAndMakeVisible(setlistBox);
    addAndMakeVisible(songBox);

    newSetlistButton.setButtonText("+ Setlist");
    deleteSetlistButton.setButtonText("- Setlist");

    addButton.setButtonText("+");
    removeButton.setButtonText("-");

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
    newSetlistButton.onClick = [this]
    {
        int number = 1;

        while (true)
        {
            const auto candidate =
                "Setlist " + juce::String(number);

            const bool exists =
                std::any_of(
                    setlists.begin(),
                    setlists.end(),
                    [&candidate](const SetlistData& setlist)
                    {
                        return setlist.name == candidate;
                    });

            if (!exists)
            {
                setlists.push_back(
                    { candidate, {} });
                break;
            }

            ++number;
        }

        selectedSetlist =
            static_cast<int>(setlists.size()) - 1;

        refreshSetlistModel();
        refreshSongModel();

        savePersistentState();
    };

    deleteSetlistButton.onClick = [this]
    {
        if (setlists.size() <= 1)
            return;

        setlists.erase(
            setlists.begin() + selectedSetlist);

        selectedSetlist =
            juce::jlimit(
                0,
                static_cast<int>(setlists.size()) - 1,
                selectedSetlist);

        refreshSetlistModel();
        refreshSongModel();

        savePersistentState();
    };

    addButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Add MIDI files",
            juce::File(),
            "*.mid;*.midi;*.kar");

        constexpr int flags =
            juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectMultipleItems;

        chooser->launchAsync(
            flags,
            [this, chooser](const juce::FileChooser&)
            {
                const auto files = chooser->getResults();

                bool changed = false;

                for (const auto& file : files)
                {
                    if (songModel->addFile(file))
                        changed = true;
                }

                if (changed)
                {
                    refreshSongModel();
                    savePersistentState();
                }
            });
    };

    removeButton.onClick = [this]
    {
        const int row =
            songBox.getSelectedRow();

        if (row < 0)
            return;

        if (songModel->removeFile(row))
        {
            refreshSongModel();
            savePersistentState();
        }
    };

    doubleClickToggle.onClick = [this]
    {
        savePersistentState();
    };

    normalizeToggle.onClick = [this]
    {
        savePersistentState();
    };

    continuousToggle.onClick = [this]
    {
        savePersistentState();
    };

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
        "CC7 " + juce::String::charToString(0x2192) + " New", 3, 110);

    channelTable.getHeader().addColumn(
        "CC11 Expression", 4, 125);

    channelTable.getHeader().addColumn(
        "Program", 5, 210);

    channelTable.getHeader().addColumn(
        "Family", 6, 110);

    channelTable.setColour(
        juce::ListBox::backgroundColourId,
        panelColour);

    channelTable.setColour(
        juce::ListBox::outlineColourId,
        juce::Colour::fromRGB(65, 65, 65));

    channelTable.getHeader().setColour(
        juce::TableHeaderComponent::backgroundColourId,
        juce::Colour::fromRGB(75, 75, 75));

    channelTable.getHeader().setColour(
        juce::TableHeaderComponent::textColourId,
        juce::Colours::white);

    channelTable.getHeader().setColour(
        juce::TableHeaderComponent::outlineColourId,
        juce::Colour::fromRGB(110, 110, 110));
        
    
    channelTable.setRowHeight(14);

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

    //setSize(1200, 850);

    
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

void MainComponent::loadPersistentState()
{
    auto* properties =
        appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    doubleClickToggle.setToggleState(
        properties->getBoolValue(
            "doubleClickPlays",
            false),
        juce::dontSendNotification);

    normalizeToggle.setToggleState(
        properties->getBoolValue(
            "normalize",
            false),
        juce::dontSendNotification);

    continuousToggle.setToggleState(
        properties->getBoolValue(
            "continuousPlay",
            false),
        juce::dontSendNotification);

    setlists.clear();

    const int setlistCount =
        properties->getIntValue(
            "setlistCount",
            0);

    for (int i = 0; i < setlistCount; ++i)
    {
        SetlistData setlist;

        setlist.name =
            properties->getValue(
                "setlist." + juce::String(i) + ".name",
                "Setlist " + juce::String(i + 1));

        const int songCount =
            properties->getIntValue(
                "setlist." +
                juce::String(i) +
                ".songCount",
                0);

        for (int j = 0; j < songCount; ++j)
        {
            const auto path =
                properties->getValue(
                    "setlist." +
                    juce::String(i) +
                    ".song." +
                    juce::String(j));

            if (path.isNotEmpty())
            {
                // Bewust ook bewaren als het bestand momenteel
                // niet beschikbaar is.
                setlist.songs.emplace_back(
                    path);
            }
        }

        setlists.push_back(
            std::move(setlist));
    }

    if (setlists.empty())
    {
        setlists.push_back(
            { "Setlist 1", {} });
    }

    selectedSetlist =
        properties->getIntValue(
            "selectedSetlist",
            0);

    selectedSetlist =
        juce::jlimit(
            0,
            static_cast<int>(setlists.size()) - 1,
            selectedSetlist);
}

void MainComponent::savePersistentState()
{
    auto* properties =
        appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    properties->setValue(
        "doubleClickPlays",
        doubleClickToggle.getToggleState());

    properties->setValue(
        "normalize",
        normalizeToggle.getToggleState());

    properties->setValue(
        "continuousPlay",
        continuousToggle.getToggleState());

    properties->setValue(
        "selectedSetlist",
        selectedSetlist);

    properties->setValue(
        "setlistCount",
        static_cast<int>(setlists.size()));

    for (std::size_t i = 0;
         i < setlists.size();
         ++i)
    {
        const auto index =
            juce::String(static_cast<int>(i));

        properties->setValue(
            "setlist." + index + ".name",
            setlists[i].name);

        properties->setValue(
            "setlist." + index + ".songCount",
            static_cast<int>(
                setlists[i].songs.size()));

        for (std::size_t j = 0;
             j < setlists[i].songs.size();
             ++j)
        {
            properties->setValue(
                "setlist." +
                index +
                ".song." +
                juce::String(static_cast<int>(j)),
                setlists[i].songs[j].getFullPathName());
        }
    }

    // Echt onmiddellijk naar disk.
    properties->save();
}

void MainComponent::refreshSetlistModel()
{
    setlistBox.updateContent();

    if (setlists.empty())
        return;

    selectedSetlist =
        juce::jlimit(
            0,
            static_cast<int>(setlists.size()) - 1,
            selectedSetlist);

    setlistBox.selectRow(
        selectedSetlist,
        juce::dontSendNotification);
}

void MainComponent::refreshSongModel()
{
    songBox.updateContent();
    songBox.deselectAllRows();

    int songCount = 0;

    if (selectedSetlist >= 0 &&
        selectedSetlist < static_cast<int>(setlists.size()))
    {
        songCount =
            static_cast<int>(
                setlists[static_cast<std::size_t>(selectedSetlist)]
                    .songs.size());
    }

    songsTitle.setText(
        songCount > 0
            ? "Songs (" + juce::String(songCount) + ")"
            : "Songs",
        juce::dontSendNotification);
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
        bounds.removeFromTop(175);

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

    g.setColour(panelColour);
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
        bounds.removeFromTop(175).reduced(12);

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
        player.removeFromTop(38);

    constexpr int buttonHeight = 30;

    // ------------------------------------------------------------
    // Links: Load / Play / Stop / Next / Transpose
    // ------------------------------------------------------------

    loadButton.setBounds(
        transport.removeFromLeft(70)
            .withHeight(buttonHeight));

    transport.removeFromLeft(6);

    playButton.setBounds(
        transport.removeFromLeft(70)
            .withHeight(buttonHeight));

    transport.removeFromLeft(6);

    stopButton.setBounds(
        transport.removeFromLeft(70)
            .withHeight(buttonHeight));

    transport.removeFromLeft(6);

    nextButton.setBounds(
        transport.removeFromLeft(70)
            .withHeight(buttonHeight));

    transport.removeFromLeft(12);

    transposeDownButton.setBounds(
        transport.removeFromLeft(32)
            .withHeight(buttonHeight));

    transposeValue.setBounds(
        transport.removeFromLeft(35)
            .withHeight(buttonHeight));

    transposeUpButton.setBounds(
        transport.removeFromLeft(32)
            .withHeight(buttonHeight));


    // ------------------------------------------------------------
    // Rechts: Panic / Lyrics / Channels
    // ------------------------------------------------------------

    constexpr int rightButtonGap = 6;
    constexpr int channelsWidth = 80;
    constexpr int lyricsWidth = 80;
    constexpr int panicWidth = 80;

    const int rightGroupWidth =
        panicWidth
        + rightButtonGap
        + lyricsWidth
        + rightButtonGap
        + channelsWidth;

    auto rightGroup =
        transport.removeFromRight(rightGroupWidth);

    panicButton.setBounds(
        rightGroup.removeFromLeft(panicWidth)
            .withHeight(buttonHeight));

    rightGroup.removeFromLeft(rightButtonGap);

    lyricsButton.setBounds(
        rightGroup.removeFromLeft(lyricsWidth)
            .withHeight(buttonHeight));

    rightGroup.removeFromLeft(rightButtonGap);

    channelsButton.setBounds(
        rightGroup.removeFromLeft(channelsWidth)
            .withHeight(buttonHeight));

    // -------------------------------------------------------------------------
    // Lists + Channel table
    //
    // Setlists en Songs gebruiken alle beschikbare ruimte.
    // Channels staat altijd onderaan en krimpt wanneer de window te laag wordt.
    // -------------------------------------------------------------------------

    constexpr int listGap = 10;

    constexpr int channelsNormalHeight = 265;
    constexpr int channelsMinimumHeight = 100;

    constexpr int listsMinimumHeight = 180;

    const bool showChannels =
        channelsButton.getToggleState();

    // Links/rechts dezelfde marge als de andere onderdelen.
    auto contentArea =
        bounds.reduced(10, 0);

    int channelsHeight = 0;

    if (showChannels)
    {
        
        // Bij een lage window wordt dit kleiner, maar nooit
        // zo klein dat Setlists + Songs onbruikbaar worden.
        const int maximumChannelsHeight =
            juce::jmax(
                channelsMinimumHeight,
                contentArea.getHeight() - listsMinimumHeight);

        channelsHeight =
            juce::jmin(
                channelsNormalHeight,
                maximumChannelsHeight);
    }

    // -------------------------------------------------------------------------
    // Channels onderaan
    // -------------------------------------------------------------------------

    juce::Rectangle<int> channelArea;

    if (showChannels)
    {
        channelArea =
            contentArea.removeFromBottom(channelsHeight);

        contentArea.removeFromBottom(8);

        channelTable.setBounds(channelArea);
        channelTable.setVisible(true);

        auto& header =
            channelTable.getHeader();

        constexpr int fixedColumnWidth = 55 + 45 + 110 + 125 + 210;

        // TableListBox houdt ruimte vrij voor de verticale scrollbar
        // zodra die zichtbaar is. Trek die ruimte daarom af.
        const int scrollBarWidth =
            channelTable.getVerticalScrollBar().isVisible()
                ? channelTable.getVerticalScrollBar().getWidth()
                : 0;

        const int familyWidth =
            juce::jmax(
                110,
                channelArea.getWidth()
                    - fixedColumnWidth
                    - scrollBarWidth);

header.setColumnWidth(6, familyWidth);
    }
    else
    {
        channelTable.setBounds({});
        channelTable.setVisible(false);
    }

    // -------------------------------------------------------------------------
    // Setlists + Songs
    //
    // Ze krijgen alle resterende ruimte.
    // -------------------------------------------------------------------------

    auto lists = contentArea;

    const int listWidth =
        (lists.getWidth() - listGap) / 2;

    // -------------------------------------------------------------------------
    // Setlists - linker helft
    // -------------------------------------------------------------------------

    auto setlistArea =
        lists.removeFromLeft(listWidth);

    lists.removeFromLeft(listGap);

    setlistsTitle.setBounds(
        setlistArea.removeFromTop(24));

    auto setlistButtons =
        setlistArea.removeFromBottom(30);

    newSetlistButton.setBounds(
        setlistButtons.removeFromLeft(85));

    setlistButtons.removeFromLeft(5);

    deleteSetlistButton.setBounds(
        setlistButtons.removeFromLeft(95));

    setlistBox.setBounds(
        setlistArea);

    // -------------------------------------------------------------------------
    // Songs - rechter helft
    // -------------------------------------------------------------------------

    auto songArea = lists;

    songsTitle.setBounds(
        songArea.removeFromTop(24));

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
}    





bool MainComponent::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file(path);

        if (file.hasFileExtension(".mid;.midi;.kar"))
            return true;
    }

    return false;
}

void MainComponent::filesDropped(
    const juce::StringArray& files,
    int x,
    int y)
{
    juce::ignoreUnused(x, y);

    bool changed = false;

    for (const auto& path : files)
    {
        const juce::File file(path);

        if (songModel->addFile(file))
            changed = true;
    }

    if (changed)
    {
        refreshSongModel();
        savePersistentState();
    }
}