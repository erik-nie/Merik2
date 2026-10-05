#include "MainComponent.h"

namespace
{
    const juce::Colour backgroundColour =
        juce::Colour::fromRGB(25, 25, 25);

    const juce::Colour panelColour =
        juce::Colour::fromRGB(38, 38, 38);

    const juce::Colour panelLightColour =
        juce::Colour::fromRGB(48, 48, 48);

    const juce::Colour merikBlue =
        juce::Colour::fromRGB(0, 164, 235);

    const juce::Colour textColour =
        juce::Colours::white;

    const juce::Colour secondaryTextColour =
        juce::Colour::fromRGB(190, 190, 190);
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
            "It's Not Unusual       Tom Jones",
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

class MainComponent::ChannelModel
    : public juce::TableListBoxModel
{
public:
    int getNumRows() override
    {
        return 16;
    }

    void paintRowBackground(
        juce::Graphics& g,
        int rowNumber,
        int width,
        int height,
        bool rowIsSelected) override
    {
        if (rowIsSelected)
            g.setColour(merikBlue);
        else if (rowNumber % 2 == 0)
            g.setColour(juce::Colour::fromRGB(47, 47, 47));
        else
            g.setColour(juce::Colour::fromRGB(39, 39, 39));

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
        juce::String value;

        switch (columnId)
        {
            case 1:
                value = "✓";
                break;

            case 2:
                value = juce::String(rowNumber + 1);
                break;

            case 3:
                value = "120 → 127";
                break;

            case 4:
                value = "127";
                break;

            case 5:
                value = "Piano";
                break;

            case 6:
                value = "Keys";
                break;

            default:
                return;
        }

        g.setColour(
            rowIsSelected
                ? juce::Colours::white
                : juce::Colour::fromRGB(210, 210, 210));

        g.setFont(juce::FontOptions(12.0f));

        g.drawText(
            value,
            6,
            0,
            width - 12,
            height,
            juce::Justification::centredLeft);
    }
};

// ============================================================================
// MainComponent
// ============================================================================

MainComponent::MainComponent()
{
    setOpaque(true);

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

    settingsButton.setButtonText("⚙");
    settingsButton.setTooltip("Settings");

    settingsButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    settingsButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    addAndMakeVisible(settingsButton);

    // -------------------------------------------------------------------------
    // Family controls
    // -------------------------------------------------------------------------

    const std::array<juce::String, 8> familyNames =
    {
        "1 Drums CH10",
        "2 Bass",
        "3 Guitar",
        "4 Keys",
        "5 Strings",
        "6 Winds",
        "7 FX",
        "8 Melody CH4"
    };

    for (size_t i = 0; i < familyControls.size(); ++i)
    {
        auto& control = familyControls[i];

        control.label.setText(
            familyNames[i],
            juce::dontSendNotification);

        control.label.setColour(
            juce::Label::textColourId,
            textColour);

        control.label.setFont(
            juce::FontOptions(11.0f));

        control.label.setJustificationType(
            juce::Justification::centred);

        addAndMakeVisible(control.label);

        control.slider.setSliderStyle(
            juce::Slider::LinearHorizontal);

        control.slider.setRange(
            0.0,
            127.0,
            1.0);

        control.slider.setValue(100.0);

        control.slider.setColour(
            juce::Slider::trackColourId,
            merikBlue);

        control.slider.setColour(
            juce::Slider::thumbColourId,
            juce::Colours::white);

        control.slider.setColour(
            juce::Slider::backgroundColourId,
            juce::Colour::fromRGB(70, 70, 70));

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
        juce::FontOptions(27.0f));

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

    playButton.setButtonText("▶  Play");

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

    stopButton.setButtonText("■  Stop");

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

    transposeDownButton.setButtonText("−");

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
        "Program", 5, 110);

    channelTable.getHeader().addColumn(
        "Family", 6, 110);

    channelTable.setColour(
        juce::ListBox::backgroundColourId,
        panelColour);

    channelTable.setColour(
        juce::ListBox::outlineColourId,
        juce::Colour::fromRGB(65, 65, 65));

    addAndMakeVisible(channelTable);

    // -------------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------------

    setAudioChannels(0, 2);

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
        bounds.removeFromTop(125);

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
        bounds.removeFromTop(125).reduced(10);

    // First row: MIDI Control / MIDI Out / SoundFont / Settings

    auto topRow =
        header.removeFromTop(52);

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
        topRow.removeFromLeft(225);

    soundFontLabel.setBounds(
        soundFontArea.removeFromTop(18));

    auto soundFontRow =
        soundFontArea.removeFromTop(28);

    soundFontButton.setBounds(
        soundFontRow.removeFromLeft(190));

    soundFontRow.removeFromLeft(5);

    settingsButton.setBounds(
        soundFontRow);

    // Second row: family controls

    header.removeFromTop(5);

    auto familyArea = header;

    const int familyWidth =
        familyArea.getWidth() / 8;

    for (auto& control : familyControls)
    {
        auto area =
            familyArea.removeFromLeft(
                familyWidth);

        control.label.setBounds(
            area.removeFromTop(18));

        control.slider.setBounds(
            area.reduced(4, 3));
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
        bounds.reduced(10);

    channelTable.setBounds(
        channelArea);
}