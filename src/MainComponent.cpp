#include "MainComponent.h"
#include "WebServer.h"
#include "BinaryData.h"
#include "FluidSynthEngine.h"
#include <functional>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace
{
    const juce::Colour backgroundColour =
        juce::Colour::fromRGB(17, 18, 22);

    const juce::Colour panelColour =
        juce::Colour::fromRGB(27, 29, 34);

    const juce::Colour panelDarkColour =
        juce::Colour::fromRGB(22, 23, 27);

        const juce::Colour panelLightColour =
        juce::Colour::fromRGB(46, 49, 56);

    const juce::Colour merikBlue =
        juce::Colour::fromRGB(24, 118, 255);

    const juce::Colour textColour =
        juce::Colours::white;

    const juce::Colour secondaryTextColour =
        juce::Colour::fromRGB(158, 164, 176);

    const juce::Colour familyBlockColour =
        juce::Colour::fromRGB(27, 29, 34);

    const juce::Colour familyBorderColour =
        juce::Colour::fromRGB(48, 51, 58);

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
        notoSansBoldTypeface =
            juce::Typeface::createSystemTypefaceFor(
                BinaryData::NotoSansSemiBold_ttf,
                BinaryData::NotoSansSemiBold_ttfSize);

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

        // Dropdowns: zelfde vlak als de knoppen
        setColour(juce::ComboBox::backgroundColourId, panelLightColour);
        setColour(juce::ComboBox::textColourId, textColour);
        setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::arrowColourId, secondaryTextColour);

        // Getalvelden van de sliders (CC-nummers, synth-waarden)
        setColour(juce::Slider::textBoxTextColourId, textColour);
        setColour(juce::Slider::textBoxBackgroundColourId, panelLightColour);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, merikBlue);

        // Scrollbars: smal en rustig
        setColour(juce::ScrollBar::thumbColourId, juce::Colour::fromRGB(84, 88, 98));

        // Uitklapmenu
        setColour(juce::PopupMenu::backgroundColourId, panelColour);
        setColour(juce::PopupMenu::textColourId, textColour);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, merikBlue);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    // juce::Font getLabelFont(juce::Label&) override
    // {
    //     return makeFont(15.0f);
    // }

    juce::Font getTextButtonFont(
        juce::TextButton&,
        int) override
    {
        return getMediumFont(20.0f);
    }

    // juce::Font getComboBoxFont(
    //     juce::ComboBox&) override
    // {
    //     return makeFont(14.0f);
    // }

    juce::Font getPopupMenuFont() override
    {
        return getMediumFont(16.0f);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return getMediumFont(18.0f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height)
                          .toFloat().reduced(0.5f);

        // Zelfde vlak en afronding als de knoppen
        auto fill = panelLightColour;

        if (box.isPopupActive())
            fill = fill.brighter(0.10f);
        else if (box.isMouseOver(true))
            fill = fill.brighter(0.06f);

        g.setColour(fill.withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle(bounds, 7.0f);

        auto arrowArea = bounds.removeFromRight(30.0f);

        juce::Path arrow;
        arrow.startNewSubPath(arrowArea.getCentreX() - 4.5f,
                              arrowArea.getCentreY() - 2.5f);
        arrow.lineTo(arrowArea.getCentreX(),
                     arrowArea.getCentreY() + 2.5f);
        arrow.lineTo(arrowArea.getCentreX() + 4.5f,
                     arrowArea.getCentreY() - 2.5f);

        g.setColour(secondaryTextColour);
        g.strokePath(arrow,
                     juce::PathStrokeType(1.8f,
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(10, 1, box.getWidth() - 40, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.fillAll(panelColour);

        g.setColour(familyBorderColour);
        g.drawRect(0, 0, width, height, 1);
    }

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool highlighted,
                          bool) override
    {
        const float boxSize = 22.0f;
        const auto box = juce::Rectangle<float>(
            2.0f,
            (static_cast<float>(button.getHeight()) - boxSize) * 0.5f,
            boxSize,
            boxSize);

        const bool on = button.getToggleState();

        auto fill = on ? merikBlue : panelLightColour;
        if (highlighted)
            fill = fill.brighter(0.08f);

        g.setColour(fill.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle(box, 5.0f);

        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath(box.getX() + boxSize * 0.24f, box.getCentreY());
            tick.lineTo(box.getX() + boxSize * 0.44f, box.getY() + boxSize * 0.70f);
            tick.lineTo(box.getX() + boxSize * 0.76f, box.getY() + boxSize * 0.30f);

            g.setColour(juce::Colours::white);
            g.strokePath(tick,
                         juce::PathStrokeType(2.2f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }

        g.setColour(textColour);
        g.setFont(getMediumFont(17.0f));
        g.drawText(button.getButtonText(),
                   button.getLocalBounds().withTrimmedLeft(
                       static_cast<int>(boxSize) + 12),
                   juce::Justification::centredLeft,
                   true);
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

        g.setColour(juce::Colour::fromRGB(38, 41, 47));
        g.fillRect(0, 0, width, height);

        g.setColour(juce::Colour::fromRGB(52, 55, 62));
        g.fillRect(width - 1, 0, 1, height);

        g.setColour(textColour);
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

        g.setColour(juce::Colour::fromRGB(38, 41, 47));
        g.fillAll();
    }

    void drawButtonBackground(
        juce::Graphics& g,
        juce::Button& button,
        const juce::Colour&,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown) override
    {
        auto bounds =
            button.getLocalBounds().toFloat().reduced(0.5f);

        // "Flat": geen eigen vlak, alleen een tint bij hover/klik
        // (gebruikt voor de transpose-capsule).
        if (button.getProperties().getWithDefault("flat", false))
        {
            if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
            {
                g.setColour(juce::Colours::white.withAlpha(
                    shouldDrawButtonAsDown ? 0.18f : 0.09f));
                g.fillRoundedRectangle(bounds.reduced(2.0f),
                                       bounds.getHeight() * 0.5f - 2.0f);
            }
            return;
        }

        // Actieve knop = MerikBlue, normale knop = effen donker vlak.
        auto colour =
            button.getToggleState()
                ? merikBlue
                : panelLightColour;

        if (shouldDrawButtonAsDown)
            colour = colour.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted)
            colour = colour.brighter(0.10f);

        g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
        g.fillRoundedRectangle(bounds, 6.0f);
    }

    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos, float, float,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider(
                g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
            return;
        }

        constexpr float trackHeight = 4.0f;
        constexpr float thumbSize = 14.0f;

        const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float left = static_cast<float>(x) + thumbSize * 0.5f;
        const float right = static_cast<float>(x + width) - thumbSize * 0.5f;

        // sliderPos is gebaseerd op de volle breedte; reken om naar het spoor.
        const float t = juce::jlimit(
            0.0f, 1.0f,
            (sliderPos - static_cast<float>(x))
                / juce::jmax(1.0f, static_cast<float>(width)));
        const float thumbX = left + t * (right - left);

        g.setColour(juce::Colour::fromRGB(52, 55, 62));
        g.fillRoundedRectangle(left, cy - trackHeight * 0.5f,
                               right - left, trackHeight, trackHeight * 0.5f);

        g.setColour(merikBlue);
        g.fillRoundedRectangle(left, cy - trackHeight * 0.5f,
                               thumbX - left, trackHeight, trackHeight * 0.5f);

        const bool active = slider.isMouseOverOrDragging();

        g.setColour(juce::Colours::white);
        g.fillEllipse(thumbX - thumbSize * 0.5f, cy - thumbSize * 0.5f,
                      thumbSize, thumbSize);

        if (active)
        {
            g.setColour(merikBlue.withAlpha(0.35f));
            g.drawEllipse(thumbX - thumbSize * 0.5f - 2.0f,
                          cy - thumbSize * 0.5f - 2.0f,
                          thumbSize + 4.0f, thumbSize + 4.0f, 2.0f);
        }
    }

    int getTabButtonBestWidth(juce::TabBarButton&, int) override
    {
        return 120;
    }

    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override
    {
    }

    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool) override
    {
        auto area = button.getLocalBounds().toFloat().reduced(3.0f, 4.0f);

        auto fill = button.isFrontTab() ? merikBlue : panelLightColour;

        if (!button.isFrontTab() && isMouseOver)
            fill = fill.brighter(0.08f);

        g.setColour(fill);
        g.fillRoundedRectangle(area, 7.0f);

        g.setColour(juce::Colours::white);
        g.setFont(getMediumFont(17.0f));
        g.drawText(button.getButtonText(), area.toNearestInt(), juce::Justification::centred);
    }

    int getDefaultScrollbarWidth() override
    {
        return 10;
    }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& bar,
                       int x, int y, int width, int height,
                       bool isVertical, int thumbStart, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        juce::ignoreUnused(bar, x, y);

        if (thumbSize <= 0)
            return;

        auto thumb = isVertical
            ? juce::Rectangle<float>(static_cast<float>(width) - 7.0f,
                                     static_cast<float>(thumbStart),
                                     5.0f, static_cast<float>(thumbSize))
            : juce::Rectangle<float>(static_cast<float>(thumbStart),
                                     static_cast<float>(height) - 7.0f,
                                     static_cast<float>(thumbSize), 5.0f);

        auto colour = juce::Colour::fromRGB(84, 88, 98);
        if (isMouseDown)
            colour = merikBlue;
        else if (isMouseOver)
            colour = colour.brighter(0.25f);

        g.setColour(colour);
        g.fillRoundedRectangle(thumb, 2.5f);
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

    juce::Font getBoldFont(float height) const
    {
        return juce::Font(
            juce::FontOptions(notoSansBoldTypeface)
                .withHeight(height));
    }

    juce::Font getMerikFont(float height)
    {
        return makeFont(height);
    }

    private:
        juce::Typeface::Ptr notoSansTypeface;
        juce::Typeface::Ptr notoSansMediumTypeface;
        juce::Typeface::Ptr notoSansBoldTypeface;

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
            g.setColour(juce::Colour::fromRGB(32, 34, 39));
        else
            g.setColour(juce::Colour::fromRGB(27, 29, 34));

        g.fillRect(0, 0, width, height);

        g.setColour(textColour);
        g.setFont(makeNotoFont(17.0f));

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
// Pagina met een eigen indeling (de eigenaar geeft een layout-functie).
class SettingsPage : public juce::Component
{
public:
    std::function<void(juce::Rectangle<int>)> layout;

    void resized() override
    {
        if (layout)
            layout(getLocalBounds());
    }
};

// Pagina die verticaal scrollt als de inhoud hoger is dan het venster.
class ScrollPage : public juce::Component
{
public:
    ScrollPage()
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    juce::Component content;

    // Krijgt de beschikbare breedte; geeft de benodigde hoogte terug.
    std::function<int(int width)> layoutContent;

    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        relayout();
    }

    void relayout()
    {
        const int width = viewport.getMaximumVisibleWidth();
        const int height = layoutContent ? layoutContent(width) : 0;

        content.setSize(width, juce::jmax(height, viewport.getHeight()));
    }

private:
    juce::Viewport viewport;
};

struct SettingsContext
{
    juce::AudioDeviceManager& deviceManager;
    juce::ComboBox& midiIn;
    juce::ComboBox& midiOut;
    juce::ComboBox& ledOut;
    juce::TextButton& soundFont;
    juce::TextButton& directPlay;
    juce::TextButton& normalize;
    juce::TextButton& autoNext;

    std::function<juce::String()> midiInActivity;

    juce::String webKey;
    std::function<void(const juce::String&)> webKeyChanged;

    MidiMapping& mapping;
    std::function<void()> mappingChanged;

    SynthSettings synth;
    std::function<void(const SynthSettings&)> synthChanged;
};

class SettingsContent final : public juce::Component,
                              private juce::ComponentListener,
                              private juce::Timer
{
public:
    explicit SettingsContent(SettingsContext contextToUse)
        : context(std::move(contextToUse)),
          sources { &context.directPlay, &context.normalize, &context.autoNext },
          comboSources { &context.midiIn, &context.midiOut, &context.ledOut }
    {
        setLookAndFeel(&merikLookAndFeel);

        buildGeneral();
        buildAudio();
        buildMidi();

        tabs.setTabBarDepth(38);
        tabs.setOutline(0);
        tabs.setIndent(0);
        tabs.addTab("General", backgroundColour, &generalPage, false);
        tabs.addTab("Audio", backgroundColour, &audioPage, false);
        tabs.addTab("Midi", backgroundColour, &midiPage, false);
        addAndMakeVisible(tabs);

        refresh();
        setSize(700, 640);
        startTimer(300);
    }

    ~SettingsContent() override
    {
        stopTimer();

        if (selector != nullptr)
            selector->removeComponentListener(this);

        setLookAndFeel(nullptr);
    }

    // De drie opties van het hoofdscherm gelijkhouden met deze kopieën.
    void refresh()
    {
        for (std::size_t i = 0; i < optionCopies.size(); ++i)
            optionCopies[i].setToggleState(sources[i]->getToggleState(), juce::dontSendNotification);
    }

    void resized() override
    {
        tabs.setBounds(getLocalBounds().reduced(10, 8));
    }

private:
    static constexpr int rowHeight = 28;
    static constexpr int rowStep = 34;

    static void styleLabel(juce::Label& label, const juce::String& text,
                           juce::Justification justification = juce::Justification::centredRight)
    {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(justification);
    }

    static void styleHeading(juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(merikLookAndFeel.getBoldFont(16.0f));
        label.setColour(juce::Label::textColourId, textColour);
        label.setJustificationType(juce::Justification::centredLeft);
    }

    // ---------------------------------------------------------------- General

    void buildGeneral()
    {
        styleLabel(webKeyLabel, "Web control key");
        generalPage.addAndMakeVisible(webKeyLabel);

        webKeyEditor.setInputRestrictions(
            32, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
        webKeyEditor.setText(context.webKey, juce::dontSendNotification);
        webKeyEditor.setColour(juce::TextEditor::backgroundColourId, panelLightColour);
        webKeyEditor.setColour(juce::TextEditor::textColourId, textColour);
        webKeyEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        webKeyEditor.setColour(juce::TextEditor::focusedOutlineColourId, merikBlue);
        webKeyEditor.setFont(merikLookAndFeel.getMediumFont(17.0f));
        webKeyEditor.setIndents(10, 0);
        webKeyEditor.setJustification(juce::Justification::centredLeft);
        webKeyEditor.onTextChange = [this]
        {
            updateWebHint();

            if (context.webKeyChanged)
                context.webKeyChanged(webKeyEditor.getText());
        };
        generalPage.addAndMakeVisible(webKeyEditor);

        webHint.setColour(juce::Label::textColourId, secondaryTextColour);
        webHint.setFont(merikLookAndFeel.getMediumFont(13.0f));
        webHint.setMinimumHorizontalScale(1.0f);
        generalPage.addAndMakeVisible(webHint);
        updateWebHint();

        static const char* optionLabels[3] = {
            "Doubleclick starts playing",
            "Normalize based on drumchannel level",
            "Auto play next song in setlist"
        };

        for (std::size_t i = 0; i < optionCopies.size(); ++i)
        {
            auto& copy = optionCopies[i];
            auto* source = sources[i];

            copy.setButtonText(optionLabels[i]);
            copy.setClickingTogglesState(true);

            copy.onClick = [source, &copy]
            {
                source->setToggleState(copy.getToggleState(), juce::dontSendNotification);

                // Zelfde afhandeling als een klik in het hoofdscherm.
                if (source->onClick != nullptr)
                    source->onClick();
            };

            generalPage.addAndMakeVisible(copy);
        }

        generalPage.layout = [this](juce::Rectangle<int> bounds)
        {
            const int controlX = bounds.proportionOfWidth(0.30f);
            const int controlWidth = bounds.proportionOfWidth(0.66f);
            int y = 18;

            webKeyLabel.setBounds(0, y, controlX - 10, rowHeight);
            webKeyEditor.setBounds(controlX, y, controlWidth, rowHeight);
            y += rowStep - 2;

            webHint.setBounds(controlX, y, controlWidth, 34);
            y += 52;

            for (auto& copy : optionCopies)
            {
                copy.setBounds(controlX, y, controlWidth, 30);
                y += 34;
            }
        };
    }

    void updateWebHint()
    {
        juce::String ip = "<ip>";

        for (const auto& address : juce::IPAddress::getAllAddresses(false))
        {
            if (!address.isIPv6 && address != juce::IPAddress::local())
            {
                ip = address.toString();
                break;
            }
        }

        const auto key = webKeyEditor.getText();

        webHint.setText(
            key.isEmpty()
                ? "Leeg: de webpagina is alleen-lezen en kan nooit bedienen."
                : "Bediening aan via http://" + ip + ":" + juce::String(WebServer::defaultPort)
                    + "/?key=" + key + "  (zonder sleutel: alleen kijken)",
            juce::dontSendNotification);
    }

    // ------------------------------------------------------------------ Audio

    struct SliderRow
    {
        juce::Label label;
        juce::Slider slider;
    };

    void buildAudio()
    {
        selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            context.deviceManager,
            0, 0,   // geen audio-input
            1, 2,   // 1-2 outputkanalen
            false,  // geen MIDI input
            false,  // geen MIDI output
            true,   // stereo pairs
            false); // advanced settings

        audioPage.content.addAndMakeVisible(*selector);
        selector->addComponentListener(this);

        styleLabel(soundFontLabel, "SoundFont");
        audioPage.content.addAndMakeVisible(soundFontLabel);

        soundFontCopy.setButtonText(context.soundFont.getButtonText());
        soundFontCopy.onClick = [this]
        {
            if (context.soundFont.onClick != nullptr)
                context.soundFont.onClick();
        };
        audioPage.content.addAndMakeVisible(soundFontCopy);

        styleHeading(soundHeading, "Soundfont player: sound");
        styleHeading(reverbHeading, "Reverb");

        for (auto* heading : { &soundHeading, &reverbHeading })
            audioPage.content.addAndMakeVisible(heading);

        struct Definition
        {
            const char* name;
            int SynthSettings::* member;
            int minimum, maximum;
        };

        static const Definition definitions[] = {
            { "Gain",        &SynthSettings::gain,         0, 100 },
            { "Room size",   &SynthSettings::reverbRoom,   0, 100 },
            { "Damping",     &SynthSettings::reverbDamp,   0, 100 },
            { "Width",       &SynthSettings::reverbWidth,  0, 100 },
            { "Level",       &SynthSettings::reverbLevel,  0, 100 }
        };

        for (std::size_t i = 0; i < synthRows.size(); ++i)
        {
            auto& row = synthRows[i];
            const auto& definition = definitions[i];

            styleLabel(row.label, definition.name);
            audioPage.content.addAndMakeVisible(row.label);

            row.slider.setSliderStyle(juce::Slider::LinearHorizontal);
            row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
            row.slider.setRange(definition.minimum, definition.maximum, 1.0);
            row.slider.setValue(context.synth.*(definition.member), juce::dontSendNotification);
            row.slider.setDoubleClickReturnValue(false, 0.0);

            row.slider.onValueChange = [this, &row, member = definition.member]
            {
                context.synth.*member = juce::roundToInt(row.slider.getValue());

                if (context.synthChanged)
                    context.synthChanged(context.synth);
            };

            audioPage.content.addAndMakeVisible(row.slider);
        }

        audioPage.layoutContent = [this](int width)
        {
            const int controlX = juce::roundToInt(static_cast<float>(width) * 0.30f);
            const int controlWidth = juce::roundToInt(static_cast<float>(width) * 0.62f);

            // SoundFont bovenaan, daaronder het audio-apparaat (bepaalt zijn
            // eigen hoogte) en de klank van de interne synth.
            soundFontLabel.setBounds(0, 18, controlX - 10, rowHeight);
            soundFontCopy.setBounds(controlX, 18, controlWidth, rowHeight);

            selector->setBounds(0, 18 + rowStep + 4, width, 3000);
            int y = selector->getBottom() + 14;

            soundHeading.setBounds(controlX, y, controlWidth, 24);
            y += 30;

            const auto place = [&](SliderRow& row)
            {
                row.label.setBounds(0, y, controlX - 10, rowHeight);
                row.slider.setBounds(controlX, y, controlWidth, rowHeight);
                y += rowStep - 4;
            };

            place(synthRows[0]);                // gain
            y += 8;

            reverbHeading.setBounds(controlX, y, controlWidth, 24);
            y += 30;

            for (std::size_t i = 1; i <= 4; ++i)
                place(synthRows[i]);

            y += 14;

            return y;
        };
    }

    void componentMovedOrResized(juce::Component& component, bool, bool wasResized) override
    {
        if (&component == selector.get() && wasResized && !inAudioLayout)
        {
            const juce::ScopedValueSetter<bool> guard(inAudioLayout, true);
            audioPage.relayout();
        }
    }

    // ------------------------------------------------------------------- Midi

    void buildMidi()
    {
        static const char* comboLabels[3] = { "MIDI control in", "MIDI out", "Controller LEDs" };

        for (std::size_t i = 0; i < comboCopies.size(); ++i)
        {
            auto& box = comboCopies[i];
            auto* source = comboSources[i];

            styleLabel(comboLabels_[i], comboLabels[i]);
            midiPage.content.addAndMakeVisible(comboLabels_[i]);

            for (int item = 0; item < source->getNumItems(); ++item)
                box.addItem(source->getItemText(item), source->getItemId(item));

            box.setSelectedId(source->getSelectedId(), juce::dontSendNotification);

            box.onChange = [source, &box]
            {
                source->setSelectedId(box.getSelectedId(), juce::sendNotification);
            };

            midiPage.content.addAndMakeVisible(box);
        }

        // Laatste CC van de controller (zoals "CC41:127" in Merik 1).
        midiInActivityLabel.setJustificationType(juce::Justification::centredRight);
        midiInActivityLabel.setColour(juce::Label::textColourId, secondaryTextColour);
        midiInActivityLabel.setFont(merikLookAndFeel.getMediumFont(13.0f));
        midiPage.content.addAndMakeVisible(midiInActivityLabel);

        ledHint.setColour(juce::Label::textColourId, secondaryTextColour);
        ledHint.setFont(merikLookAndFeel.getMediumFont(13.0f));
        ledHint.setMinimumHorizontalScale(1.0f);
        ledHint.setText("LEDs: set the nanoKONTROL2 LED mode to \"External\" in the Korg Kontrol Editor.",
                        juce::dontSendNotification);
        midiPage.content.addAndMakeVisible(ledHint);

        styleHeading(assignHeading, "Controller assignments (CC numbers)");
        midiPage.content.addAndMakeVisible(assignHeading);

        styleLabel(faderTitle, "Fader (volume)", juce::Justification::centred);
        styleLabel(enableTitle, "R button (on/off)", juce::Justification::centred);
        styleLabel(buttonTitle, "Transport buttons", juce::Justification::centred);

        for (auto* title : { &faderTitle, &enableTitle, &buttonTitle })
        {
            title->setColour(juce::Label::textColourId, secondaryTextColour);
            title->setFont(merikLookAndFeel.getMediumFont(13.0f));
            midiPage.content.addAndMakeVisible(title);
        }

        static const char* familyNames[MidiMapping::families] = {
            "1 Drums", "2 Bass", "3 Guitars", "4 Keys", "5 Strings", "6 Winds", "7 FX", "8 Other"
        };

        const auto setupCc = [this](juce::Slider& slider, std::atomic<int>& value)
        {
            slider.setSliderStyle(juce::Slider::IncDecButtons);
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 26);
            slider.setRange(0, 127, 1);
            slider.setValue(value.load(), juce::dontSendNotification);
            slider.onValueChange = [this, &slider, &value]
            {
                value = juce::roundToInt(slider.getValue());

                if (context.mappingChanged)
                    context.mappingChanged();
            };
            midiPage.content.addAndMakeVisible(slider);
        };

        for (std::size_t i = 0; i < MidiMapping::families; ++i)
        {
            styleLabel(familyLabels[i], familyNames[i]);
            midiPage.content.addAndMakeVisible(familyLabels[i]);

            setupCc(faderSliders[i], context.mapping.fader[i]);
            setupCc(enableSliders[i], context.mapping.enable[i]);
        }

        struct Button
        {
            const char* name;
            std::atomic<int>* value;
        };

        const Button buttons[4] = {
            { "Play / Pause", &context.mapping.play },
            { "Stop", &context.mapping.stop },
            { "Next", &context.mapping.next },
            { "Panic", &context.mapping.panic }
        };

        for (std::size_t i = 0; i < buttonSliders.size(); ++i)
        {
            styleLabel(buttonLabels[i], buttons[i].name);
            midiPage.content.addAndMakeVisible(buttonLabels[i]);
            setupCc(buttonSliders[i], *buttons[i].value);
        }

        resetMapping.setButtonText("Reset to nanoKONTROL2 defaults");
        resetMapping.onClick = [this]
        {
            MidiMapping defaults;

            for (std::size_t i = 0; i < MidiMapping::families; ++i)
            {
                faderSliders[i].setValue(defaults.fader[i].load(), juce::sendNotification);
                enableSliders[i].setValue(defaults.enable[i].load(), juce::sendNotification);
            }

            buttonSliders[0].setValue(defaults.play.load(), juce::sendNotification);
            buttonSliders[1].setValue(defaults.stop.load(), juce::sendNotification);
            buttonSliders[2].setValue(defaults.next.load(), juce::sendNotification);
            buttonSliders[3].setValue(defaults.panic.load(), juce::sendNotification);
        };
        midiPage.content.addAndMakeVisible(resetMapping);

        midiPage.layoutContent = [this](int width)
        {
            const int controlX = juce::roundToInt(static_cast<float>(width) * 0.30f);
            const int controlWidth = juce::roundToInt(static_cast<float>(width) * 0.62f);
            int y = 18;

            for (std::size_t i = 0; i < comboCopies.size(); ++i)
            {
                comboLabels_[i].setBounds(0, y, controlX - 10, rowHeight);

                // Eerste rij: ruimte voor het activiteitslabel.
                const int boxWidth = i == 0 ? controlWidth - 84 : controlWidth;
                comboCopies[i].setBounds(controlX, y, boxWidth, rowHeight);

                if (i == 0)
                    midiInActivityLabel.setBounds(controlX + controlWidth - 80, y, 80, rowHeight);

                y += rowStep;
            }

            ledHint.setBounds(controlX, y - 4, controlWidth + 20, 20);
            y += 34;

            assignHeading.setBounds(controlX - 70, y, controlWidth + 70, 24);
            y += 32;

            // Drie kolommen: fader | R-knop | transportknoppen (naast de eerste vier rijen).
            const int columnWidth = 104;
            const int labelWidth = 92;
            const int faderX = labelWidth + 12;
            const int enableX = faderX + columnWidth + 22;
            const int buttonLabelX = enableX + columnWidth + 26;
            const int buttonX = buttonLabelX + labelWidth + 8;

            faderTitle.setBounds(faderX - 10, y, columnWidth + 20, 18);
            enableTitle.setBounds(enableX - 10, y, columnWidth + 20, 18);
            buttonTitle.setBounds(buttonLabelX, y, labelWidth + 8 + columnWidth, 18);
            y += 22;

            for (std::size_t i = 0; i < MidiMapping::families; ++i)
            {
                familyLabels[i].setBounds(0, y, labelWidth, rowHeight);
                faderSliders[i].setBounds(faderX, y, columnWidth, rowHeight - 2);
                enableSliders[i].setBounds(enableX, y, columnWidth, rowHeight - 2);

                if (i < buttonSliders.size())
                {
                    buttonLabels[i].setBounds(buttonLabelX, y, labelWidth, rowHeight);
                    buttonSliders[i].setBounds(buttonX, y, columnWidth, rowHeight - 2);
                }

                y += rowStep - 4;
            }

            y += 12;
            resetMapping.setBounds(faderX, y, 250, rowHeight);
            y += rowStep + 12;

            return y;
        };
    }

    // ------------------------------------------------------------------ Timer

    void timerCallback() override
    {
        if (context.midiInActivity)
        {
            const auto text = context.midiInActivity();

            if (midiInActivityLabel.getText() != text)
                midiInActivityLabel.setText(text, juce::dontSendNotification);
        }

        if (soundFontCopy.getButtonText() != context.soundFont.getButtonText())
            soundFontCopy.setButtonText(context.soundFont.getButtonText());

        for (std::size_t i = 0; i < comboCopies.size(); ++i)
        {
            auto& box = comboCopies[i];
            const auto& source = *comboSources[i];

            // De apparatenlijst kan veranderen terwijl dit venster openstaat
            // (MIDI-apparaat in- of uitgestoken): neem de items opnieuw over.
            bool sameItems = box.getNumItems() == source.getNumItems();

            for (int item = 0; sameItems && item < source.getNumItems(); ++item)
                sameItems = box.getItemText(item) == source.getItemText(item);

            if (!sameItems)
            {
                box.clear(juce::dontSendNotification);

                for (int item = 0; item < source.getNumItems(); ++item)
                    box.addItem(source.getItemText(item), source.getItemId(item));
            }

            if (box.getSelectedId() != source.getSelectedId())
                box.setSelectedId(source.getSelectedId(), juce::dontSendNotification);
        }
    }

    SettingsContext context;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // General
    SettingsPage generalPage;
    juce::Label webKeyLabel, webHint;
    juce::TextEditor webKeyEditor;
    std::array<juce::ToggleButton, 3> optionCopies;
    std::array<juce::TextButton*, 3> sources;

    // Audio
    ScrollPage audioPage;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Label soundFontLabel, soundHeading, reverbHeading;
    juce::TextButton soundFontCopy;
    std::array<SliderRow, 5> synthRows;
    bool inAudioLayout = false;

    // Midi
    ScrollPage midiPage;
    std::array<juce::ComboBox*, 3> comboSources;
    std::array<juce::ComboBox, 3> comboCopies;
    std::array<juce::Label, 3> comboLabels_;
    juce::Label midiInActivityLabel, ledHint, assignHeading, faderTitle, enableTitle, buttonTitle;
    std::array<juce::Label, MidiMapping::families> familyLabels;
    std::array<juce::Slider, MidiMapping::families> faderSliders, enableSliders;
    std::array<juce::Label, 4> buttonLabels;
    std::array<juce::Slider, 4> buttonSliders;
    juce::TextButton resetMapping;
};

class AudioSettingsWindow final : public juce::DialogWindow
{
public:
    AudioSettingsWindow(juce::Component* content,
                        juce::Component* owner)
        : juce::DialogWindow(
              "Settings",
              backgroundColour,
              true)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);

        setContentOwned(content, true);

        centreAroundComponent(owner, getWidth(), getHeight());
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
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront(true);
        return;
    }

    SettingsContext context {
        deviceManager,
        midiControlBox,
        midiOutBox,
        ledOutBox,
        soundFontButton,
        doubleClickToggle,
        normalizeToggle,
        continuousToggle,

        [this]
        {
            const int cc = lastMidiInCc.load();

            return cc < 0 ? juce::String()
                          : "CC" + juce::String(cc).paddedLeft('0', 2) + ":" + juce::String(lastMidiInValue.load());
        },

        webControlKey,
        [this](const juce::String& key)
        {
            webControlKey = key;
            webServer.setControlKey(key.toStdString());

            if (auto* properties = appProperties.getUserSettings())
            {
                properties->setValue("webControlKey", key);
                properties->saveIfNeeded();
            }
        },

        midiMapping,
        [this] { saveMidiMapping(); },

        synthEngine.getSynthSettings(),
        [this](const SynthSettings& settings)
        {
            synthEngine.setSynthSettings(settings);
            saveSynthSettings(settings);
        }
    };

    auto* content = new SettingsContent(std::move(context));

    juce::Component::SafePointer<SettingsContent> safeContent(content);

    syncSettingsToggles = [safeContent]
    {
        if (safeContent != nullptr)
            safeContent->refresh();
    };

    settingsWindow = new AudioSettingsWindow(content, this);
}

// ============================================================================
// SetlistModel
// ============================================================================
// ============================================================================
// Gedeelde tekenhulp voor de lijsten
// ============================================================================

namespace
{
// Twee kolommen van drie puntjes: "pak mij vast" (de rij is sleepbaar).
void drawGrip(juce::Graphics& g, int height, bool onBlue)
{
    g.setColour((onBlue ? juce::Colours::white : secondaryTextColour).withAlpha(0.45f));

    const float top = (static_cast<float>(height) - 10.0f) * 0.5f;

    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse(6.0f + static_cast<float>(column) * 4.5f,
                          top + static_cast<float>(row) * 4.5f,
                          2.2f, 2.2f);
}
}

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

    std::function<void(int row)> onContextMenu;
    std::function<void(int row)> onDeleteKey;

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

        drawGrip(g, height, rowIsSelected);

        const auto& setlist = (*setlists)[static_cast<std::size_t>(rowNumber)];

        // Aantal nummers rechts, zodat je ziet wat er in een setlist zit.
        const auto count = juce::String(static_cast<int>(setlist.songs.size()));

        g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.85f)
                                  : secondaryTextColour);
        g.setFont(makeNotoFont(15.0f));
        g.drawText(count, width - 50, 0, 40, height, juce::Justification::centredRight);

        g.setColour(textColour);
        g.setFont(makeNotoFont(17.0f));

        g.drawText(
            setlist.name,
            22,
            0,
            width - 80,
            height,
            juce::Justification::centredLeft);
    }

    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override
    {
        if (rows.size() != 1)
            return {};

        return "setlist:" + juce::String(rows[0]);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && onContextMenu)
            onContextMenu(row);
    }

    void deleteKeyPressed(int lastRowSelected) override
    {
        if (onDeleteKey)
            onDeleteKey(lastRowSelected);
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

    std::function<void(int row)> onDoubleClick;
    std::function<void(int row)> onContextMenu;
    std::function<void(int row)> onDeleteKey;
    std::function<bool(const juce::File&)> isCurrentSong;

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

        drawGrip(g, height, rowIsSelected);

        const auto& entry = (*songs)[static_cast<std::size_t>(rowNumber)];

        g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.85f)
                                  : secondaryTextColour);
        g.setFont(makeNotoFont(15.0f));

        g.drawText(
            juce::String(rowNumber + 1),
            16,
            0,
            28,
            height,
            juce::Justification::centredRight);

        // Driehoekje bij het nummer dat nu geladen is.
        if (isCurrentSong && isCurrentSong(entry.file))
        {
            const float cy = static_cast<float>(height) * 0.5f;
            juce::Path triangle;
            triangle.addTriangle(50.0f, cy - 5.0f, 50.0f, cy + 5.0f, 58.0f, cy);

            g.setColour(rowIsSelected ? juce::Colours::white : merikBlue);
            g.fillPath(triangle);
        }

        // Transpose-badge rechts (alleen als er een standaard transpose is).
        int rightEdge = width - 10;

        if (entry.transpose != 0)
        {
            const auto text = (entry.transpose > 0 ? "+" : "")
                              + juce::String(entry.transpose);

            g.setFont(makeNotoFont(14.0f));

            const int badgeWidth = 34;
            const auto badge = juce::Rectangle<float>(
                static_cast<float>(rightEdge - badgeWidth),
                (static_cast<float>(height) - 16.0f) * 0.5f,
                static_cast<float>(badgeWidth), 16.0f);

            g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.25f)
                                      : merikBlue.withAlpha(0.28f));
            g.fillRoundedRectangle(badge, 8.0f);

            g.setColour(textColour);
            g.drawText(text, badge.toNearestInt(), juce::Justification::centred);

            rightEdge -= badgeWidth + 8;
        }

        g.setColour(textColour);
        g.setFont(makeNotoFont(17.0f));

        g.drawText(
            entry.shownName(true),
            64,
            0,
            rightEdge - 64,
            height,
            juce::Justification::centredLeft,
            true);
    }

    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override
    {
        if (rows.size() != 1 || selectedSetlist == nullptr)
            return {};

        return "song:" + juce::String(*selectedSetlist) + ":" + juce::String(rows[0]);
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
                [&file](const MainComponent::SongEntry& existing)
                {
                    return existing.file == file;
                });

        if (alreadyExists != songs->end())
            return false;

        MainComponent::SongEntry entry;
        entry.file = file;
        songs->push_back(std::move(entry));
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

    const MainComponent::SongEntry* getEntry(int row) const
    {
        const auto* songs = getCurrentSongs();

        if (songs == nullptr ||
            row < 0 ||
            row >= static_cast<int>(songs->size()))
        {
            return nullptr;
        }

        return &(*songs)[static_cast<std::size_t>(row)];
    }

    void listBoxItemDoubleClicked(
        int row,
        const juce::MouseEvent&) override
    {
        if (onDoubleClick && getEntry(row) != nullptr)
            onDoubleClick(row);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && onContextMenu)
            onContextMenu(row);
    }

    void deleteKeyPressed(int lastRowSelected) override
    {
        if (onDeleteKey)
            onDeleteKey(lastRowSelected);
    }

private:
    // BELANGRIJK:
    // Dit is een vector van SetlistData, niet van juce::File.
    std::vector<MainComponent::SetlistData>* setlists = nullptr;

    int* selectedSetlist = nullptr;

    std::vector<MainComponent::SongEntry>* getCurrentSongs()
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

    const std::vector<MainComponent::SongEntry>* getCurrentSongs() const
    {
        return const_cast<SongModel*>(this)->getCurrentSongs();
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
    enum class Active { on, offThisSong, off };

    // Wordt aangeroepen als een kanaal gedempt/ontdempt moet worden.
    void setMuteCallback(std::function<void(int, bool)> callback)
    {
        muteCallback = std::move(callback);
    }

    // Nieuw nummer: "uit voor dit nummer" gaat terug naar aan.
    void resetThisSongOnly()
    {
        for (int ch = 0; ch < 16; ++ch)
        {
            auto& a = active[static_cast<std::size_t>(ch)];

            if (a == Active::offThisSong)
            {
                a = Active::on;

                if (muteCallback)
                    muteCallback(ch, false);
            }
        }
    }

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override
    {
        if (columnId != 1 || rowNumber < 0 || rowNumber >= 16)
            return;

        auto& a = active[static_cast<std::size_t>(rowNumber)];

        a = a == Active::on          ? Active::offThisSong
          : a == Active::offThisSong ? Active::off
                                     : Active::on;

        if (muteCallback)
            muteCallback(rowNumber, a != Active::on);
    }

    juce::String getCellTooltip(int rowNumber, int columnId) override
    {
        if (columnId != 1 || rowNumber < 0 || rowNumber >= 16)
            return {};

        switch (active[static_cast<std::size_t>(rowNumber)])
        {
            case Active::on:          return "Channel ON";
            case Active::offThisSong: return "Channel OFF (This Song only)";
            case Active::off:         return "Channel OFF";
        }

        return {};
    }

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
            juce::Colour::fromRGB(40, 43, 49));

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
            {
                constexpr float d = 12.0f;
                const float x = 7.0f;
                const float y = (static_cast<float>(height) - d) * 0.5f;
                const juce::Rectangle<float> r(x, y, d, d);

                switch (active[static_cast<std::size_t>(rowNumber)])
                {
                    case Active::on:
                        g.setColour(juce::Colour::fromRGB(46, 204, 113));
                        g.fillEllipse(r);
                        break;

                    case Active::offThisSong:
                        g.setColour(juce::Colour::fromRGB(245, 166, 35));
                        g.drawEllipse(r.reduced(1.0f), 1.6f);
                        g.fillEllipse(r.reduced(4.0f));
                        break;

                    case Active::off:
                        g.setColour(juce::Colour::fromRGB(235, 64, 52));
                        g.drawEllipse(r.reduced(1.0f), 1.6f);
                        g.drawLine(r.getX() + 2.2f, r.getBottom() - 2.2f,
                                   r.getRight() - 2.2f, r.getY() + 2.2f, 1.6f);
                        break;
                }

                return;
            }

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
            juce::Colour::fromRGB(40, 43, 49));

        g.fillRect(
            width - 1,
            0,
            1,
            height);
    }

private:
    using Clock = std::chrono::steady_clock;

    std::shared_ptr<const Song> song;

    std::array<Active, 16> active { };
    std::function<void(int, bool)> muteCallback;

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

void MainComponent::setTranspose(int semitones)
{
    transpose = juce::jlimit(-12, 12, semitones);

    transposeValue.setText(
        transpose > 0 ? "+" + juce::String(transpose) : juce::String(transpose),
        juce::dontSendNotification);

    synthEngine.setTranspose(transpose);
    webServer.setTranspose(transpose);
}

void MainComponent::updateChannelModel()
{
    if (channelModel != nullptr)
    {
        channelModel->setSong(currentSong);
        channelModel->resetThisSongOnly();
    }

    channelTable.updateContent();
}

juce::MouseCursor MainComponent::getMouseCursor()
{
    // Alle onderdelen zonder eigen cursor nemen die van hun parent over,
    // dus dit geldt overal in het venster zolang er een song gesleept wordt.
    if (isDragAndDropActive()
        && getCurrentDragDescription().toString().startsWith("song:")
        && juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown())
    {
        return juce::MouseCursor::CopyingCursor;
    }

    return juce::AudioAppComponent::getMouseCursor();
}

void MainComponent::paintOverChildren(juce::Graphics& g)
{
    g.setColour(familyBorderColour);

    for (const auto& block : familyBlockBounds)
    {
        if (block.isEmpty())
            continue;

        g.drawRoundedRectangle(block.toFloat().reduced(0.5f), 8.0f, 1.0f);
    }
}

void MainComponent::syncFamilySettingsToWebServer()
{
    std::array<int, 8> volumes{};
    std::array<bool, 8> enabled{};

    for (std::size_t i = 0; i < familyControls.size(); ++i)
    {
        volumes[i] = juce::roundToInt(
            familyControls[i].slider.getValue());

        enabled[i] =
            familyControls[i].labelButton.getToggleState();
    }

    webServer.setFamilySettings(volumes, enabled);
}

void MainComponent::timerCallback()
{
    updateTransportDisplay();
    channelTable.repaint();

    webServer.setPositionSamples(synthEngine.positionSamples());
    webServer.setTransportInfo(
        synthEngine.isPlaying(),
        nextSetlistEntry() != nullptr
            ? nextSetlistEntry()->shownName(false).toStdString()
            : std::string());

    const auto familySettings = synthEngine.getAllFamilySettings();

    for (std::size_t i = 0; i < familyControls.size(); ++i)
    {
        auto& control = familyControls[i];
        const auto& settings = familySettings[i];

        if (juce::roundToInt(control.slider.getValue()) != settings.volume)
        {
            control.slider.setValue(
                settings.volume,
                juce::dontSendNotification);
        }

        const auto displayedValue = juce::String(settings.volume);

        if (control.valueLabel.getText() != displayedValue)
        {
            control.valueLabel.setText(
                displayedValue,
                juce::dontSendNotification);
        }

        if (control.labelButton.getToggleState() != settings.enabled)
        {
            control.labelButton.setToggleState(
                settings.enabled,
                juce::dontSendNotification);
        }
    }

    // Ook wijzigingen van buiten de GUI (MIDI-controller) naar de webpagina.
    syncFamilySettingsToWebServer();

    updateControllerLeds();
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
        [this](int family, int volume, bool enabled)
        {
            synthEngine.setFamilyVolume(family, volume);
            synthEngine.setFamilyEnabled(family, enabled);
        });

    // Play/Stop/Next vanuit de webpagina: uitvoeren op de GUI-thread,
    // net als een klik op de knoppen.
    webServer.setControlCallback(
        [safeThis = juce::Component::SafePointer<MainComponent>(this)]
        (const std::string& command)
        {
            juce::MessageManager::callAsync(
                [safeThis, command]
                {
                    if (safeThis == nullptr)
                        return;

                    if (command == "play")
                        safeThis->play();
                    else if (command == "stop")
                        safeThis->stop();
                    else if (command == "next")
                        safeThis->skipToNextSong();
                });
        });

    // Bediening via de webpagina: standaard sleutel "merik"; aanpasbaar
    // in Settings. Leeg = bediening volledig uit.
    if (auto* properties = appProperties.getUserSettings())
        webControlKey = properties->getValue("webControlKey", "merik");

    webServer.setControlKey(webControlKey.toStdString());

    webServer.start(WebServer::defaultPort);

    // -------------------------------------------------------------------------
    // MIDI Control
    // -------------------------------------------------------------------------

    midiControlLabel.setText(
        "MIDI control in",
        juce::dontSendNotification);

    midiControlLabel.setColour(
        juce::Label::textColourId,
        secondaryTextColour);


    // Gevuld door refreshMidiInputs(): "None" + beschikbare MIDI-ingangen.
    midiControlBox.onChange = [this]
    {
        const int id = midiControlBox.getSelectedId();

        desiredMidiIn = (id >= 2 && id - 2 < midiInIdentifiers.size())
                            ? midiInIdentifiers[id - 2]
                            : juce::String();

        if (auto* properties = appProperties.getUserSettings())
        {
            properties->setValue("midiInIdentifier", desiredMidiIn);
            properties->saveIfNeeded();
        }

        applyMidiInput();
    };

    if (auto* properties = appProperties.getUserSettings())
    {
        if (properties->containsKey("midiInIdentifier"))
        {
            desiredMidiIn = properties->getValue("midiInIdentifier", "");
        }
        else
        {
            // Eerste keer: een nanoKONTROL vanzelf kiezen als die aangesloten is.
            for (const auto& device : juce::MidiInput::getAvailableDevices())
            {
                if (device.name.containsIgnoreCase("nanokontrol"))
                {
                    desiredMidiIn = device.identifier;
                    break;
                }
            }
        }
    }

    midiControlBox.setColour(juce::ComboBox::textColourId, textColour);

    midiControlBox.setLookAndFeel(&merikLookAndFeel);


    // -------------------------------------------------------------------------
    // MIDI Out
    // -------------------------------------------------------------------------

    midiOutLabel.setText(
        "MIDI Out",
        juce::dontSendNotification);

    midiOutLabel.setColour(
        juce::Label::textColourId,
        secondaryTextColour);


    // Gevuld door refreshMidiOutputs(): "Soundfont player" + beschikbare apparaten.
    midiOutBox.setTooltip("Soundfont player = interne FluidSynth; anders gaat de MIDI naar het gekozen apparaat");
    midiOutBox.onChange = [this]
    {
        const int id = midiOutBox.getSelectedId();

        desiredMidiOut = (id >= 2 && id - 2 < midiOutIdentifiers.size())
                             ? midiOutIdentifiers[id - 2]
                             : juce::String();

        if (auto* properties = appProperties.getUserSettings())
        {
            properties->setValue("midiOutIdentifier", desiredMidiOut);
            properties->saveIfNeeded();
        }

        applyMidiOutput();
    };

    if (auto* properties = appProperties.getUserSettings())
        desiredMidiOut = properties->getValue("midiOutIdentifier", "");

    // LED-uitgang van de controller (Auto = dezelfde controller als de ingang).
    ledOutBox.onChange = [this]
    {
        const int id = ledOutBox.getSelectedId();

        ledChoice = id == 2 ? juce::String("none")
                  : (id >= 3 && id - 3 < ledIdentifiers.size()) ? ledIdentifiers[id - 3]
                                                                : juce::String("auto");

        if (auto* properties = appProperties.getUserSettings())
        {
            properties->setValue("ledOutChoice", ledChoice);
            properties->saveIfNeeded();
        }

        applyLedOutput();
    };

    if (auto* properties = appProperties.getUserSettings())
        ledChoice = properties->getValue("ledOutChoice", "auto");

    ledLast.fill(-1);

    loadMidiMapping();

    // Klank van de interne synth (reverb, chorus, gain).
    if (auto* properties = appProperties.getUserSettings())
    {
        SynthSettings synth;
        synth.gain          = properties->getIntValue("synth.gain", synth.gain);
        synth.reverbRoom    = properties->getIntValue("synth.reverbRoom", synth.reverbRoom);
        synth.reverbDamp    = properties->getIntValue("synth.reverbDamp", synth.reverbDamp);
        synth.reverbWidth   = properties->getIntValue("synth.reverbWidth", synth.reverbWidth);
        synth.reverbLevel   = properties->getIntValue("synth.reverbLevel", synth.reverbLevel);
        synthEngine.setSynthSettings(synth);
    }

    midiDeviceListConnection =
        juce::MidiDeviceListConnection::make([this]
        {
            refreshMidiOutputs();
            refreshMidiInputs();
            refreshLedOutputs();
        });

    refreshMidiOutputs();
    refreshMidiInputs();
    refreshLedOutputs();

    midiOutBox.setColour(juce::ComboBox::textColourId, textColour);

    midiOutBox.setLookAndFeel(&merikLookAndFeel);


    // -------------------------------------------------------------------------
    // SoundFont
    // -------------------------------------------------------------------------

    soundFontLabel.setText(
        "SoundFont",
        juce::dontSendNotification);

    soundFontLabel.setColour(
        juce::Label::textColourId,
        secondaryTextColour);


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

            syncFamilySettingsToWebServer();

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

        control.valueLabel.setFont(makeNotoFont(20.0f));

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
            juce::Colour(52, 55, 62));

        control.slider.onValueChange = [this, i]
        {
            auto& family = familyControls[i];

            const int value =
                juce::roundToInt(
                    family.slider.getValue());

            family.valueLabel.setText(
                juce::String(value),
                juce::dontSendNotification);

            synthEngine.setFamilyVolume(
                static_cast<int>(i),
                value);

            syncFamilySettingsToWebServer();
        };

        syncFamilySettingsToWebServer();
        addAndMakeVisible(control.slider);
    }
    
    // -------------------------------------------------------------------------
    // Player
    // -------------------------------------------------------------------------

    // Song title
    songTitle.setText("No MIDI loaded", juce::dontSendNotification);
    songTitle.setColour(juce::Label::textColourId, textColour);
    // songTitle.setColour(juce::Label::backgroundColourId, juce::Colours::lime);
    //songTitle.setFont(makeNotoFont(60.0f));
    songTitle.setFont(
        merikLookAndFeel.getBoldFont(36.0f));
    songTitle.setJustificationType(juce::Justification::centred);
    songTitle.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(songTitle);

    nextSong.setText(
        "Next: -",
        juce::dontSendNotification);

    nextSong.setColour(
        juce::Label::textColourId,
        secondaryTextColour);
    // nextSong.setColour(juce::Label::backgroundColourId, juce::Colours::red);

    nextSong.setFont(makeNotoFont(26.0f));

    nextSong.setJustificationType(
        juce::Justification::centred);

    addAndMakeVisible(nextSong);

    elapsedTime.setText(
        "00:00",
        juce::dontSendNotification);

    elapsedTime.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    elapsedTime.setFont(makeNotoFont(20.0f));

    addAndMakeVisible(elapsedTime);

    totalTime.setText(
        "00:00",
        juce::dontSendNotification);

    totalTime.setColour(
        juce::Label::textColourId,
        secondaryTextColour);

    totalTime.setJustificationType(
        juce::Justification::centredRight);

    totalTime.setFont(makeNotoFont(20.0f));

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
        juce::Colour::fromRGB(52, 55, 62));

    positionSlider.onDragStart = [this]
    {
        positionSliderDragging = true;

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
        // De sliderwaarde is nu de bedoelde positie; lees die eerst,
        // pas daarna mag de timer de slider weer bijwerken.
        const double sliderValue = positionSlider.getValue();
        positionSliderDragging = false;

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
                sliderValue);

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

    nextButton.onClick = [this]
    {
        skipToNextSong();
    };

    addAndMakeVisible(nextButton);

    // Transpose: één afgeronde capsule met - / waarde / +
    transposeDownButton.setButtonText(juce::String::fromUTF8("\xe2\x88\x92"));
    transposeUpButton.setButtonText("+");

    for (auto* button : { &transposeDownButton, &transposeUpButton })
    {
        button->getProperties().set("flat", true);
        button->setColour(juce::TextButton::textColourOffId, textColour);
        button->setTooltip("Transpose (semitones)");
        addAndMakeVisible(*button);
    }

    transposeDownButton.onClick = [this] { setTranspose(transpose - 1); };
    transposeUpButton.onClick   = [this] { setTranspose(transpose + 1); };

    transposeValue.setText("0", juce::dontSendNotification);
    transposeValue.setColour(juce::Label::textColourId, textColour);
    transposeValue.setFont(merikLookAndFeel.getMediumFont(17.0f));
    transposeValue.setJustificationType(juce::Justification::centred);
    transposeValue.setTooltip("Transpose (semitones)");
    addAndMakeVisible(transposeValue);

    panicButton.setButtonText("Panic");

    panicButton.setColour(
        juce::TextButton::buttonColourId,
        panelLightColour);

    panicButton.setColour(
        juce::TextButton::textColourOffId,
        textColour);

    panicButton.setTooltip("Stop all sounding notes immediately");
    panicButton.onClick = [this] { panic(); };

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
        juce::URL("http://localhost:" + juce::String(WebServer::defaultPort)).launchInDefaultBrowser();
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
        merikLookAndFeel.getMediumFont(17.0f));

    setlistsTitle.setColour(
        juce::Label::backgroundColourId,
        juce::Colour::fromRGB(38, 41, 47));

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
        merikLookAndFeel.getMediumFont(17.0f));

    songsTitle.setColour(
        juce::Label::backgroundColourId,
        juce::Colour::fromRGB(38, 41, 47));

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


    // -------------------------------------------------------------------------
    // Toggle buttons: Dbl Click Plays / Normalize / Continuous Play
    // -------------------------------------------------------------------------

    doubleClickToggle.setButtonText("Direct Play");
    normalizeToggle.setButtonText("Normalize");
    continuousToggle.setButtonText("Auto Next");

    for (auto* button :
        { &doubleClickToggle, &normalizeToggle, &continuousToggle })
    {
        button->setColour(
            juce::TextButton::buttonColourId,
            panelLightColour);

        button->setColour(
            juce::TextButton::buttonOnColourId,
            merikBlue);

        button->setColour(
            juce::TextButton::textColourOffId,
            juce::Colours::white);

        button->setColour(
            juce::TextButton::textColourOnId,
            juce::Colours::white);

        button->setClickingTogglesState(true);

        // Alleen toestand-houders; bediening zit in het settingsvenster.
    }

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

    setlistBox.setRowHeight(19);
    songBox.setRowHeight(19);

    refreshSetlistModel();
    refreshSongModel();

    setlistModel->onContextMenu = [this](int row) { showSetlistMenu(row); };
    setlistModel->onDeleteKey   = [this](int row) { removeSetlistAt(row); };

    songModel->onContextMenu = [this](int row) { showSongMenu(row); };
    songModel->onDeleteKey   = [this](int row) { removeSongAt(row); };

    songModel->isCurrentSong = [this](const juce::File& file)
    {
        return currentSongFromSetlist && file == currentMidiFile;
    };

    songModel->onDoubleClick = [this](int row)
    {
        if (const auto* found = songModel->getEntry(row))
        {
            const SongEntry entry = *found;
            loadMidiFile(entry.file, true, &entry);

            if (doubleClickToggle.getToggleState())
                play();
        }
    };

    // ---- slepen: setlists herschikken, songs herschikken/verplaatsen/kopiëren
    auto parseSong = [](const juce::String& description, int& setlist, int& row)
    {
        const auto parts = juce::StringArray::fromTokens(description, ":", "");

        if (parts.size() != 3 || parts[0] != "song")
            return false;

        setlist = parts[1].getIntValue();
        row = parts[2].getIntValue();
        return true;
    };

    setlistBox.accepts = [](const juce::String& description)
    {
        return description.startsWith("setlist:") || description.startsWith("song:");
    };

    setlistBox.visualFor =
        [this, parseSong](const juce::String& description, juce::Point<int> position)
        {
            DropListBox::Visual visual;

            if (description.startsWith("setlist:"))
            {
                visual.insertIndex = setlistBox.getInsertionIndexForPosition(position.x, position.y);
                return visual;
            }

            int sourceSetlist = 0, sourceRow = 0;
            const int target = setlistBox.getRowContainingPosition(position.x, position.y);

            if (parseSong(description, sourceSetlist, sourceRow) && target >= 0 && target != sourceSetlist)
            {
                visual.highlightRow = target;
                visual.canCopy = true;
            }

            return visual;
        };

    setlistBox.onDrop =
        [this, parseSong](const juce::String& description, juce::Point<int> position, bool copy)
        {
            if (description.startsWith("setlist:"))
            {
                moveSetlist(description.fromFirstOccurrenceOf(":", false, false).getIntValue(),
                            setlistBox.getInsertionIndexForPosition(position.x, position.y));
                return;
            }

            int sourceSetlist = 0, sourceRow = 0;
            const int target = setlistBox.getRowContainingPosition(position.x, position.y);

            if (parseSong(description, sourceSetlist, sourceRow)
                && sourceSetlist == selectedSetlist && target >= 0)
                moveOrCopySong(sourceRow, target, copy);
        };

    songBox.accepts = [](const juce::String& description)
    {
        return description.startsWith("song:");
    };

    songBox.visualFor =
        [this](const juce::String&, juce::Point<int> position)
        {
            DropListBox::Visual visual;
            visual.insertIndex = songBox.getInsertionIndexForPosition(position.x, position.y);
            return visual;
        };

    songBox.onDrop =
        [this, parseSong](const juce::String& description, juce::Point<int> position, bool)
        {
            int sourceSetlist = 0, sourceRow = 0;

            if (parseSong(description, sourceSetlist, sourceRow) && sourceSetlist == selectedSetlist)
                moveSongWithinSetlist(sourceRow,
                                      songBox.getInsertionIndexForPosition(position.x, position.y));
        };

    setlistBox.setColour(
        juce::ListBox::backgroundColourId,
        panelDarkColour);
        
    
    songBox.setColour(
        juce::ListBox::backgroundColourId,
        panelDarkColour);



    addAndMakeVisible(setlistBox);
    addAndMakeVisible(songBox);

    // Beide paren hebben dezelfde vorm: "+ ..." voegt toe, "- ..." haalt weg.
    const auto minus = juce::String::fromUTF8("\xe2\x88\x92");

    newSetlistButton.setButtonText("+ Setlist");
    deleteSetlistButton.setButtonText(minus + " Setlist");
    addButton.setButtonText("+ Song");
    removeButton.setButtonText(minus + " Song");

    newSetlistButton.setTooltip("Add a new setlist");
    deleteSetlistButton.setTooltip("Remove the selected setlist (or right-click it)");
    addButton.setTooltip("Add MIDI files to this setlist (or drop them on the window)");
    removeButton.setTooltip("Remove the selected song from this setlist");

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
    newSetlistButton.onClick = [this] { addSetlist(); };

    deleteSetlistButton.onClick = [this] { removeSetlistAt(selectedSetlist); };

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

    removeButton.onClick = [this] { removeSongAt(songBox.getSelectedRow()); };

    doubleClickToggle.onClick = [this]
    {
        savePersistentState();
        if (syncSettingsToggles) syncSettingsToggles();
    };

    normalizeToggle.onClick = [this]
    {
        synthEngine.setNormalizeEnabled( 
            normalizeToggle.getToggleState());

        channelTable.repaint();
        savePersistentState();
        if (syncSettingsToggles) syncSettingsToggles();
    };

    continuousToggle.onClick = [this]
    {
        savePersistentState();
        if (syncSettingsToggles) syncSettingsToggles();
    };


    // -------------------------------------------------------------------------
    // Channel table
    // -------------------------------------------------------------------------

    channelModel =
        std::make_unique<ChannelModel>();
    channelModel->setSynthEngine(&synthEngine);
    channelModel->setMuteCallback(
        [this](int channel, bool muted)
        {
            synthEngine.setChannelMuted(channel, muted);
        });

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
        juce::Colour::fromRGB(48, 51, 58));

    channelTable.getHeader().setColour(
        juce::TableHeaderComponent::backgroundColourId,
        juce::Colour::fromRGB(38, 41, 47));

    channelTable.getHeader().setColour(
        juce::TableHeaderComponent::textColourId,
        juce::Colours::white);

    channelTable.getHeader().setColour(
        juce::TableHeaderComponent::outlineColourId,
        juce::Colour::fromRGB(52, 55, 62));
        
    
    channelTable.setRowHeight(14);
    channelTable.getHeader().setStretchToFitActive(false);
    channelTable.setHeaderHeight(20);

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

    // De engine mag het MIDI-uitgangsobject niet meer gebruiken als dat verdwijnt.
    synthEngine.setExternalOutput(nullptr);
    midiOutSink.reset();

    if (midiIn != nullptr)
        midiIn->stop();

    midiIn.reset();

    ledsAllOff();
    ledOut.reset();
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
    synthEngine.setNormalizeEnabled( 
        normalizeToggle.getToggleState());

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
                SongEntry entry;
                entry.file = juce::File(path);

                const auto base =
                    "setlist." + juce::String(i) + ".song." + juce::String(j);

                entry.displayName = properties->getValue(base + ".name", "");
                entry.transpose = properties->getIntValue(base + ".transpose", 0);

                setlist.songs.push_back(std::move(entry));
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
                setlists[i].songs[j].file.getFullPathName());

            const auto base =
                "setlist." + index + ".song." + juce::String(static_cast<int>(j));

            properties->setValue(base + ".name", setlists[i].songs[j].displayName);
            properties->setValue(base + ".transpose", setlists[i].songs[j].transpose);
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

    // Transpose-capsule
    if (!transposeBounds.isEmpty())
    {
        g.setColour(panelLightColour);
        g.fillRoundedRectangle(transposeBounds.toFloat(),
                               static_cast<float>(transposeBounds.getHeight()) * 0.5f);
    }

    // Family-blokken als afgeronde kaarten
    for (const auto& block : familyBlockBounds)
    {
        if (block.isEmpty())
            continue;

        g.setColour(familyBlockColour);
        g.fillRoundedRectangle(block.toFloat(), 8.0f);
    }

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
        bounds.removeFromTop(86).reduced(10,0);

    header.removeFromTop(6);

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
            area.reduced(1, 0));

        if (i < familyControls.size() - 1)
            familyArea.removeFromLeft(familyGap);
    }

    // -------------------------------------------------------------------------
    // Player
    // -------------------------------------------------------------------------

    auto player =
        bounds.removeFromTop(179).reduced(10,5);

    auto titleArea =
        player.removeFromTop(48);
    //titleArea.translate(0, -10);
    songTitle.setBounds(titleArea);

    nextSong.setBounds(
        player.removeFromTop(36));

    auto progressArea =
        player.removeFromTop(35);

    elapsedTime.setBounds(
        progressArea.removeFromLeft(60));

    totalTime.setBounds(
        progressArea.removeFromRight(60));

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

    transposeBounds =
        transport.removeFromLeft(112).withHeight(buttonHeight);

    {
        auto area = transposeBounds;

        transposeDownButton.setBounds(area.removeFromLeft(34));
        transposeUpButton.setBounds(area.removeFromRight(34));
        transposeValue.setBounds(area);
    }

    // ------------------------------------------------------------
    // Rechts: Panic / Lyrics / Channels
    // ------------------------------------------------------------

    constexpr int rightButtonGap = 6;
    constexpr int channelsWidth = 80;
    constexpr int lyricsWidth = 80;
    constexpr int panicWidth = 80;
    constexpr int settingsWidth = 80;

    const int rightGroupWidth =
        settingsWidth
        + rightButtonGap
        + panicWidth
        + rightButtonGap
        + lyricsWidth
        + rightButtonGap
        + channelsWidth;

    auto rightGroup =
        transport.removeFromRight(rightGroupWidth);

    settingsButton.setBounds(
        rightGroup.removeFromLeft(settingsWidth)
            .withHeight(buttonHeight));

    rightGroup.removeFromLeft(rightButtonGap);

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
        setlistArea.removeFromTop(20));

    auto setlistButtons = setlistArea.removeFromBottom(30);
    setlistArea.removeFromBottom(6);
    setlistBox.setBounds(setlistArea);

    newSetlistButton.setBounds(
        setlistButtons.removeFromLeft(90));

    setlistButtons.removeFromLeft(6);

    deleteSetlistButton.setBounds(
        setlistButtons.removeFromLeft(90));

    setlistBox.setBounds(
        setlistArea);

    // -------------------------------------------------------------------------
    // Songs - rechter helft
    // -------------------------------------------------------------------------

    auto songArea = lists;

    songsTitle.setBounds(
        songArea.removeFromTop(20));

    auto songButtons = songArea.removeFromBottom(30);
    songArea.removeFromBottom(6);
    songBox.setBounds(songArea);

    addButton.setBounds(
        songButtons.removeFromLeft(90));

    songButtons.removeFromLeft(6);

    removeButton.setBounds(
        songButtons.removeFromLeft(90));

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


// ============================================================================
// Setlists en songs: toevoegen, verwijderen, herschikken, hernoemen
// ============================================================================

void MainComponent::promptRename(
    const juce::String& title,
    const juce::String& current,
    std::function<void(const juce::String&)> onAccepted)
{
    auto* window = new juce::AlertWindow(
        title, {}, juce::MessageBoxIconType::NoIcon, this);

    window->setLookAndFeel(&merikLookAndFeel);
    window->addTextEditor("name", current, {});
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    if (auto* editor = window->getTextEditor("name"))
        editor->selectAll();

    juce::Component::SafePointer<juce::AlertWindow> safeWindow(window);

    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeWindow, onAccepted = std::move(onAccepted)](int result)
            {
                if (result == 1 && safeWindow != nullptr)
                    onAccepted(safeWindow->getTextEditorContents("name").trim());
            }),
        true);
}

void MainComponent::addSetlist()
{
    int number = 1;

    while (true)
    {
        const auto candidate = "Setlist " + juce::String(number);

        const bool exists =
            std::any_of(
                setlists.begin(), setlists.end(),
                [&candidate](const SetlistData& setlist)
                {
                    return setlist.name == candidate;
                });

        if (!exists)
        {
            setlists.push_back({ candidate, {} });
            break;
        }

        ++number;
    }

    selectedSetlist = static_cast<int>(setlists.size()) - 1;

    refreshSetlistModel();
    refreshSongModel();
    savePersistentState();
}

void MainComponent::removeSetlistAt(int row)
{
    if (row < 0 || row >= static_cast<int>(setlists.size()) || setlists.size() <= 1)
        return;

    const auto remove = [this, row]
    {
        if (row >= static_cast<int>(setlists.size()))
            return;

        setlists.erase(setlists.begin() + row);

        if (row < selectedSetlist)
            --selectedSetlist;

        selectedSetlist = juce::jlimit(0, static_cast<int>(setlists.size()) - 1, selectedSetlist);

        refreshSetlistModel();
        refreshSongModel();
        savePersistentState();
    };

    const auto& setlist = setlists[static_cast<std::size_t>(row)];

    // Een lege setlist verdwijnt zonder vragen; anders eerst bevestigen.
    if (setlist.songs.empty())
    {
        remove();
        return;
    }

    auto* window = new juce::AlertWindow(
        "Remove setlist",
        "Remove \"" + setlist.name + "\" with "
            + juce::String(static_cast<int>(setlist.songs.size()))
            + " songs?\nThe MIDI files themselves are not deleted.",
        juce::MessageBoxIconType::QuestionIcon, this);

    window->setLookAndFeel(&merikLookAndFeel);
    window->addButton("Remove", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [remove](int result)
            {
                if (result == 1)
                    remove();
            }),
        true);
}

void MainComponent::removeSongAt(int row)
{
    if (row < 0 || songModel == nullptr || !songModel->removeFile(row))
        return;

    refreshSongModel();

    const int remaining = songModel->getNumRows();

    if (remaining > 0)
        songBox.selectRow(juce::jmin(row, remaining - 1));

    setlistBox.repaint();
    savePersistentState();
}

void MainComponent::moveSetlist(int from, int insertBefore)
{
    const int count = static_cast<int>(setlists.size());

    if (from < 0 || from >= count)
        return;

    insertBefore = juce::jlimit(0, count, insertBefore);

    if (insertBefore == from || insertBefore == from + 1)
        return;

    auto item = std::move(setlists[static_cast<std::size_t>(from)]);
    setlists.erase(setlists.begin() + from);

    const int to = insertBefore > from ? insertBefore - 1 : insertBefore;
    setlists.insert(setlists.begin() + to, std::move(item));

    // De geselecteerde setlist blijft dezelfde, ook als hij verschuift.
    if (selectedSetlist == from)
    {
        selectedSetlist = to;
    }
    else
    {
        if (from < selectedSetlist)
            --selectedSetlist;

        if (to <= selectedSetlist)
            ++selectedSetlist;
    }

    refreshSetlistModel();
    refreshSongModel();
    savePersistentState();
}

void MainComponent::moveSongWithinSetlist(int from, int insertBefore)
{
    if (selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
        return;

    auto& songs = setlists[static_cast<std::size_t>(selectedSetlist)].songs;
    const int count = static_cast<int>(songs.size());

    if (from < 0 || from >= count)
        return;

    insertBefore = juce::jlimit(0, count, insertBefore);

    if (insertBefore == from || insertBefore == from + 1)
        return;

    auto item = std::move(songs[static_cast<std::size_t>(from)]);
    songs.erase(songs.begin() + from);

    const int to = insertBefore > from ? insertBefore - 1 : insertBefore;
    songs.insert(songs.begin() + to, std::move(item));

    refreshSongModel();
    songBox.selectRow(to);
    savePersistentState();
}

void MainComponent::moveOrCopySong(int songRow, int targetSetlist, bool copy)
{
    const int setlistCount = static_cast<int>(setlists.size());

    if (selectedSetlist < 0 || selectedSetlist >= setlistCount
        || targetSetlist < 0 || targetSetlist >= setlistCount
        || targetSetlist == selectedSetlist)
    {
        return;
    }

    auto& source = setlists[static_cast<std::size_t>(selectedSetlist)].songs;
    auto& target = setlists[static_cast<std::size_t>(targetSetlist)].songs;

    if (songRow < 0 || songRow >= static_cast<int>(source.size()))
        return;

    const SongEntry entry = source[static_cast<std::size_t>(songRow)];

    // Staat het nummer er al in, dan verandert er niets.
    const bool alreadyThere =
        std::any_of(target.begin(), target.end(),
                    [&entry](const SongEntry& existing) { return existing.file == entry.file; });

    if (alreadyThere)
        return;

    target.push_back(entry);

    if (!copy)
        source.erase(source.begin() + songRow);

    refreshSongModel();
    setlistBox.repaint();
    savePersistentState();
}

void MainComponent::showSetlistMenu(int row)
{
    if (row < 0 || row >= static_cast<int>(setlists.size()))
        return;

    setlistBox.selectRow(row);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&merikLookAndFeel);
    menu.addItem(1, "Rename...");
    menu.addItem(2, "Remove", setlists.size() > 1);

    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [this, row](int result)
        {
            if (row >= static_cast<int>(setlists.size()))
                return;

            if (result == 1)
            {
                promptRename(
                    "Rename setlist",
                    setlists[static_cast<std::size_t>(row)].name,
                    [this, row](const juce::String& name)
                    {
                        if (name.isEmpty() || row >= static_cast<int>(setlists.size()))
                            return;

                        setlists[static_cast<std::size_t>(row)].name = name;
                        setlistBox.repaint();
                        savePersistentState();
                    });
            }
            else if (result == 2)
            {
                removeSetlistAt(row);
            }
        });
}

void MainComponent::showSongMenu(int row)
{
    const auto* found = songModel != nullptr ? songModel->getEntry(row) : nullptr;

    if (found == nullptr)
        return;

    songBox.selectRow(row);

    const SongEntry entry = *found;

    juce::PopupMenu transposeMenu;

    // -6..+6 dekt alle toonsoorten (een octaaf verder is dezelfde toonsoort).
    for (int semitones = 6; semitones >= -6; --semitones)
    {
        const auto label = semitones == 0
            ? juce::String("0  (original key)")
            : (semitones > 0 ? "+" : "") + juce::String(semitones);

        transposeMenu.addItem(100 + semitones + 6, label, true, semitones == entry.transpose);
    }

    juce::PopupMenu menu;
    menu.setLookAndFeel(&merikLookAndFeel);
    menu.addSubMenu("Transpose", transposeMenu);
    menu.addItem(1, "Rename...");
    menu.addItem(2, "Restore file name", entry.displayName.isNotEmpty());
    menu.addSeparator();
    menu.addItem(3, "Delete");

    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [this, row, entry](int result)
        {
            if (selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
                return;

            auto& songs = setlists[static_cast<std::size_t>(selectedSetlist)].songs;

            if (row < 0 || row >= static_cast<int>(songs.size())
                || songs[static_cast<std::size_t>(row)].file != entry.file)
            {
                return;
            }

            auto& target = songs[static_cast<std::size_t>(row)];
            const bool isLoaded = currentSongFromSetlist && currentMidiFile == target.file;

            if (result >= 100 && result <= 112)
            {
                target.transpose = result - 100 - 6;

                // Staat dit nummer nu aan, dan klinkt de nieuwe transpose meteen.
                if (isLoaded)
                    setTranspose(target.transpose);
            }
            else if (result == 1)
            {
                promptRename(
                    "Rename song",
                    target.shownName(false),
                    [this, row, file = entry.file](const juce::String& name)
                    {
                        if (selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
                            return;

                        auto& list = setlists[static_cast<std::size_t>(selectedSetlist)].songs;

                        if (row < 0 || row >= static_cast<int>(list.size())
                            || list[static_cast<std::size_t>(row)].file != file)
                        {
                            return;
                        }

                        auto& song = list[static_cast<std::size_t>(row)];

                        // De echte bestandsnaam blijft bestaan; leeg of gelijk
                        // aan de bestandsnaam = geen eigen naam.
                        song.displayName =
                            (name.isEmpty() || name == song.file.getFileNameWithoutExtension())
                                ? juce::String()
                                : name;

                        if (currentSongFromSetlist && currentMidiFile == song.file)
                        {
                            currentDisplayName = song.displayName;
                            updateSongDisplay();
                            webServer.setSongTitle(
                                (song.displayName.isNotEmpty() ? song.displayName : juce::String()).toStdString());
                        }

                        songBox.repaint();
                        savePersistentState();
                    });

                return;
            }
            else if (result == 2)
            {
                target.displayName = {};

                if (isLoaded)
                {
                    currentDisplayName = {};
                    updateSongDisplay();
                    webServer.setSongTitle({});
                }
            }
            else if (result == 3)
            {
                removeSongAt(row);
                return;
            }
            else
            {
                return;
            }

            songBox.repaint();
            savePersistentState();
        });
}


// ============================================================================
// MIDI out: Soundfont player of extern apparaat
// ============================================================================

void MainComponent::refreshMidiOutputs()
{
    const auto devices = juce::MidiOutput::getAvailableDevices();

    midiOutBox.clear(juce::dontSendNotification);
    midiOutIdentifiers.clear();

    midiOutBox.addItem("Soundfont player", 1);

    int id = 2;

    for (const auto& device : devices)
    {
        midiOutBox.addItem(device.name, id++);
        midiOutIdentifiers.add(device.identifier);
    }

    // Terug naar de gewenste keuze; een apparaat dat (nog) niet bestaat
    // valt tijdelijk terug op de interne synth en wordt automatisch weer
    // gebruikt zodra het opnieuw verschijnt.
    const int index = midiOutIdentifiers.indexOf(desiredMidiOut);

    midiOutBox.setSelectedId(index >= 0 ? index + 2 : 1, juce::dontSendNotification);

    applyMidiOutput();
}

void MainComponent::applyMidiOutput()
{
    const bool available = desiredMidiOut.isNotEmpty()
                           && midiOutIdentifiers.contains(desiredMidiOut);

    if (!available)
    {
        synthEngine.setExternalOutput(nullptr);
        midiOutSink.reset();
        openedMidiOut = {};
        return;
    }

    // Al open? Dan niets doen (de lijst met apparaten ververst vaker).
    if (midiOutSink != nullptr && midiOutSink->isOpen() && openedMidiOut == desiredMidiOut)
        return;

    auto newSink = std::make_unique<MidiOutSink>();

    if (!newSink->open(desiredMidiOut))
    {
        desiredMidiOut = {};
        midiOutBox.setSelectedId(1, juce::dontSendNotification);
        synthEngine.setExternalOutput(nullptr);
        midiOutSink.reset();
        openedMidiOut = {};

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "MIDI out",
            "Could not open this MIDI output. The Soundfont player is used instead.");
        return;
    }

    // Eerst de engine omzetten, dan pas de vorige uitgang opruimen.
    synthEngine.setExternalOutput(newSink.get());
    midiOutSink = std::move(newSink);
    openedMidiOut = desiredMidiOut;
}


// ============================================================================
// Panic
// ============================================================================

void MainComponent::panic()
{
    synthEngine.panic();
}


// ============================================================================
// MIDI control in (nanoKONTROL2): zelfde toewijzing als Merik 1
//
//   CC 0..7                    -> volume van family 1..8 (de faders)
//   CC 41 (waarde 127)         -> Play / Pauze
//   CC 42 (waarde 127)         -> Stop
//   CC 44 (waarde 127)         -> Next
//   CC 45 (waarde 127)         -> Panic
// ============================================================================

void MainComponent::refreshMidiInputs()
{
    const auto devices = juce::MidiInput::getAvailableDevices();

    midiControlBox.clear(juce::dontSendNotification);
    midiInIdentifiers.clear();

    midiControlBox.addItem("None", 1);

    int id = 2;

    for (const auto& device : devices)
    {
        midiControlBox.addItem(device.name, id++);
        midiInIdentifiers.add(device.identifier);
    }

    const int index = midiInIdentifiers.indexOf(desiredMidiIn);

    midiControlBox.setSelectedId(index >= 0 ? index + 2 : 1, juce::dontSendNotification);

    applyMidiInput();
}

void MainComponent::applyMidiInput()
{
    const bool available = desiredMidiIn.isNotEmpty()
                           && midiInIdentifiers.contains(desiredMidiIn);

    if (!available)
    {
        if (midiIn != nullptr)
            midiIn->stop();

        midiIn.reset();
        openedMidiIn = {};
        applyLedOutput();
        return;
    }

    if (midiIn != nullptr && openedMidiIn == desiredMidiIn)
        return;

    if (midiIn != nullptr)
        midiIn->stop();

    midiIn = juce::MidiInput::openDevice(desiredMidiIn, this);

    if (midiIn == nullptr)
    {
        openedMidiIn = {};
        desiredMidiIn = {};
        midiControlBox.setSelectedId(1, juce::dontSendNotification);

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "MIDI control in",
            "Could not open this MIDI input.");
        return;
    }

    midiIn->start();
    openedMidiIn = desiredMidiIn;

    applyLedOutput();
}

// Draait op de MIDI-thread van het systeem.
void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    if (!message.isController())
        return;

    const int cc = message.getControllerNumber();
    const int value = message.getControllerValue();

    lastMidiInCc = cc;
    lastMidiInValue = value;

    // Faders -> family-volume (de slider volgt via de GUI-timer).
    for (int i = 0; i < MidiMapping::families; ++i)
        if (cc == midiMapping.fader[static_cast<std::size_t>(i)].load())
            synthEngine.setFamilyVolume(i, value);

    // Knoppen sturen 127 bij indrukken en 0 bij loslaten.
    if (value != 127)
        return;

    // 'R'-knoppen: family aan/uit.
    for (int i = 0; i < MidiMapping::families; ++i)
    {
        if (cc == midiMapping.enable[static_cast<std::size_t>(i)].load())
        {
            synthEngine.setFamilyEnabled(i, !synthEngine.getFamilySettings(i).enabled);
            return;
        }
    }

    const char* command = cc == midiMapping.play.load()  ? "play"
                        : cc == midiMapping.stop.load()  ? "stop"
                        : cc == midiMapping.next.load()  ? "next"
                        : cc == midiMapping.panic.load() ? "panic"
                                                         : nullptr;

    if (command == nullptr)
        return;

    juce::MessageManager::callAsync(
        [safeThis = juce::Component::SafePointer<MainComponent>(this), cmd = juce::String(command)]
        {
            if (safeThis == nullptr)
                return;

            if (cmd == "play")
                safeThis->play();
            else if (cmd == "stop")
                safeThis->stop();
            else if (cmd == "next")
                safeThis->skipToNextSong();
            else if (cmd == "panic")
                safeThis->panic();
        });
}


// ============================================================================
// Controller-toewijzing en synth-klank bewaren
// ============================================================================

void MainComponent::loadMidiMapping()
{
    auto* properties = appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    for (int i = 0; i < MidiMapping::families; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        midiMapping.fader[index] = properties->getIntValue("midi.fader." + juce::String(i), i);
        midiMapping.enable[index] = properties->getIntValue("midi.enable." + juce::String(i), 64 + i);
    }

    midiMapping.play = properties->getIntValue("midi.play", 41);
    midiMapping.stop = properties->getIntValue("midi.stop", 42);
    midiMapping.next = properties->getIntValue("midi.next", 44);
    midiMapping.panic = properties->getIntValue("midi.panic", 45);
}

void MainComponent::saveMidiMapping()
{
    auto* properties = appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    for (int i = 0; i < MidiMapping::families; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        properties->setValue("midi.fader." + juce::String(i), midiMapping.fader[index].load());
        properties->setValue("midi.enable." + juce::String(i), midiMapping.enable[index].load());
    }

    properties->setValue("midi.play", midiMapping.play.load());
    properties->setValue("midi.stop", midiMapping.stop.load());
    properties->setValue("midi.next", midiMapping.next.load());
    properties->setValue("midi.panic", midiMapping.panic.load());
    properties->saveIfNeeded();
}

void MainComponent::saveSynthSettings(const SynthSettings& synth)
{
    auto* properties = appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    properties->setValue("synth.gain", synth.gain);
    properties->setValue("synth.reverbRoom", synth.reverbRoom);
    properties->setValue("synth.reverbDamp", synth.reverbDamp);
    properties->setValue("synth.reverbWidth", synth.reverbWidth);
    properties->setValue("synth.reverbLevel", synth.reverbLevel);
    properties->saveIfNeeded();
}


// ============================================================================
// LEDs van de controller
// ============================================================================

void MainComponent::refreshLedOutputs()
{
    const auto devices = juce::MidiOutput::getAvailableDevices();

    ledOutBox.clear(juce::dontSendNotification);
    ledIdentifiers.clear();

    ledOutBox.addItem("Auto (same controller)", 1);
    ledOutBox.addItem("None", 2);

    int id = 3;

    for (const auto& device : devices)
    {
        ledOutBox.addItem(device.name, id++);
        ledIdentifiers.add(device.identifier);
    }

    int selected = 1;

    if (ledChoice == "none")
        selected = 2;
    else if (ledChoice != "auto" && ledIdentifiers.contains(ledChoice))
        selected = 3 + ledIdentifiers.indexOf(ledChoice);

    ledOutBox.setSelectedId(selected, juce::dontSendNotification);

    applyLedOutput();
}

void MainComponent::applyLedOutput()
{
    juce::String target;

    if (ledChoice == "auto")
    {
        // Zoek de uitgang die bij de gekozen ingang hoort (zelfde naam, of
        // allebei "nanoKONTROL").
        juce::String inputName;

        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (device.identifier == openedMidiIn)
                inputName = device.name;

        if (inputName.isNotEmpty())
        {
            const auto outputs = juce::MidiOutput::getAvailableDevices();
            const auto firstWord = inputName.upToFirstOccurrenceOf(" ", false, false);

            for (const auto& output : outputs)
                if (output.name == inputName) { target = output.identifier; break; }

            if (target.isEmpty() && inputName.containsIgnoreCase("nanokontrol"))
                for (const auto& output : outputs)
                    if (output.name.containsIgnoreCase("nanokontrol")) { target = output.identifier; break; }

            if (target.isEmpty() && firstWord.isNotEmpty())
                for (const auto& output : outputs)
                    if (output.name.upToFirstOccurrenceOf(" ", false, false).equalsIgnoreCase(firstWord))
                    {
                        target = output.identifier;
                        break;
                    }
        }
    }
    else if (ledChoice != "none" && ledIdentifiers.contains(ledChoice))
    {
        target = ledChoice;
    }

    if (target.isEmpty())
    {
        ledsAllOff();
        ledOut.reset();
        openedLedOut = {};
        return;
    }

    if (ledOut != nullptr && openedLedOut == target)
        return;

    ledsAllOff();
    ledOut = juce::MidiOutput::openDevice(target);
    openedLedOut = ledOut != nullptr ? target : juce::String();
    ledLast.fill(-1);
}

void MainComponent::ledsAllOff()
{
    if (ledOut == nullptr)
        return;

    for (int cc = 0; cc < 128; ++cc)
        if (ledLast[static_cast<std::size_t>(cc)] > 0)
            ledOut->sendMessageNow(juce::MidiMessage::controllerEvent(1, cc, 0));

    ledLast.fill(-1);
}

void MainComponent::updateControllerLeds()
{
    if (ledOut == nullptr)
        return;

    std::array<int, 128> want;
    want.fill(-1);

    const auto set = [&want](int cc, int value)
    {
        if (cc >= 0 && cc < 128)
            want[static_cast<std::size_t>(cc)] = value;
    };

    const bool playing = synthEngine.isPlaying();
    const auto position = synthEngine.positionSamples();
    const bool paused = !playing && currentSong != nullptr
                        && position > 0 && position < synthEngine.lengthSamples();

    // Pauze: een halve seconde aan, een halve seconde uit.
    const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;

    set(midiMapping.next.load(), 127);
    set(midiMapping.stop.load(), 127);
    set(midiMapping.play.load(), playing ? 127 : (paused && blinkOn ? 127 : 0));

    const auto families = synthEngine.getAllFamilySettings();

    for (int i = 0; i < MidiMapping::families; ++i)
        set(midiMapping.enable[static_cast<std::size_t>(i)].load(),
            families[static_cast<std::size_t>(i)].enabled ? 127 : 0);

    for (int cc = 0; cc < 128; ++cc)
    {
        auto& last = ledLast[static_cast<std::size_t>(cc)];

        // Een LED die niet meer bij een functie hoort (toewijzing gewijzigd) gaat uit.
        const int target = want[static_cast<std::size_t>(cc)] >= 0
                               ? want[static_cast<std::size_t>(cc)]
                               : (last > 0 ? 0 : -1);

        if (target >= 0 && target != last)
        {
            ledOut->sendMessageNow(juce::MidiMessage::controllerEvent(1, cc, target));
            last = target;
        }
    }
}
