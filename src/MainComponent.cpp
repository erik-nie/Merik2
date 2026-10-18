#include "MainComponent.h"
#include "MerikBuilder.h"
#include "MerikPackage.h"
#include "LoadingOverlay.h"
#include "PackageLoader.h"
#include "ClickSettings.h"
#include "WebServer.h"
#include "BinaryData.h"
#include "FluidSynthEngine.h"
#include <deque>
#include <functional>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace
{
// Alle uitgangskanalen van het apparaat staan aan (maximaal 64); welk paar of
// kanaal voor de hoofdmix en voor de klik dient, kiest Merik zelf.
void enableAllOutputChannels(juce::AudioDeviceManager& manager)
{
    auto* device = manager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    juce::BigInteger wanted;
    wanted.setRange(0, std::min(64, device->getOutputChannelNames().size()), true);

    if (device->getActiveOutputChannels() == wanted)
        return;

    auto setup = manager.getAudioDeviceSetup();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels = wanted;
    manager.setAudioDeviceSetup(setup, true);
}

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
        int buttonHeight) override
    {
        return getMediumFont(juce::jmin(20.0f, static_cast<float>(buttonHeight) * 0.85f));
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

    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar& bar, juce::Graphics& g, int width, int height) override
    {
        juce::ignoreUnused(bar);

        // Lijn onder de tabs; de actieve tab loopt erdoorheen in de pagina.
        g.setColour(juce::Colour::fromRGB(52, 55, 62));
        g.fillRect(0, height - 1, width, 1);
    }

    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const bool front = button.isFrontTab();

        juce::Path shape;
        const auto area = bounds.withTrimmedTop(front ? 3.0f : 6.0f).reduced(1.0f, 0.0f);
        shape.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight() + 8.0f,
                                  8.0f, 8.0f, true, true, false, false);

        auto fill = front ? backgroundColour : panelDarkColour;

        if (!front && isMouseOver)
            fill = fill.brighter(0.1f);

        g.saveState();
        g.reduceClipRegion(button.getLocalBounds());
        g.setColour(fill);
        g.fillPath(shape);

        g.setColour(juce::Colour::fromRGB(52, 55, 62));
        g.strokePath(shape, juce::PathStrokeType(1.0f));

        if (front)
        {
            g.setColour(merikBlue);
            g.fillRect(area.getX() + 6.0f, area.getY(), area.getWidth() - 12.0f, 2.0f);

            // Onderrand weghalen zodat de tab in de pagina overgaat.
            g.setColour(backgroundColour);
            g.fillRect(area.getX() + 1.0f, bounds.getBottom() - 1.0f, area.getWidth() - 2.0f, 1.0f);
        }

        g.restoreState();

        g.setColour(front ? juce::Colours::white : secondaryTextColour);
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
    juce::TextButton& updateCheck;
    std::function<void()> checkNow;
    std::function<void()> createMerik;

    std::function<juce::String()> midiInActivity;

    juce::String webKey;
    std::function<void(const juce::String&)> webKeyChanged;

    MidiMapping& mapping;
    std::function<void()> mappingChanged;

    SynthSettings synth;
    std::function<void(const SynthSettings&)> synthChanged;

    int mainRoute = 0;
    std::function<void(int route)> mainChanged;

    int clickRoute = 0;
    int clickOffsetMs = 0;
    std::function<void(int route, int offsetMs)> clickChanged;

    std::array<int, 7> stemRoutes {};
    std::function<void(int family, int route)> stemRouteChanged;

    std::function<void()> openRender;
};

class SettingsContent final : public juce::Component,
                              private juce::ComponentListener,
                              private juce::Timer
{
public:
    explicit SettingsContent(SettingsContext contextToUse)
        : context(std::move(contextToUse)),
          sources { &context.directPlay, &context.normalize, &context.autoNext, &context.updateCheck },
          comboSources { &context.midiIn, &context.midiOut, &context.ledOut }
    {
        setLookAndFeel(&merikLookAndFeel);

        buildGeneral();
        buildAudio();
        buildMidi();
        buildTools();

        tabs.setTabBarDepth(38);
        tabs.setOutline(0);
        tabs.setIndent(0);
        tabs.addTab("General", backgroundColour, &generalPage, false);
        tabs.addTab("Soundfont", backgroundColour, &soundFontPage, false);
        tabs.addTab("Audio", backgroundColour, &audioPage, false);
        tabs.addTab("Midi", backgroundColour, &midiPage, false);
        tabs.addTab("Tools", backgroundColour, &toolsPage, false);
        addAndMakeVisible(tabs);

        refresh();
        setSize(700, 640);
        startTimer(300);
    }

    ~SettingsContent() override
    {
        stopTimer();

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

        static const char* optionLabels[4] = {
            "Doubleclick starts playing",
            "Normalize based on drumchannel level",
            "Auto play next song in setlist",
            "Check for updates at startup"
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

        checkNowButton.setButtonText("Check for updates now");
        checkNowButton.onClick = [this]
        {
            if (context.checkNow)
                context.checkNow();
        };
        generalPage.addAndMakeVisible(checkNowButton);

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

            y += 8;
            checkNowButton.setBounds(controlX, y, 190, 30);
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
                ? "Empty: the web page is view-only and can never control Merik."
                : "Remote control: http://" + ip + ":" + juce::String(WebServer::defaultPort)
                    + "/?key=" + key + "  (without the key: view only)",
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
        // ---- Soundfont-tab
        styleLabel(soundFontLabel, "SoundFont");
        soundFontPage.content.addAndMakeVisible(soundFontLabel);

        soundFontCopy.setButtonText(context.soundFont.getButtonText());
        soundFontCopy.onClick = [this]
        {
            if (context.soundFont.onClick != nullptr)
                context.soundFont.onClick();
        };
        soundFontPage.content.addAndMakeVisible(soundFontCopy);

        styleHeading(soundHeading, "Soundfont player: sound");
        styleHeading(reverbHeading, "Reverb");

        for (auto* heading : { &soundHeading, &reverbHeading })
            soundFontPage.content.addAndMakeVisible(heading);

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
            soundFontPage.content.addAndMakeVisible(row.label);

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

            soundFontPage.content.addAndMakeVisible(row.slider);
        }

        soundFontPage.layoutContent = [this](int width)
        {
            const int controlX = juce::roundToInt(static_cast<float>(width) * 0.30f);
            const int controlWidth = juce::roundToInt(static_cast<float>(width) * 0.62f);

            soundFontLabel.setBounds(0, 18, controlX - 10, rowHeight);
            soundFontCopy.setBounds(controlX, 18, controlWidth, rowHeight);

            int y = 18 + rowStep + 14;

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

            return y + 14;
        };

        // ---- Audio-tab: apparaat, hoofduitgang en klik
        styleHeading(deviceHeading, "Audio device");
        styleHeading(mainHeading, "Main output");
        styleHeading(clickHeading, "Click track (family 8)");

        for (auto* heading : { &deviceHeading, &mainHeading, &clickHeading })
            audioPage.content.addAndMakeVisible(heading);

        styleLabel(typeLabel, "Driver");
        styleLabel(deviceLabel, "Output device");
        styleLabel(rateLabel, "Sample rate");
        styleLabel(bufferLabel, "Buffer size");
        styleLabel(mainOutLabel, "Main output");
        styleLabel(clickOutLabel, "Click output");

        for (auto* component : std::initializer_list<juce::Component*> {
                 &typeLabel, &deviceLabel, &rateLabel, &bufferLabel, &mainOutLabel, &clickOutLabel,
                 &typeBox, &deviceBox, &rateBox, &bufferBox, &mainOutBox, &clickOutBox })
            audioPage.content.addAndMakeVisible(component);

        typeBox.onChange = [this]
        {
            if (updatingDevice)
                return;

            context.deviceManager.setCurrentAudioDeviceType(typeBox.getText(), true);
            enableAllOutputChannels(context.deviceManager);
            fillDeviceControls();
        };

        deviceBox.onChange = [this]
        {
            if (updatingDevice)
                return;

            auto setup = context.deviceManager.getAudioDeviceSetup();
            setup.outputDeviceName = deviceBox.getText();
            setup.inputDeviceName = {};
            setup.useDefaultOutputChannels = true;
            context.deviceManager.setAudioDeviceSetup(setup, true);
            enableAllOutputChannels(context.deviceManager);
            fillDeviceControls();
        };

        rateBox.onChange = [this]
        {
            if (updatingDevice)
                return;

            auto setup = context.deviceManager.getAudioDeviceSetup();
            setup.sampleRate = rateBox.getText().getDoubleValue();
            context.deviceManager.setAudioDeviceSetup(setup, true);
            enableAllOutputChannels(context.deviceManager);
        };

        bufferBox.onChange = [this]
        {
            if (updatingDevice)
                return;

            auto setup = context.deviceManager.getAudioDeviceSetup();
            setup.bufferSize = bufferBox.getText().getIntValue();
            context.deviceManager.setAudioDeviceSetup(setup, true);
            enableAllOutputChannels(context.deviceManager);
        };

        mainOutBox.onChange = [this]
        {
            if (updatingDevice)
                return;

            const int id = mainOutBox.getSelectedId();
            context.mainRoute = id >= 2000 ? ClickRoute::pairBase + (id - 2000)
                                           : ClickRoute::monoBase + (id - 1000);

            if (context.mainChanged)
                context.mainChanged(context.mainRoute);
        };

        clickOutBox.onChange = [this]
        {
            if (updatingDevice)
                return;

            const int id = clickOutBox.getSelectedId();

            context.clickRoute = id >= 2000 ? ClickRoute::pairBase + (id - 2000)
                               : id >= 1000 ? ClickRoute::monoBase + (id - 1000)
                               : id == 3    ? ClickRoute::split
                               : id == 2    ? ClickRoute::mixed
                                            : ClickRoute::automatic;

            if (context.clickChanged)
                context.clickChanged(context.clickRoute, context.clickOffsetMs);
        };

        styleLabel(clickOffsetRow.label, "Click delay (ms)");
        audioPage.content.addAndMakeVisible(clickOffsetRow.label);

        clickOffsetRow.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        clickOffsetRow.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 24);
        clickOffsetRow.slider.setRange(-50, 300, 1.0);
        clickOffsetRow.slider.setValue(context.clickOffsetMs, juce::dontSendNotification);
        clickOffsetRow.slider.setDoubleClickReturnValue(true, 0.0);
        clickOffsetRow.slider.onValueChange = [this]
        {
            context.clickOffsetMs = juce::roundToInt(clickOffsetRow.slider.getValue());

            if (context.clickChanged)
                context.clickChanged(context.clickRoute, context.clickOffsetMs);
        };
        audioPage.content.addAndMakeVisible(clickOffsetRow.slider);

        clickHint.setColour(juce::Label::textColourId, secondaryTextColour);
        clickHint.setFont(merikLookAndFeel.getMediumFont(13.0f));
        clickHint.setMinimumHorizontalScale(1.0f);
        clickHint.setText("Volume and on/off of the click: fader and R button 8. "
                          "The delay only matters when playing to an external MIDI device.",
                          juce::dontSendNotification);
        audioPage.content.addAndMakeVisible(clickHint);

        styleHeading(stemHeading, "Audio stems (.merik): output per family");
        audioPage.content.addAndMakeVisible(stemHeading);

        static const char* stemNames[7] = { "1 Drums", "2 Bass", "3 Guitars", "4 Keys", "5 Strings", "6 Winds", "7 Others" };

        for (std::size_t i = 0; i < stemOutBoxes.size(); ++i)
        {
            styleLabel(stemOutLabels[i], stemNames[i]);
            audioPage.content.addAndMakeVisible(stemOutLabels[i]);
            audioPage.content.addAndMakeVisible(stemOutBoxes[i]);

            stemOutBoxes[i].onChange = [this, i]
            {
                if (updatingDevice)
                    return;

                const int id = stemOutBoxes[i].getSelectedId();
                const int route = id >= 2000 ? ClickRoute::pairBase + (id - 2000)
                                : id >= 1000 ? ClickRoute::monoBase + (id - 1000)
                                             : 0;

                context.stemRoutes[i] = route;

                if (context.stemRouteChanged)
                    context.stemRouteChanged(static_cast<int>(i), route);
            };
        }

        styleHint(stemHint, "Only for audio stems; MIDI always plays through the SoundFont (or the MIDI out). "
                            "A mono output plays left and right together.");
        audioPage.content.addAndMakeVisible(stemHint);

        fillDeviceControls();

        audioPage.layoutContent = [this](int width)
        {
            const int controlX = juce::roundToInt(static_cast<float>(width) * 0.30f);
            const int controlWidth = juce::roundToInt(static_cast<float>(width) * 0.62f);

            int y = 18;

            const auto heading = [&](juce::Label& label)
            {
                label.setBounds(controlX, y, controlWidth, 24);
                y += 30;
            };

            const auto row = [&](juce::Label& label, juce::Component& control)
            {
                label.setBounds(0, y, controlX - 10, rowHeight);
                control.setBounds(controlX, y, controlWidth, rowHeight);
                y += rowStep;
            };

            heading(deviceHeading);
            row(typeLabel, typeBox);
            row(deviceLabel, deviceBox);
            row(rateLabel, rateBox);
            row(bufferLabel, bufferBox);

            y += 10;
            heading(mainHeading);
            row(mainOutLabel, mainOutBox);

            y += 10;
            heading(clickHeading);
            row(clickOutLabel, clickOutBox);
            row(clickOffsetRow.label, clickOffsetRow.slider);
            clickHint.setBounds(controlX, y, controlWidth, 40);
            y += 54;

            heading(stemHeading);

            for (std::size_t i = 0; i < stemOutBoxes.size(); ++i)
                row(stemOutLabels[i], stemOutBoxes[i]);

            stemHint.setBounds(controlX, y, controlWidth, 40);

            return y + 54;
        };
    }

    // Vult de apparaatkeuzes uit de huidige toestand van de AudioDeviceManager
    // en bouwt de uitgangslijsten (alle kanalen van het apparaat).
    juce::String stemRouteSignature() const
    {
        juce::String text;

        for (const int route : context.stemRoutes)
            text += juce::String(route) + ",";

        return text;
    }

    static void styleHint(juce::Label& label, const juce::String& text)
    {
        label.setColour(juce::Label::textColourId, secondaryTextColour);
        label.setFont(merikLookAndFeel.getMediumFont(13.0f));
        label.setMinimumHorizontalScale(1.0f);
        label.setText(text, juce::dontSendNotification);
    }

    void fillDeviceControls()
    {
        auto& manager = context.deviceManager;
        auto* device = manager.getCurrentAudioDevice();

        const auto setup = manager.getAudioDeviceSetup();
        const auto channelNames = device != nullptr ? device->getOutputChannelNames() : juce::StringArray();

        const juce::String signature = manager.getCurrentAudioDeviceType() + "|" + setup.outputDeviceName + "|"
            + juce::String(setup.sampleRate) + "|" + juce::String(setup.bufferSize) + "|"
            + channelNames.joinIntoString(",") + "|" + juce::String(context.mainRoute) + "|"
            + juce::String(context.clickRoute) + "|" + stemRouteSignature();

        if (signature == deviceSignature)
            return;

        deviceSignature = signature;

        const juce::ScopedValueSetter<bool> guard(updatingDevice, true);

        // Driver
        typeBox.clear(juce::dontSendNotification);

        for (auto* type : manager.getAvailableDeviceTypes())
            typeBox.addItem(type->getTypeName(), typeBox.getNumItems() + 1);

        typeBox.setText(manager.getCurrentAudioDeviceType(), juce::dontSendNotification);
        typeBox.setEnabled(typeBox.getNumItems() > 1);

        // Apparaat
        deviceBox.clear(juce::dontSendNotification);

        if (auto* type = manager.getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames(false);

            for (int i = 0; i < names.size(); ++i)
                deviceBox.addItem(names[i], i + 1);
        }

        deviceBox.setText(setup.outputDeviceName, juce::dontSendNotification);

        // Samplefrequentie en buffer
        rateBox.clear(juce::dontSendNotification);
        bufferBox.clear(juce::dontSendNotification);

        if (device != nullptr)
        {
            int id = 1;

            for (const auto rate : device->getAvailableSampleRates())
                rateBox.addItem(juce::String(juce::roundToInt(rate)), id++);

            rateBox.setText(juce::String(juce::roundToInt(device->getCurrentSampleRate())),
                            juce::dontSendNotification);

            id = 1;

            for (const auto size : device->getAvailableBufferSizes())
                bufferBox.addItem(juce::String(size), id++);

            bufferBox.setText(juce::String(device->getCurrentBufferSizeSamples()),
                              juce::dontSendNotification);
        }

        // Uitgangen: een paar (1+2, 3+4, ...) of één kanaal, alle kanalen van het apparaat.
        const auto fillOutputs = [&](juce::ComboBox& box, bool forClick, bool forStem = false)
        {
            box.clear(juce::dontSendNotification);

            if (forStem)
                box.addItem("Main output", 1);

            if (forClick)
            {
                box.addItem("Auto (first free pair, else in the main mix)", 1);
                box.addItem("In the main mix", 2);
                box.addItem("Split: click left, music (mono) right", 3);
            }

            for (int i = 0; i < channelNames.size(); i += 2)
                if (i + 1 < channelNames.size())
                    box.addItem(channelNames[i] + " + " + channelNames[i + 1], 2000 + i);

            for (int i = 0; i < channelNames.size(); ++i)
                box.addItem(channelNames[i] + " (mono)", 1000 + i);
        };

        fillOutputs(mainOutBox, false);
        fillOutputs(clickOutBox, true);

        for (std::size_t i = 0; i < stemOutBoxes.size(); ++i)
        {
            fillOutputs(stemOutBoxes[i], false, true);
            stemOutBoxes[i].setSelectedId(1, juce::dontSendNotification);

            const int route = context.stemRoutes[i];
            const int id = route >= ClickRoute::pairBase ? 2000 + (route - ClickRoute::pairBase)
                         : route >= ClickRoute::monoBase ? 1000 + (route - ClickRoute::monoBase)
                                                         : 1;

            if (stemOutBoxes[i].indexOfItemId(id) >= 0)
                stemOutBoxes[i].setSelectedId(id, juce::dontSendNotification);
        }

        const auto idFor = [](int route, int fallback)
        {
            if (route >= ClickRoute::pairBase)
                return 2000 + (route - ClickRoute::pairBase);

            if (route >= ClickRoute::monoBase)
                return 1000 + (route - ClickRoute::monoBase);

            return route == ClickRoute::mixed ? 2 : route == ClickRoute::split ? 3 : fallback;
        };

        int mainId = idFor(context.mainRoute, 2000);

        if (mainOutBox.indexOfItemId(mainId) < 0)
            mainId = mainOutBox.getNumItems() > 0 ? mainOutBox.getItemId(0) : 0;

        mainOutBox.setSelectedId(mainId, juce::dontSendNotification);

        int clickId = idFor(context.clickRoute, 1);

        if (clickOutBox.indexOfItemId(clickId) < 0)
            clickId = 1;

        clickOutBox.setSelectedId(clickId, juce::dontSendNotification);
    }

    // ------------------------------------------------------------------ Tools

    void buildTools()
    {
        styleHeading(makeHeading, "Create a .merik file");
        toolsPage.content.addAndMakeVisible(makeHeading);

        createMerikButton.setButtonText("Create a .merik file from a folder...");
        createMerikButton.onClick = [this]
        {
            if (context.createMerik)
                context.createMerik();
        };
        toolsPage.content.addAndMakeVisible(createMerikButton);

        styleHint(makeHint, "Choose a folder with stems (1-drums.wav, ...), optionally with a MIDI file. "
                            "Merik shows what it found and saves a .merik without compression.");
        toolsPage.content.addAndMakeVisible(makeHint);

        styleHeading(renderHeading, "Render MIDI files");
        toolsPage.content.addAndMakeVisible(renderHeading);

        renderButton.setButtonText("Render MIDI files...");
        renderButton.onClick = [this]
        {
            if (context.openRender)
                context.openRender();
        };
        toolsPage.content.addAndMakeVisible(renderButton);

        styleHint(renderHint, "Plays MIDI files through a hardware sound module or a software synth, records every "
                              "family as audio and saves a .merik per song, with the measured delay.");
        toolsPage.content.addAndMakeVisible(renderHint);

        toolsPage.layoutContent = [this](int width)
        {
            const int controlX = juce::roundToInt(static_cast<float>(width) * 0.30f);
            const int controlWidth = juce::roundToInt(static_cast<float>(width) * 0.66f);
            int y = 18;

            makeHeading.setBounds(controlX, y, controlWidth, 24);
            y += 30;
            createMerikButton.setBounds(controlX, y, 320, 30);
            y += 38;
            makeHint.setBounds(controlX, y, controlWidth, 40);
            y += 64;

            renderHeading.setBounds(controlX, y, controlWidth, 24);
            y += 30;
            renderButton.setBounds(controlX, y, 320, 30);
            y += 38;
            renderHint.setBounds(controlX, y, controlWidth, 40);
            y += 64;

            return y;
        };
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
            "1 Drums", "2 Bass", "3 Guitars", "4 Keys", "5 Strings", "6 Winds", "7 Others", "8 Click"
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
        fillDeviceControls();

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
    std::array<juce::ToggleButton, 4> optionCopies;
    std::array<juce::TextButton*, 4> sources;
    juce::TextButton checkNowButton, createMerikButton;

    // Audio
    ScrollPage soundFontPage;
    ScrollPage audioPage;
    ScrollPage toolsPage;
    juce::Label makeHeading, makeHint, renderHeading, renderHint;
    juce::TextButton renderButton;
    juce::Label soundFontLabel, soundHeading, reverbHeading;
    juce::Label deviceHeading, mainHeading, clickHeading, clickHint;
    juce::Label typeLabel, deviceLabel, rateLabel, bufferLabel, mainOutLabel, clickOutLabel;
    juce::ComboBox typeBox, deviceBox, rateBox, bufferBox, mainOutBox, clickOutBox;
    SliderRow clickOffsetRow;
    juce::Label stemHeading, stemHint;
    std::array<juce::Label, 7> stemOutLabels;
    std::array<juce::ComboBox, 7> stemOutBoxes;
    juce::String deviceSignature;
    bool updatingDevice = false;
    juce::TextButton soundFontCopy;
    std::array<SliderRow, 5> synthRows;

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

// ============================================================================
// Een .merik maken uit een map
// ============================================================================

class MerikMakerContent final : public juce::Component,
                                private juce::Timer
{
public:
    std::function<void(const juce::File&)> onAddToSetlist;

    explicit MerikMakerContent(juce::File startFolder)
        : lastFolder(std::move(startFolder))
    {
        setLookAndFeel(&merikLookAndFeel);

        heading.setText("Create a .merik file", juce::dontSendNotification);
        heading.setFont(merikLookAndFeel.getBoldFont(20.0f));
        heading.setColour(juce::Label::textColourId, textColour);
        addAndMakeVisible(heading);

        hint.setText("Choose a folder with your stems (1-drums.wav, 2-bass.flac, ...), and optionally song.mid, "
                     "click.wav, chords.json and lyrics.json. Merik shows what it found, can convert WAV to FLAC, "
                     "and saves everything as a .merik without compression, so it loads instantly.",
                     juce::dontSendNotification);
        hint.setFont(merikLookAndFeel.getMediumFont(14.0f));
        hint.setColour(juce::Label::textColourId, secondaryTextColour);
        hint.setMinimumHorizontalScale(1.0f);
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        folderButton.setButtonText("Choose folder...");
        folderButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible(folderButton);

        folderLabel.setFont(merikLookAndFeel.getMediumFont(14.0f));
        folderLabel.setColour(juce::Label::textColourId, textColour);
        folderLabel.setText("No folder chosen", juce::dontSendNotification);
        addAndMakeVisible(folderLabel);

        report.setMultiLine(true, false);
        report.setReadOnly(true);
        report.setScrollbarsShown(true);
        report.setCaretVisible(false);
        report.setFont(merikLookAndFeel.getMediumFont(14.0f));
        report.setColour(juce::TextEditor::backgroundColourId, panelDarkColour);
        report.setColour(juce::TextEditor::textColourId, textColour);
        report.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        report.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        report.setIndents(10, 8);
        addAndMakeVisible(report);

        flacToggle.setButtonText("Convert WAV to FLAC (about half the size, lossless)");
        flacToggle.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(flacToggle);

        createButton.setButtonText("Create .merik...");
        createButton.setColour(juce::TextButton::buttonColourId, merikBlue);
        createButton.setEnabled(false);
        createButton.onClick = [this] { chooseOutput(); };
        addAndMakeVisible(createButton);

        statusLabel.setFont(merikLookAndFeel.getMediumFont(14.0f));
        statusLabel.setColour(juce::Label::textColourId, secondaryTextColour);
        addAndMakeVisible(statusLabel);

        addToSetlistButton.setButtonText("Add to the current setlist");
        addToSetlistButton.onClick = [this]
        {
            if (onAddToSetlist && created != juce::File())
                onAddToSetlist(created);

            addToSetlistButton.setEnabled(false);
        };
        addChildComponent(addToSetlistButton);

        showButton.setButtonText("Show in Finder");
        showButton.onClick = [this] { created.revealToUser(); };
        addChildComponent(showButton);

        progressBar = std::make_unique<juce::ProgressBar>(progressValue);
        progressBar->setPercentageDisplay(true);
        addChildComponent(*progressBar);

        setSize(620, 600);
    }

    ~MerikMakerContent() override
    {
        stopTimer();
        cancel = true;

        if (worker != nullptr)
            worker->stopThread(60000);

        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(backgroundColour);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(22, 18);

        heading.setBounds(area.removeFromTop(30));
        area.removeFromTop(4);
        hint.setBounds(area.removeFromTop(62));
        area.removeFromTop(8);

        auto row = area.removeFromTop(32);
        folderButton.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(12);
        folderLabel.setBounds(row);

        area.removeFromTop(10);

        auto bottom = area.removeFromBottom(120);
        report.setBounds(area);

        bottom.removeFromTop(10);
        flacToggle.setBounds(bottom.removeFromTop(26));
        bottom.removeFromTop(8);

        auto progressRow = bottom.removeFromTop(24);
        progressBar->setBounds(progressRow);
        statusLabel.setBounds(progressRow);

        bottom.removeFromTop(12);
        auto buttons = bottom.removeFromTop(32);
        createButton.setBounds(buttons.removeFromRight(160));
        buttons.removeFromRight(10);
        addToSetlistButton.setBounds(buttons.removeFromRight(200));
        buttons.removeFromRight(10);
        showButton.setBounds(buttons.removeFromRight(130));
    }

private:
    void chooseFolder()
    {
        chooser = std::make_shared<juce::FileChooser>("Choose the folder with your stems", lastFolder);

        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<MerikMakerContent>(this)](const juce::FileChooser& fc)
            {
                if (safe == nullptr)
                    return;

                const auto folder = fc.getResult();

                if (folder.isDirectory())
                    safe->showFolder(folder);
            });
    }

    void showFolder(const juce::File& folder)
    {
        lastFolder = folder;
        plan = MerikBuilder::scan(folder);

        folderLabel.setText(folder.getFullPathName(), juce::dontSendNotification);
        created = juce::File();
        addToSetlistButton.setVisible(false);
        showButton.setVisible(false);
        statusLabel.setText({}, juce::dontSendNotification);

        juce::String text;

        if (!plan.items.empty())
        {
            text << "This will go into the .merik:\n";

            for (const auto& item : plan.items)
                text << "  +  " << item.description << "\n";
        }

        if (plan.ignored.size() > 0)
        {
            text << "\nNot used (name not recognised):\n";

            for (const auto& name : plan.ignored)
                text << "  -  " << name << "\n";
        }

        if (plan.warnings.size() > 0)
        {
            text << "\nTo check:\n";

            for (const auto& warning : plan.warnings)
                text << "  !  " << warning << "\n";
        }

        if (plan.items.empty())
            text << "Nothing recognised in this folder.\n\nName the stems like 1-drums.wav, 2-bass.wav, 3-guitars.wav, "
                    "4-keys.wav, 5-strings.wav, 6-winds.wav, 7-others.wav and 8-click.wav (flac and mp3 also work).\n";

        report.setText(text, false);
        createButton.setEnabled(plan.usable());
    }

    void chooseOutput()
    {
        if (worker != nullptr)
            return;

        const auto suggested = plan.folder.getSiblingFile(plan.folder.getFileName() + ".merik");

        chooser = std::make_shared<juce::FileChooser>("Save the .merik file as", suggested, "*.merik");

        chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe = juce::Component::SafePointer<MerikMakerContent>(this)](const juce::FileChooser& fc)
            {
                if (safe == nullptr)
                    return;

                auto file = fc.getResult();

                if (file == juce::File())
                    return;

                if (!file.hasFileExtension("merik"))
                    file = file.withFileExtension("merik");

                safe->startBuild(file);
            });
    }

    void startBuild(const juce::File& output)
    {
        cancel = false;
        finished = false;
        progress = 0.0f;
        progressValue = 0.0;
        message = "Starting...";
        result = {};
        created = juce::File();

        createButton.setEnabled(false);
        folderButton.setEnabled(false);
        flacToggle.setEnabled(false);
        addToSetlistButton.setVisible(false);
        showButton.setVisible(false);
        statusLabel.setVisible(false);
        progressBar->setVisible(true);

        const auto copy = plan;
        const bool convert = flacToggle.getToggleState();

        worker = std::make_unique<Worker>([this, copy, convert, output]
        {
            auto outcome = MerikBuilder::build(copy, convert, output, [this](float p, const juce::String& text)
            {
                progress = p;

                {
                    const juce::ScopedLock lock(messageLock);
                    message = text;
                }

                return !cancel.load();
            });

            {
                const juce::ScopedLock lock(messageLock);
                result = outcome;
            }

            outputFile = output;
            finished = true;
        });

        startTimerHz(20);
    }

    void timerCallback() override
    {
        progressValue = static_cast<double>(progress.load());

        {
            const juce::ScopedLock lock(messageLock);
            progressBar->setTextToDisplay(juce::String(juce::roundToInt(progressValue * 100.0)) + "%   " + message);
        }

        if (!finished.load())
            return;

        stopTimer();

        if (worker != nullptr)
            worker->stopThread(5000);

        worker.reset();

        progressBar->setVisible(false);
        statusLabel.setVisible(true);
        folderButton.setEnabled(true);
        flacToggle.setEnabled(true);
        createButton.setEnabled(plan.usable());

        MerikBuilder::Result outcome;

        {
            const juce::ScopedLock lock(messageLock);
            outcome = result;
        }

        if (outcome.ok)
        {
            created = outputFile;
            statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(46, 204, 113));
            statusLabel.setText("Created " + outputFile.getFileName() + " ("
                                    + juce::String(static_cast<double>(outcome.bytes) / (1024.0 * 1024.0), 1) + " MB)",
                                juce::dontSendNotification);
            addToSetlistButton.setEnabled(true);
            addToSetlistButton.setVisible(true);
            showButton.setVisible(true);
        }
        else
        {
            statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 120, 120));
            statusLabel.setText(outcome.cancelled ? "Cancelled" : outcome.error, juce::dontSendNotification);
        }
    }

    class Worker final : public juce::Thread
    {
    public:
        explicit Worker(std::function<void()> jobToRun)
            : juce::Thread("Merik maker"), job(std::move(jobToRun))
        {
            startThread(juce::Thread::Priority::normal);
        }

        ~Worker() override { stopThread(60000); }

    private:
        void run() override { job(); }
        std::function<void()> job;
    };

    juce::Label heading, hint, folderLabel, statusLabel;
    juce::TextButton folderButton, createButton, addToSetlistButton, showButton;
    juce::ToggleButton flacToggle;
    juce::TextEditor report;
    std::unique_ptr<juce::ProgressBar> progressBar;
    double progressValue = 0.0;

    std::shared_ptr<juce::FileChooser> chooser;
    juce::File lastFolder, created, outputFile;
    MerikBuilder::Plan plan;

    std::unique_ptr<Worker> worker;
    std::atomic<bool> cancel { false }, finished { false };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection messageLock;
    juce::String message;
    MerikBuilder::Result result;
};

class MerikMakerWindow final : public juce::DialogWindow
{
public:
    explicit MerikMakerWindow(juce::Component* content, juce::Component* owner)
        : juce::DialogWindow("Create a .merik file", backgroundColour, true)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        setContentOwned(content, true);
        centreAroundComponent(owner, getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { delete this; }
};

// ============================================================================
// Opnemen via een externe module: niveau en vertraging instellen
// ============================================================================

// Niveaubalk in dBFS (-48..0) met het gewenste gebied (-9..-3) en een piekhouder.
class CalibrationMeter final : public juce::Component
{
public:
    void setLevels(const std::array<float, 2>& current, const std::array<float, 2>& hold, int channelCount)
    {
        levels = current;
        holds = hold;
        channels = channelCount;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const float left = 30.0f;
        const float width = static_cast<float>(getWidth()) - left - 4.0f;
        const auto toX = [&](float db) { return left + width * std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f); };
        const auto toDb = [](float linear) { return linear > 0.00001f ? 20.0f * std::log10(linear) : -100.0f; };

        const float barHeight = 22.0f;
        const float top = 6.0f;

        // Gewenst gebied (-9 .. -3 dBFS) over de hoogte van beide balken.
        const float zoneBottom = top + static_cast<float>(channels) * (barHeight + 4.0f);
        g.setColour(juce::Colour::fromRGB(46, 204, 113).withAlpha(0.16f));
        g.fillRect(toX(-9.0f), top - 4.0f, toX(-3.0f) - toX(-9.0f), zoneBottom - top + 4.0f);

        for (int c = 0; c < channels; ++c)
        {
            const float y = top + static_cast<float>(c) * (barHeight + 4.0f);
            const juce::Rectangle<float> bar(left, y, width, barHeight);

            g.setColour(juce::Colour::fromRGB(30, 32, 37));
            g.fillRoundedRectangle(bar, 4.0f);

            const float db = toDb(levels[static_cast<std::size_t>(c)]);
            const float x = toX(db);

            if (x > left + 1.0f)
            {
                const auto colour = db > -0.5f ? juce::Colour::fromRGB(255, 85, 85)
                                  : db > -3.0f ? juce::Colour::fromRGB(245, 166, 35)
                                  : db > -9.0f ? juce::Colour::fromRGB(46, 204, 113)
                                               : juce::Colour::fromRGB(24, 118, 255);
                g.setColour(colour);
                g.fillRoundedRectangle(juce::Rectangle<float>(left, y, x - left, barHeight), 4.0f);
            }

            // Piekhouder
            const float holdX = toX(toDb(holds[static_cast<std::size_t>(c)]));
            g.setColour(juce::Colours::white);
            g.fillRect(holdX - 1.0f, y, 2.0f, barHeight);

            g.setColour(secondaryTextColour);
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(channels == 1 ? "M" : (c == 0 ? "L" : "R"), 0, static_cast<int>(y), 24,
                       static_cast<int>(barHeight), juce::Justification::centredRight);
        }

        // Schaal
        g.setColour(secondaryTextColour);
        g.setFont(juce::FontOptions(11.0f));

        for (int db = -48; db <= 0; db += 6)
            g.drawText(juce::String(db), static_cast<int>(toX(static_cast<float>(db))) - 14,
                       static_cast<int>(zoneBottom) + 2, 28, 14, juce::Justification::centred);
    }

private:
    std::array<float, 2> levels {}, holds {};
    int channels = 2;
};

class RenderCalibrationContent final : public juce::Component,
                                       private juce::Timer
{
public:
    RenderCalibrationContent(RenderEngine& renderEngine, bool sendGmReset,
                             std::function<void(double)> delayFound, std::function<void()> closed)
        : engine(renderEngine), gmReset(sendGmReset), onDelay(std::move(delayFound)), onClosed(std::move(closed))
    {
        setLookAndFeel(&merikLookAndFeel);

        heading.setText("Levels and delay", juce::dontSendNotification);
        heading.setFont(merikLookAndFeel.getBoldFont(20.0f));
        heading.setColour(juce::Label::textColourId, textColour);
        addAndMakeVisible(heading);

        hint.setText("1. Press Start and listen: a piano and drums play at the highest velocity.\n"
                     "2. Turn the level of your module (or the input gain on your mixer) up or down until the peaks "
                     "sit in the green zone, around -6 dB. Not higher: the loudest notes of a song must not clip.\n"
                     "3. Press Measure delay: Merik plays short hits and measures how long the sound takes to come "
                     "back. Measure again after changing the audio buffer size.",
                     juce::dontSendNotification);
        hint.setFont(merikLookAndFeel.getMediumFont(14.0f));
        hint.setColour(juce::Label::textColourId, secondaryTextColour);
        hint.setMinimumHorizontalScale(1.0f);
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        addAndMakeVisible(meter);

        peakLabel.setFont(merikLookAndFeel.getBoldFont(18.0f));
        peakLabel.setColour(juce::Label::textColourId, textColour);
        addAndMakeVisible(peakLabel);

        adviceLabel.setFont(merikLookAndFeel.getMediumFont(15.0f));
        adviceLabel.setColour(juce::Label::textColourId, secondaryTextColour);
        addAndMakeVisible(adviceLabel);

        startButton.setButtonText("Start test sound");
        startButton.setColour(juce::TextButton::buttonColourId, merikBlue);
        startButton.onClick = [this] { toggleTest(); };
        addAndMakeVisible(startButton);

        measureButton.setButtonText("Measure delay");
        measureButton.onClick = [this] { measureDelay(); };
        addAndMakeVisible(measureButton);

        delayLabel.setFont(merikLookAndFeel.getMediumFont(15.0f));
        delayLabel.setColour(juce::Label::textColourId, textColour);
        addAndMakeVisible(delayLabel);

        closeButton.setButtonText("Close");
        closeButton.onClick = [safe = juce::Component::SafePointer<RenderCalibrationContent>(this)]
        {
            // Het venster sluit na deze klik (het verwijdert de knop waar we nu in zitten).
            juce::MessageManager::callAsync([safe]
            {
                if (safe != nullptr)
                    if (auto* window = safe->findParentComponentOfClass<juce::DialogWindow>())
                        delete window;
            });
        };
        addAndMakeVisible(closeButton);

        rate = engine.sampleRate();
        channels = engine.channels();
        engine.session().prepare(rate, channels, static_cast<std::int64_t>(14.0 * rate));

        setSize(620, 520);
        startTimerHz(30);
    }

    ~RenderCalibrationContent() override
    {
        stopTimer();
        engine.session().reset();
        engine.midi().allNotesOff(-1);
        setLookAndFeel(nullptr);

        if (onClosed)
            onClosed();
    }

    void paint(juce::Graphics& g) override { g.fillAll(backgroundColour); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(22, 18);
        heading.setBounds(area.removeFromTop(30));
        area.removeFromTop(6);
        hint.setBounds(area.removeFromTop(120));
        area.removeFromTop(10);
        meter.setBounds(area.removeFromTop(channels == 1 ? 60 : 88));
        area.removeFromTop(6);
        peakLabel.setBounds(area.removeFromTop(28));
        adviceLabel.setBounds(area.removeFromTop(26));
        area.removeFromTop(14);

        auto row = area.removeFromTop(34);
        startButton.setBounds(row.removeFromLeft(170));
        row.removeFromLeft(12);
        measureButton.setBounds(row.removeFromLeft(150));

        area.removeFromTop(12);
        delayLabel.setBounds(area.removeFromTop(48));

        closeButton.setBounds(area.removeFromBottom(34).removeFromRight(110));
    }

private:
    using Message = CaptureSession::Message;

    static void add(std::vector<Message>& schedule, double seconds, double rate,
                    std::initializer_list<std::uint8_t> bytes)
    {
        Message message;
        message.sample = static_cast<std::int64_t>(std::llround(seconds * rate));
        message.size = static_cast<int>(bytes.size());
        std::copy(bytes.begin(), bytes.end(), message.data.begin());
        schedule.push_back(message);
    }

    // Vier tellen: piano (kanaal 1) en drums (kanaal 10), alles op velocity 127.
    void armTestLoop(bool first)
    {
        std::vector<Message> schedule;

        if (first)
        {
            if (gmReset)
                add(schedule, 0.05, rate, { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 });

            add(schedule, 0.25, rate, { 0xC0, 0 });         // piano op kanaal 1
            add(schedule, 0.26, rate, { 0xB0, 7, 127 });
            add(schedule, 0.27, rate, { 0xB9, 7, 127 });    // drums op kanaal 10
        }

        struct Beat { std::uint8_t drum1, drum2, a, b, c; };
        const Beat beats[4] = { { 36, 49, 48, 55, 64 }, { 38, 38, 53, 57, 65 }, { 36, 42, 52, 59, 67 }, { 38, 46, 55, 62, 71 } };
        const double offset = first ? 0.6 : 0.1;

        for (int i = 0; i < 4; ++i)
        {
            const double t = offset + static_cast<double>(i);
            const auto& beat = beats[i];

            add(schedule, t, rate, { 0x99, beat.drum1, 127 });
            add(schedule, t, rate, { 0x99, beat.drum2, 127 });
            add(schedule, t, rate, { 0x90, beat.a, 127 });
            add(schedule, t, rate, { 0x90, beat.b, 127 });
            add(schedule, t, rate, { 0x90, beat.c, 127 });
            add(schedule, t + 0.8, rate, { 0x80, beat.a, 0 });
            add(schedule, t + 0.8, rate, { 0x80, beat.b, 0 });
            add(schedule, t + 0.8, rate, { 0x80, beat.c, 0 });
        }

        std::stable_sort(schedule.begin(), schedule.end(), [](const Message& a, const Message& b) { return a.sample < b.sample; });
        engine.session().arm(std::move(schedule), static_cast<std::int64_t>((offset + 4.2) * rate));
    }

    void toggleTest()
    {
        if (testing)
        {
            testing = false;
            engine.session().reset();
            engine.midi().allNotesOff(-1);
            startButton.setButtonText("Start test sound");
            return;
        }

        if (measuring)
            return;

        testing = true;
        firstLoop = true;
        startButton.setButtonText("Stop test sound");
        armTestLoop(true);
        firstLoop = false;
    }

    void measureDelay()
    {
        if (measuring)
            return;

        testing = false;
        startButton.setButtonText("Start test sound");
        engine.session().reset();
        engine.midi().allNotesOff(-1);

        // Tien scherpe slagen (Hi Wood Block, drumkanaal), 0,8 s uit elkaar.
        std::vector<Message> schedule;
        hitSamples.clear();

        if (gmReset)
            add(schedule, 0.05, rate, { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 });

        add(schedule, 0.7, rate, { 0xB9, 7, 127 });

        for (int i = 0; i < 10; ++i)
        {
            const double t = 1.4 + 0.8 * static_cast<double>(i);
            add(schedule, t, rate, { 0x99, 76, 127 });
            hitSamples.push_back(static_cast<std::int64_t>(std::llround(t * rate)));
        }

        std::stable_sort(schedule.begin(), schedule.end(), [](const Message& a, const Message& b) { return a.sample < b.sample; });

        measuring = true;
        measureButton.setEnabled(false);
        startButton.setEnabled(false);
        delayLabel.setColour(juce::Label::textColourId, secondaryTextColour);
        delayLabel.setText("Measuring...", juce::dontSendNotification);
        engine.session().arm(std::move(schedule), static_cast<std::int64_t>(10.2 * rate));
    }

    void finishMeasurement()
    {
        measuring = false;
        measureButton.setEnabled(true);
        startButton.setEnabled(true);

        const auto result = analyseDelay(engine.session().data(), hitSamples, rate);

        if (!result.ok)
        {
            delayLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 120, 120));
            delayLabel.setText(juce::String(result.problem), juce::dontSendNotification);
            return;
        }

        delayLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(46, 204, 113));
        delayLabel.setText("Delay: " + juce::String(result.delayMs, 1) + " ms   (" + juce::String(result.hits)
                               + " hits, spread " + juce::String(result.spreadMs, 1) + " ms). Saved; it goes into every .merik you render.",
                           juce::dontSendNotification);

        if (onDelay)
            onDelay(result.delayMs);
    }

    void timerCallback() override
    {
        const auto peaks = engine.session().takePeaks();

        for (std::size_t c = 0; c < 2; ++c)
        {
            shown[c] = peaks[c] > shown[c] ? peaks[c] : shown[c] * 0.88f;

            if (peaks[c] >= hold[c] || holdAge[c] > 90)
            {
                hold[c] = peaks[c];
                holdAge[c] = 0;
            }
            else
            {
                ++holdAge[c];
            }
        }

        meter.setLevels(shown, hold, channels);

        const float loudest = std::max(hold[0], channels > 1 ? hold[1] : 0.0f);
        const float db = loudest > 0.00001f ? 20.0f * std::log10(loudest) : -100.0f;

        peakLabel.setText(db > -99.0f ? "Peak " + juce::String(db, 1) + " dBFS" : "No signal", juce::dontSendNotification);

        juce::String advice;
        auto colour = secondaryTextColour;

        if (db < -60.0f)
        {
            advice = testing ? "No signal. Check the MIDI cable, the module's output, the input channels, and (macOS) the microphone permission."
                             : "Press Start to play the test sound.";
        }
        else if (db < -12.0f)
        {
            advice = "Too low: turn the level up.";
            colour = juce::Colour::fromRGB(24, 118, 255);
        }
        else if (db < -9.0f)
        {
            advice = "A little low: a bit more.";
            colour = juce::Colour::fromRGB(24, 118, 255);
        }
        else if (db <= -3.0f)
        {
            advice = "Good. This is the right level.";
            colour = juce::Colour::fromRGB(46, 204, 113);
        }
        else if (db < -0.5f)
        {
            advice = "A little high: turn the level down a bit.";
            colour = juce::Colour::fromRGB(245, 166, 35);
        }
        else
        {
            advice = "Too high: it clips. Turn the level down.";
            colour = juce::Colour::fromRGB(255, 85, 85);
        }

        adviceLabel.setColour(juce::Label::textColourId, colour);
        adviceLabel.setText(advice, juce::dontSendNotification);

        // De testgeluid blijft herhalen; een meting wordt afgerond als ze klaar is.
        auto& session = engine.session();

        if (testing && session.isFinished())
            armTestLoop(false);
        else if (measuring && session.isFinished())
            finishMeasurement();
    }

    RenderEngine& engine;
    bool gmReset;
    std::function<void(double)> onDelay;
    std::function<void()> onClosed;

    double rate = 48000.0;
    int channels = 2;
    bool testing = false, measuring = false, firstLoop = true;
    std::vector<std::int64_t> hitSamples;
    std::array<float, 2> shown {}, hold {};
    std::array<int, 2> holdAge {};

    juce::Label heading, hint, peakLabel, adviceLabel, delayLabel;
    juce::TextButton startButton, measureButton, closeButton;
    CalibrationMeter meter;
};

class RenderCalibrationWindow final : public juce::DialogWindow
{
public:
    RenderCalibrationWindow(juce::Component* content, juce::Component* owner)
        : juce::DialogWindow("Levels and delay", backgroundColour, true)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        setContentOwned(content, true);
        centreAroundComponent(owner, getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { delete this; }
};

// ============================================================================
// Venster "Render MIDI files": instellingen, en daarna de voortgang
// ============================================================================

struct RenderContext
{
    juce::AudioDeviceManager* deviceManager = nullptr;
    RenderPrefs prefs;
    std::function<void(const RenderPrefs&)> prefsChanged;
    std::function<RenderPrefs()> getPrefs;
    std::function<juce::String(const RenderOptions&)> start;      // geeft een foutmelding of leeg
    std::function<void()> stop;
    std::function<RenderStatus()> status;
    std::function<bool()> busy;
    std::function<void()> openCalibration;
};

class RenderContent final : public juce::Component,
                            private juce::Timer
{
public:
    explicit RenderContent(RenderContext contextToUse)
        : context(std::move(contextToUse)), prefs(context.prefs)
    {
        setLookAndFeel(&merikLookAndFeel);

        const auto label = [this](juce::Label& l, const juce::String& text)
        {
            l.setText(text, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(l);
        };

        const auto info = [this](juce::Label& l)
        {
            l.setFont(merikLookAndFeel.getMediumFont(14.0f));
            l.setColour(juce::Label::textColourId, secondaryTextColour);
            l.setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(l);
        };

        heading.setText("Render MIDI files with an external module", juce::dontSendNotification);
        heading.setFont(merikLookAndFeel.getBoldFont(19.0f));
        heading.setColour(juce::Label::textColourId, textColour);
        addAndMakeVisible(heading);

        hint.setText("Plays each MIDI file through your sound module or a software synth, one family at a time, "
                     "records the audio and saves a .merik per song. The measured delay is stored in the file.",
                     juce::dontSendNotification);
        hint.setFont(merikLookAndFeel.getMediumFont(13.0f));
        hint.setColour(juce::Label::textColourId, secondaryTextColour);
        hint.setMinimumHorizontalScale(1.0f);
        hint.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(hint);

        label(midiOutLabel, "MIDI output");
        label(deviceLabel, "Audio input device");
        label(inputLabel, "Input channels");
        label(calibrateLabel, "Levels and delay");
        label(filesLabel, "MIDI files");
        label(folderLabel, "Output folder");

        addAndMakeVisible(midiOutBox);
        addAndMakeVisible(deviceBox);
        addAndMakeVisible(inputBox);

        midiOutBox.onChange = [this]
        {
            const int index = midiOutBox.getSelectedId() - 2;
            prefs.midiOutIdentifier = index >= 0 && index < midiIds.size() ? midiIds[index] : juce::String();
            savePrefs();
        };

        deviceBox.onChange = [this]
        {
            const int index = deviceBox.getSelectedId() - 2;
            prefs.inputDevice = index >= 0 && index < deviceNames.size() ? deviceNames[index] : juce::String();
            prefs.inputRoute = 0;     // de kanalen horen bij het apparaat
            inputSignature.clear();
            savePrefs();
        };

        inputBox.onChange = [this]
        {
            const int id = inputBox.getSelectedId();
            prefs.inputRoute = id >= RenderEngine::monoBase ? id : 0;
            savePrefs();
        };

        calibrateButton.setButtonText("Test levels and measure delay...");
        calibrateButton.onClick = [this]
        {
            if (context.openCalibration)
                context.openCalibration();
        };
        addAndMakeVisible(calibrateButton);

        delayCaption.setText("Delay (ms)", juce::dontSendNotification);
        delayCaption.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(delayCaption);

        delayEditor.setInputRestrictions(8, "0123456789.-");
        delayEditor.setText(juce::String(prefs.delayMs, 1), juce::dontSendNotification);
        delayEditor.setColour(juce::TextEditor::backgroundColourId, panelLightColour);
        delayEditor.setColour(juce::TextEditor::textColourId, textColour);
        delayEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        delayEditor.setColour(juce::TextEditor::focusedOutlineColourId, merikBlue);
        delayEditor.setFont(merikLookAndFeel.getMediumFont(17.0f));
        delayEditor.setIndents(10, 0);
        delayEditor.setJustification(juce::Justification::centredLeft);
        delayEditor.onTextChange = [this]
        {
            prefs.delayMs = delayEditor.getText().getDoubleValue();
            savePrefs();
        };
        addAndMakeVisible(delayEditor);

        chooseFilesButton.setButtonText("Choose MIDI files...");
        chooseFilesButton.onClick = [this] { chooseFiles(); };
        addAndMakeVisible(chooseFilesButton);

        chooseFolderButton.setButtonText("Choose output folder...");
        chooseFolderButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible(chooseFolderButton);

        info(filesInfo);
        info(folderPath);
        filesInfo.setText("No files chosen", juce::dontSendNotification);
        folderPath.setText(prefs.outputFolder.isEmpty() ? "No folder chosen" : prefs.outputFolder, juce::dontSendNotification);

        keepWavToggle.setButtonText("Keep the WAV files in a folder next to the .merik");
        keepWavToggle.setToggleState(prefs.keepWav, juce::dontSendNotification);
        keepWavToggle.onClick = [this] { prefs.keepWav = keepWavToggle.getToggleState(); savePrefs(); };
        addAndMakeVisible(keepWavToggle);

        flacToggle.setButtonText("Convert to FLAC inside the .merik (about half the size)");
        flacToggle.setToggleState(prefs.toFlac, juce::dontSendNotification);
        flacToggle.onClick = [this] { prefs.toFlac = flacToggle.getToggleState(); savePrefs(); };
        addAndMakeVisible(flacToggle);

        gmResetToggle.setButtonText("Send a General MIDI reset before every pass");
        gmResetToggle.setToggleState(prefs.gmReset, juce::dontSendNotification);
        gmResetToggle.onClick = [this] { prefs.gmReset = gmResetToggle.getToggleState(); savePrefs(); };
        addAndMakeVisible(gmResetToggle);

        errorLabel.setFont(merikLookAndFeel.getMediumFont(14.0f));
        errorLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 120, 120));
        errorLabel.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(errorLabel);

        startButton.setButtonText("Start rendering");
        startButton.setColour(juce::TextButton::buttonColourId, merikBlue);
        startButton.onClick = [this] { startRender(); };
        addAndMakeVisible(startButton);

        // ---- voortgang
        titleLabel.setFont(merikLookAndFeel.getBoldFont(22.0f));
        titleLabel.setColour(juce::Label::textColourId, textColour);
        addChildComponent(titleLabel);

        for (auto* l : { &songLabel, &passLabel, &etaLabel })
        {
            l->setFont(merikLookAndFeel.getMediumFont(16.0f));
            l->setColour(juce::Label::textColourId, secondaryTextColour);
            addChildComponent(*l);
        }

        passLabel.setColour(juce::Label::textColourId, textColour);

        progressBar = std::make_unique<juce::ProgressBar>(progressValue);
        progressBar->setPercentageDisplay(true);
        addChildComponent(*progressBar);

        log.setMultiLine(true, true);
        log.setReadOnly(true);
        log.setScrollbarsShown(true);
        log.setCaretVisible(false);
        log.setFont(merikLookAndFeel.getMediumFont(13.0f));
        log.setColour(juce::TextEditor::backgroundColourId, panelDarkColour);
        log.setColour(juce::TextEditor::textColourId, textColour);
        log.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        log.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        log.setIndents(8, 6);
        addChildComponent(log);

        stopButton.setButtonText("Stop");
        stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(150, 40, 40));
        stopButton.onClick = [this]
        {
            if (context.stop)
                context.stop();

            stopButton.setEnabled(false);
        };
        addChildComponent(stopButton);

        backButton.setButtonText("Back");
        backButton.onClick = [this] { showingProgress = false; updateView(); };
        addChildComponent(backButton);

        fillDevices();
        showingProgress = context.busy && context.busy();
        updateView();

        setSize(660, 540);
        startTimerHz(4);
    }

    ~RenderContent() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    [[nodiscard]] bool isBusy() const { return context.busy && context.busy(); }

    // Voor het sluitknopje van het venster: stoppen na bevestiging.
    void askToStop(std::function<void()> thenClose)
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Stop rendering?")
                .withMessage("The song that is being recorded will be discarded. Finished songs stay.")
                .withButton("Stop and close")
                .withButton("Keep rendering")
                .withAssociatedComponent(this),
            [this, thenClose = std::move(thenClose), safe = juce::Component::SafePointer<RenderContent>(this)](int result)
            {
                if (safe == nullptr || result != 1)
                    return;

                if (context.stop)
                    context.stop();

                thenClose();
            });
    }

    void paint(juce::Graphics& g) override { g.fillAll(backgroundColour); }

    void resized() override
    {
        const int width = getWidth();
        const int controlX = 170;
        const int controlWidth = width - controlX - 24;
        const int labelWidth = controlX - 10;
        constexpr int row = 30;

        heading.setBounds(24, 16, width - 48, 28);
        hint.setBounds(24, 46, width - 48, 38);

        int y = 98;

        const auto place = [&](juce::Label& l, juce::Component& c, int w = -1)
        {
            l.setBounds(0, y, labelWidth, row);
            c.setBounds(controlX, y, w > 0 ? w : controlWidth, row);
            y += 38;
        };

        place(midiOutLabel, midiOutBox);
        place(deviceLabel, deviceBox);
        place(inputLabel, inputBox);

        calibrateLabel.setBounds(0, y, labelWidth, row);
        calibrateButton.setBounds(controlX, y, 270, row);
        delayCaption.setBounds(controlX + 270, y, 90, row);
        delayEditor.setBounds(controlX + 366, y, 80, row);
        y += 38;

        filesLabel.setBounds(0, y, labelWidth, row);
        chooseFilesButton.setBounds(controlX, y, 190, row);
        filesInfo.setBounds(controlX + 200, y, controlWidth - 200, row);
        y += 38;

        folderLabel.setBounds(0, y, labelWidth, row);
        chooseFolderButton.setBounds(controlX, y, 190, row);
        folderPath.setBounds(controlX + 200, y, controlWidth - 200, row);
        y += 42;

        for (auto* toggle : { &keepWavToggle, &flacToggle, &gmResetToggle })
        {
            toggle->setBounds(controlX, y, controlWidth, 26);
            y += 28;
        }

        y += 8;
        errorLabel.setBounds(24, y, width - 48, 22);
        startButton.setBounds(width - 24 - 170, getHeight() - 24 - 36, 170, 36);

        // Voortgang
        titleLabel.setBounds(28, 24, width - 56, 34);
        songLabel.setBounds(28, 66, width - 56, 24);
        passLabel.setBounds(28, 92, width - 56, 24);
        progressBar->setBounds(28, 130, width - 56, 28);
        etaLabel.setBounds(28, 166, width - 56, 24);
        log.setBounds(28, 204, width - 56, getHeight() - 204 - 24 - 36 - 16);
        stopButton.setBounds(width - 28 - 130, getHeight() - 24 - 36, 130, 36);
        backButton.setBounds(width - 28 - 130, getHeight() - 24 - 36, 130, 36);
    }

private:
    static juce::String formatDuration(double seconds)
    {
        const int minutes = juce::roundToInt(seconds / 60.0);

        return minutes >= 60 ? juce::String(minutes / 60) + " h " + juce::String(minutes % 60).paddedLeft('0', 2) + " min"
                             : juce::String(juce::jmax(1, minutes)) + " min";
    }

    void savePrefs()
    {
        if (context.prefsChanged)
            context.prefsChanged(prefs);
    }

    void chooseFiles()
    {
        chooser = std::make_shared<juce::FileChooser>("Choose the MIDI files to render", juce::File(prefs.outputFolder),
                                                      "*.mid;*.midi;*.kar;*.MID;*.MIDI;*.KAR");

        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::canSelectMultipleItems,
            [safe = juce::Component::SafePointer<RenderContent>(this)](const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResults().isEmpty())
                    return;

                safe->files.clear();

                for (const auto& file : fc.getResults())
                    safe->files.push_back(file);

                const auto estimate = RenderJob::estimate(safe->files);

                safe->filesInfo.setText(juce::String(estimate.songs) + (estimate.songs == 1 ? " song, " : " songs, ")
                                            + juce::String(estimate.passes) + " passes, about " + formatDuration(estimate.seconds),
                                        juce::dontSendNotification);
                safe->errorLabel.setText(estimate.notes.joinIntoString("  "), juce::dontSendNotification);
            });
    }

    void chooseFolder()
    {
        chooser = std::make_shared<juce::FileChooser>("Choose the folder for the .merik files", juce::File(prefs.outputFolder));

        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<RenderContent>(this)](const juce::FileChooser& fc)
            {
                if (safe == nullptr || !fc.getResult().isDirectory())
                    return;

                safe->prefs.outputFolder = fc.getResult().getFullPathName();
                safe->folderPath.setText(safe->prefs.outputFolder, juce::dontSendNotification);
                safe->savePrefs();
            });
    }

    void startRender()
    {
        errorLabel.setText({}, juce::dontSendNotification);

        juce::String problem;

        if (files.empty())
            problem = "Choose one or more MIDI files.";
        else if (!juce::File(prefs.outputFolder).isDirectory())
            problem = "Choose an output folder.";
        else if (prefs.midiOutIdentifier.isEmpty())
            problem = "Choose the MIDI output.";
        else if (prefs.inputRoute == 0)
            problem = "Choose the input channels.";

        if (problem.isEmpty())
        {
            RenderOptions options;
            options.midiOutIdentifier = prefs.midiOutIdentifier;
            options.inputDevice = prefs.inputDevice;
            options.inputRoute = prefs.inputRoute;
            options.delayMs = prefs.delayMs;
            options.outputFolder = juce::File(prefs.outputFolder);
            options.keepWav = prefs.keepWav;
            options.toFlac = prefs.toFlac;
            options.gmReset = prefs.gmReset;
            options.midiFiles = files;

            if (context.start)
                problem = context.start(options);
        }

        if (problem.isNotEmpty())
        {
            errorLabel.setText(problem, juce::dontSendNotification);
            return;
        }

        showingProgress = true;
        updateView();
    }

    // Instellingen of voortgang tonen.
    void updateView()
    {
        const bool progress = showingProgress;

        for (auto* component : std::initializer_list<juce::Component*> {
                 &heading, &hint, &midiOutLabel, &deviceLabel, &inputLabel, &calibrateLabel, &filesLabel, &folderLabel,
                 &midiOutBox, &deviceBox, &inputBox, &calibrateButton, &delayCaption, &delayEditor, &chooseFilesButton,
                 &chooseFolderButton, &filesInfo, &folderPath, &keepWavToggle, &flacToggle, &gmResetToggle, &errorLabel,
                 &startButton })
            component->setVisible(!progress);

        for (auto* component : std::initializer_list<juce::Component*> {
                 &titleLabel, &songLabel, &passLabel, &etaLabel, progressBar.get(), &log })
            component->setVisible(progress);

        const bool busy = isBusy();
        stopButton.setVisible(progress && busy);
        stopButton.setEnabled(true);
        backButton.setVisible(progress && !busy);
    }

    void fillDevices()
    {
        if (context.deviceManager == nullptr)
            return;

        // MIDI-uitgangen
        const auto devices = juce::MidiOutput::getAvailableDevices();
        juce::String midiSignature;

        for (const auto& device : devices)
            midiSignature += device.identifier + ",";

        if (midiSignature != lastMidiSignature)
        {
            lastMidiSignature = midiSignature;
            midiIds.clear();
            midiOutBox.clear(juce::dontSendNotification);
            midiOutBox.addItem("Choose a MIDI output...", 1);

            int id = 2;

            for (const auto& device : devices)
            {
                midiOutBox.addItem(device.name, id++);
                midiIds.add(device.identifier);
            }

            const int index = midiIds.indexOf(prefs.midiOutIdentifier);
            midiOutBox.setSelectedId(index >= 0 ? index + 2 : 1, juce::dontSendNotification);
        }

        // Ingangsapparaten: standaard hetzelfde apparaat als de uitgang van Merik.
        const auto inputs = RenderEngine::inputDeviceNames(*context.deviceManager);
        const auto deviceSignature = inputs.joinIntoString(",") + "|" + prefs.inputDevice;

        if (deviceSignature != lastDeviceSignature)
        {
            lastDeviceSignature = deviceSignature;
            deviceNames = inputs;
            deviceBox.clear(juce::dontSendNotification);
            deviceBox.addItem("Same device as the audio output", 1);

            int id = 2;

            for (const auto& name : inputs)
                deviceBox.addItem(name, id++);

            const int index = inputs.indexOf(prefs.inputDevice);
            deviceBox.setSelectedId(prefs.inputDevice.isNotEmpty() && index >= 0 ? index + 2 : 1, juce::dontSendNotification);
        }

        // Kanalen van dat apparaat.
        const auto names = RenderEngine::availableInputChannels(*context.deviceManager, prefs.inputDevice);
        const auto channelSignature = names.joinIntoString(",") + "|" + juce::String(prefs.inputRoute) + "|" + prefs.inputDevice;

        if (channelSignature != inputSignature)
        {
            inputSignature = channelSignature;
            inputBox.clear(juce::dontSendNotification);

            if (names.isEmpty())
            {
                inputBox.addItem("This device has no inputs", 1);
                inputBox.setSelectedId(1, juce::dontSendNotification);
            }
            else
            {
                inputBox.addItem("Choose the input channels...", 1);

                for (int i = 0; i + 1 < names.size(); i += 2)
                    inputBox.addItem(names[i] + " + " + names[i + 1] + " (stereo)", RenderEngine::pairBase + i);

                for (int i = 0; i < names.size(); ++i)
                    inputBox.addItem(names[i] + " (mono)", RenderEngine::monoBase + i);

                inputBox.setSelectedId(inputBox.indexOfItemId(prefs.inputRoute) >= 0 ? prefs.inputRoute : 1,
                                       juce::dontSendNotification);
            }
        }
    }

    void timerCallback() override
    {
        const bool busy = isBusy();

        if (!showingProgress)
        {
            fillDevices();

            // De vertraging kan door het testvenster zijn gemeten.
            if (context.getPrefs && !delayEditor.hasKeyboardFocus(true))
            {
                const auto latest = context.getPrefs();
                const auto text = juce::String(latest.delayMs, 1);

                if (delayEditor.getText() != text)
                    delayEditor.setText(text, juce::dontSendNotification);

                prefs.delayMs = latest.delayMs;
            }

            return;
        }

        const RenderStatus status = context.status ? context.status() : RenderStatus();

        progressValue = status.totalSeconds > 0.0 ? juce::jlimit(0.0, 1.0, status.doneSeconds / status.totalSeconds) : 0.0;

        titleLabel.setText(status.finished ? (status.stopped ? "Stopped" : "Done") : "Rendering...", juce::dontSendNotification);
        songLabel.setText(status.songs > 0 ? "Song " + juce::String(status.song) + " of " + juce::String(status.songs)
                                                 + ":  " + status.songName
                                           : juce::String(),
                          juce::dontSendNotification);
        passLabel.setText(status.finished ? status.step
                                          : (status.passes > 0 ? "Pass " + juce::String(status.pass) + " of " + juce::String(status.passes)
                                                                     + ":  " + status.passName + "   (" + status.step + ")"
                                                               : juce::String("Preparing...")),
                          juce::dontSendNotification);

        const double left = juce::jmax(0.0, status.totalSeconds - status.doneSeconds);
        etaLabel.setText(status.finished ? juce::String() : "About " + formatDuration(left) + " left",
                         juce::dontSendNotification);

        const auto logText = status.log.joinIntoString("\n");

        if (logText != logShown)
        {
            logShown = logText;
            log.setText(logText, false);
            log.moveCaretToEnd();
        }

        if (status.error.isNotEmpty() && errorLabel.getText().isEmpty())
            errorLabel.setText(status.error, juce::dontSendNotification);

        stopButton.setVisible(busy);
        backButton.setVisible(!busy);
    }

    RenderContext context;
    RenderPrefs prefs;
    bool showingProgress = false;
    double progressValue = 0.0;

    std::vector<juce::File> files;
    juce::StringArray midiIds, deviceNames;
    juce::String lastMidiSignature, lastDeviceSignature, inputSignature, logShown;
    std::shared_ptr<juce::FileChooser> chooser;

    juce::Label heading, hint, midiOutLabel, deviceLabel, inputLabel, calibrateLabel, delayCaption, filesLabel, folderLabel;
    juce::Label filesInfo, folderPath, errorLabel, titleLabel, songLabel, passLabel, etaLabel;
    juce::ComboBox midiOutBox, deviceBox, inputBox;
    juce::TextButton calibrateButton, chooseFilesButton, chooseFolderButton, startButton, stopButton, backButton;
    juce::TextEditor delayEditor, log;
    juce::ToggleButton keepWavToggle, flacToggle, gmResetToggle;
    std::unique_ptr<juce::ProgressBar> progressBar;
};

class RenderWindow final : public juce::DialogWindow
{
public:
    RenderWindow(RenderContent* content, juce::Component* owner)
        : juce::DialogWindow("Render MIDI files", backgroundColour, true), renderContent(content)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        setContentOwned(content, true);
        centreAroundComponent(owner, getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        // Tijdens het opnemen eerst vragen: sluiten stopt het opnemen.
        if (renderContent != nullptr && renderContent->isBusy())
        {
            renderContent->askToStop([safe = juce::Component::SafePointer<juce::Component>(this)]
            {
                if (safe != nullptr)
                    delete safe.getComponent();
            });
            return;
        }

        delete this;
    }

private:
    juce::Component::SafePointer<RenderContent> renderContent;
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

void MainComponent::loadRenderPrefs()
{
    auto* properties = appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    renderPrefs.midiOutIdentifier = properties->getValue("render.midiOut");
    renderPrefs.inputDevice = properties->getValue("render.inputDevice");
    renderPrefs.inputRoute = properties->getIntValue("render.input", 0);
    renderPrefs.delayMs = properties->getDoubleValue("render.delayMs", 0.0);
    renderPrefs.outputFolder = properties->getValue("render.folder");
    renderPrefs.keepWav = properties->getBoolValue("render.keepWav", false);
    renderPrefs.toFlac = properties->getBoolValue("render.flac", true);
    renderPrefs.gmReset = properties->getBoolValue("render.gmReset", false);
}

void MainComponent::saveRenderPrefs()
{
    auto* properties = appProperties.getUserSettings();

    if (properties == nullptr)
        return;

    properties->setValue("render.midiOut", renderPrefs.midiOutIdentifier);
    properties->setValue("render.inputDevice", renderPrefs.inputDevice);
    properties->setValue("render.input", renderPrefs.inputRoute);
    properties->setValue("render.delayMs", renderPrefs.delayMs);
    properties->setValue("render.folder", renderPrefs.outputFolder);
    properties->setValue("render.keepWav", renderPrefs.keepWav);
    properties->setValue("render.flac", renderPrefs.toFlac);
    properties->setValue("render.gmReset", renderPrefs.gmReset);
    properties->saveIfNeeded();
}

juce::String MainComponent::startRender(const RenderOptions& options)
{
    if (renderJob != nullptr)
        return "A render is already running.";

    if (calibrationWindow != nullptr)
        return "Close the levels and delay window first.";

    // Terwijl er opgenomen wordt speelt Merik zelf niets.
    synthEngine.stop();
    isPlaying = false;
    playButton.setButtonText("Play");

    if (renderEngine == nullptr)
        renderEngine = std::make_unique<RenderEngine>(deviceManager);

    juce::String error;

    if (!renderEngine->start(options.midiOutIdentifier, options.inputDevice, options.inputRoute, error))
        return error;

    lastRenderStatus = {};
    renderJob = std::make_unique<RenderJob>(*renderEngine, options);
    return {};
}

void MainComponent::stopRender()
{
    if (renderJob != nullptr)
        renderJob->requestStop();
}

// Rondt een opname af (aangeroepen door de timer): het audioapparaat gaat terug naar normaal.
void MainComponent::pollRender()
{
    if (renderJob == nullptr)
        return;

    const auto status = renderJob->status();

    if (!status.finished)
        return;

    lastRenderStatus = status;
    renderJob.reset();

    if (renderEngine != nullptr)
        renderEngine->stop();
}

void MainComponent::showRenderWindow()
{
    if (renderWindow != nullptr)
    {
        renderWindow->toFront(true);
        return;
    }

    RenderContext context;
    context.deviceManager = &deviceManager;
    context.prefs = renderPrefs;
    context.prefsChanged = [this](const RenderPrefs& prefs) { renderPrefs = prefs; saveRenderPrefs(); };
    context.getPrefs = [this] { return renderPrefs; };
    context.start = [this](const RenderOptions& options) { return startRender(options); };
    context.stop = [this] { stopRender(); };
    context.status = [this] { return renderJob != nullptr ? renderJob->status() : lastRenderStatus; };
    context.busy = [this] { return renderJob != nullptr; };
    context.openCalibration = [this] { showRenderCalibration(); };

    renderWindow = new RenderWindow(new RenderContent(std::move(context)), this);
}

void MainComponent::showRenderCalibration()
{
    if (calibrationWindow != nullptr)
    {
        calibrationWindow->toFront(true);
        return;
    }

    if (renderJob != nullptr)
    {
        confirmDialog("Levels and delay", "Wait until the render has finished, or stop it first.", "OK", nullptr);
        return;
    }

    if (renderEngine == nullptr)
        renderEngine = std::make_unique<RenderEngine>(deviceManager);

    juce::String error;

    if (renderPrefs.midiOutIdentifier.isEmpty())
        error = "Choose the MIDI output first (Settings > Tools).";
    else if (renderPrefs.inputRoute == 0)
        error = "Choose the audio input channels first (Settings > Tools).";
    else
        renderEngine->start(renderPrefs.midiOutIdentifier, renderPrefs.inputDevice, renderPrefs.inputRoute, error);

    if (error.isNotEmpty())
    {
        confirmDialog("Levels and delay", error, "OK", nullptr);
        return;
    }

    auto* content = new RenderCalibrationContent(
        *renderEngine,
        renderPrefs.gmReset,
        [this](double milliseconds)
        {
            renderPrefs.delayMs = milliseconds;
            saveRenderPrefs();
        },
        [this]
        {
            if (renderEngine != nullptr)
                renderEngine->stop();
        });

    calibrationWindow = new RenderCalibrationWindow(content, this);
}

void MainComponent::showMerikMaker()
{
    if (makerWindow != nullptr)
    {
        makerWindow->toFront(true);
        return;
    }

    auto* content = new MerikMakerContent(
        currentMidiFile.existsAsFile() ? currentMidiFile.getParentDirectory() : juce::File());

    // Een gemaakt bestand kan meteen onderaan de getoonde setlist.
    content->onAddToSetlist = [this](const juce::File& file)
    {
        if (selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
            return;

        SongEntry entry;
        entry.file = file;
        setlists[static_cast<std::size_t>(selectedSetlist)].songs.push_back(std::move(entry));

        savePersistentState();
        refreshSongModel();
        refreshSetlistModel();
        scanSongInfo();
    };

    makerWindow = new MerikMakerWindow(content, this);
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
        updateCheckToggle,
        [this] { checkForUpdates(true); },
        [this] { showMerikMaker(); },

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
        },

        mainRoute.load(),
        [this](int route)
        {
            mainRoute = route;

            if (auto* properties = appProperties.getUserSettings())
            {
                properties->setValue("main.route", route);
                properties->saveIfNeeded();
            }
        },

        clickRoute.load(),
        clickOffsetMs,
        [this](int route, int offsetMs)
        {
            clickOffsetMs = offsetMs;
            saveClickSettings(route, offsetMs);
        },

        [this]
        {
            std::array<int, 7> routes {};

            for (std::size_t i = 0; i < routes.size(); ++i)
                routes[i] = stemRoutes[i].load();

            return routes;
        }(),
        [this](int family, int route)
        {
            stemRoutes[static_cast<std::size_t>(family)] = route;

            if (auto* properties = appProperties.getUserSettings())
            {
                properties->setValue("stem.route." + juce::String(family), route);
                properties->saveIfNeeded();
            }
        },

        [this] { showRenderWindow(); }
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
// Duur, lyrics en akkoorden van de nummers (op de achtergrond uitgelezen)
// ============================================================================

namespace
{
juce::String formatLength(double seconds)
{
    const int total = static_cast<int>(std::lround(seconds));
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;

    return hours > 0
        ? juce::String(hours) + ":" + juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(secs).paddedLeft('0', 2)
        : juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(secs).paddedLeft('0', 2);
}
}

class SongInfoScanner final : private juce::Thread
{
public:
    struct Info
    {
        double length = 0.0;
        int lyrics = 0;
        int chords = 0;
        int midi = 0;
        int audio = 0;
    };

    SongInfoScanner(std::mutex& readMutexToUse,
                    std::function<void(const juce::File&, const Info&)> resultCallback)
        : juce::Thread("Merik song info"),
          readMutex(readMutexToUse),
          onResult(std::move(resultCallback))
    {
        startThread(juce::Thread::Priority::low);
    }

    ~SongInfoScanner() override
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread(10000);
    }

    // Vervangt de wachtrij door deze volgorde (de nummers die nu in beeld
    // zijn staan vooraan, de rest daarna).
    void setQueue(const std::vector<juce::File>& files)
    {
        {
            const std::scoped_lock lock(queueMutex);
            queue.assign(files.begin(), files.end());
        }

        wake.signal();
    }

private:
    void run() override
    {
        while (!threadShouldExit())
        {
            juce::File next;

            {
                const std::scoped_lock lock(queueMutex);

                if (!queue.empty())
                {
                    next = queue.front();
                    queue.pop_front();
                }
            }

            if (next == juce::File())
            {
                wake.wait(500);
                continue;
            }

            Info info;

            try
            {
                MidiFileReader reader;
                Song song;

                {
                    const std::scoped_lock lock(readMutex);
                    juce::File midiPath = next;

                    if (MerikPackage::isPackage(next))
                    {
                        juce::String packageError;
                        midiPath = MerikPackage::midiFor(next, packageError);
                    }

                    song = reader.read(midiPath.getFullPathName().toStdString(), 48000.0);

                    if (MerikPackage::isPackage(next))
                        MerikPackage::applyScore(next, song);
                }

                double end = 0.0;

                if (!song.playbackEvents.empty())
                    end = std::max(end, song.playbackEvents.back().seconds);
                if (!song.lyrics.empty())
                    end = std::max(end, song.lyrics.back().seconds);
                if (!song.chords.empty())
                    end = std::max(end, song.chords.back().endSeconds);

                info.length = end;
                info.lyrics = song.lyrics.empty() ? 0 : 1;
                info.chords = song.chords.empty() ? 0 : 1;

                // MIDI = er zijn noten om te spelen (een bestand met alleen
                // tempo of tekst telt niet); audio = stems in een .merik.
                info.midi = std::any_of(song.playbackEvents.begin(), song.playbackEvents.end(),
                    [](const RawMidiEvent& event)
                    {
                        return event.bytes.size() >= 3 && (event.bytes[0] & 0xF0) == 0x90 && event.bytes[2] > 0;
                    }) ? 1 : 0;

                info.audio = MerikPackage::isPackage(next) && MerikPackage::stemCount(next) > 0 ? 1 : 0;
            }
            catch (const std::exception&)
            {
                // Onleesbaar bestand: duur 0 zodat het niet steeds opnieuw wordt geprobeerd.
            }

            juce::MessageManager::callAsync([callback = onResult, next, info]
            {
                if (callback)
                    callback(next, info);
            });
        }
    }

    std::mutex& readMutex;
    std::function<void(const juce::File&, const Info&)> onResult;

    std::mutex queueMutex;
    std::deque<juce::File> queue;
    juce::WaitableEvent wake;
};

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
    std::function<bool(int row)> isPlayingSetlist;

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

        // Rechts drie vaste kolommen, zodat alles netjes onder elkaar staat:
        // aantal | totale tijd | "(+3?)" als er nog duren onbekend zijn.
        double total = 0.0;
        int unknown = 0;

        for (const auto& song : setlist.songs)
        {
            if (song.lengthSeconds >= 0.0)
                total += song.lengthSeconds;
            else if (!song.missing)
                ++unknown;
        }

        constexpr int unknownWidth = 44;
        constexpr int timeWidth = 62;
        constexpr int countWidth = 34;

        const int unknownX = width - 6 - unknownWidth;
        const int timeX = unknownX - timeWidth;
        const int countX = timeX - countWidth;

        g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.85f) : secondaryTextColour);
        g.setFont(makeNotoFont(15.0f));
        g.drawText(juce::String(static_cast<int>(setlist.songs.size())), countX, 0, countWidth - 6, height,
                   juce::Justification::centredRight);
        g.drawText(formatLength(total), timeX, 0, timeWidth, height, juce::Justification::centredRight);

        if (unknown > 0)
        {
            g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.85f) : juce::Colour::fromRGB(245, 166, 35));
            g.setFont(makeNotoFont(13.0f));
            g.drawText("+" + juce::String(unknown) + "?", unknownX + 6, 0, unknownWidth - 6, height,
                       juce::Justification::centredLeft);
        }

        // Driehoekje bij de setlist waaruit het huidige nummer speelt.
        if (isPlayingSetlist && isPlayingSetlist(rowNumber))
        {
            const float cy = static_cast<float>(height) * 0.5f;
            juce::Path triangle;
            triangle.addTriangle(18.0f, cy - 5.0f, 18.0f, cy + 5.0f, 26.0f, cy);

            g.setColour(rowIsSelected ? juce::Colours::white : merikBlue);
            g.fillPath(triangle);
        }

        g.setColour(textColour);
        g.setFont(makeNotoFont(17.0f));

        g.drawText(
            setlist.name,
            32,
            0,
            countX - 38,
            height,
            juce::Justification::centredLeft,
            true);
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
    std::function<bool(int row)> isCurrentSong;

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
        if (isCurrentSong && isCurrentSong(rowNumber))
        {
            const float cy = static_cast<float>(height) * 0.5f;
            juce::Path triangle;
            triangle.addTriangle(50.0f, cy - 5.0f, 50.0f, cy + 5.0f, 58.0f, cy);

            g.setColour(rowIsSelected ? juce::Colours::white : merikBlue);
            g.fillPath(triangle);
        }

        // Duur
        g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.85f) : secondaryTextColour);
        g.setFont(makeNotoFont(15.0f));
        g.drawText(entry.lengthSeconds >= 0.0 ? formatLength(entry.lengthSeconds) : juce::String("--:--"),
                   64, 0, 50, height, juce::Justification::centredLeft);

        // Rechts: vaste kolommen voor lyrics, akkoorden en transpose, zodat
        // gelijke labels altijd onder elkaar staan (ook als de ander ontbreekt).
        constexpr int transposeSlot = 30, chordsSlot = 52, lyricsSlot = 52, audioSlot = 46, midiSlot = 38, gap = 4;

        int rightEdge = width - 10;

        const auto drawTag = [&](const juce::String& text, int slotWidth, juce::Colour colour, bool show)
        {
            if (show)
            {
                const auto tag = juce::Rectangle<float>(
                    static_cast<float>(rightEdge - slotWidth),
                    (static_cast<float>(height) - 16.0f) * 0.5f,
                    static_cast<float>(slotWidth), 16.0f);

                g.setColour(rowIsSelected ? juce::Colours::white.withAlpha(0.25f) : colour.withAlpha(0.28f));
                g.fillRoundedRectangle(tag, 8.0f);

                g.setColour(textColour);
                g.setFont(makeNotoFont(12.0f));
                g.drawText(text, tag.toNearestInt(), juce::Justification::centred);
            }

            rightEdge -= slotWidth + gap;
        };

        drawTag((entry.transpose > 0 ? "+" : "") + juce::String(entry.transpose), transposeSlot, merikBlue,
                entry.transpose != 0);
        drawTag("CHORDS", chordsSlot, juce::Colour::fromRGB(46, 204, 113), entry.hasChords == 1);
        drawTag("LYRICS", lyricsSlot, juce::Colour::fromRGB(245, 166, 35), entry.hasLyrics == 1);
        drawTag("AUDIO", audioSlot, juce::Colour::fromRGB(0, 164, 235), entry.hasAudio == 1);
        drawTag("MIDI", midiSlot, juce::Colour::fromRGB(160, 120, 255), entry.hasMidi == 1);

        // Ontbrekende bestanden staan rood.
        const auto nameColour = entry.missing
            ? (rowIsSelected ? juce::Colour::fromRGB(255, 190, 190) : juce::Colour::fromRGB(255, 85, 85))
            : textColour;

        g.setColour(nameColour);
        g.setFont(makeNotoFont(17.0f));

        g.drawText(
            entry.shownName(true),
            120,
            0,
            rightEdge - 120,
            height,
            juce::Justification::centredLeft,
            true);
    }

    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || selectedSetlist == nullptr)
            return {};

        juce::StringArray list;

        for (int i = 0; i < rows.size(); ++i)
            list.add(juce::String(rows[i]));

        return "song:" + juce::String(*selectedSetlist) + ":" + list.joinIntoString(",");
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
            extension != ".kar" &&
            extension != ".merik")
        {
            return false;
        }

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
        "7 Others",
        "8 Click"
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
        // De kanalentabel heeft geen selectie: een klik laat geen blauwe regel achter.
        rowIsSelected = false;

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
        rowIsSelected = false;

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
                    // Kanaal 10 is percussie: daar is het programma een drumkit
                    // (Standard, Room, Power, ...) en geen instrument.
                    value =
                        juce::String(program + 1)
                        + " "
                        + (rowNumber == 9 ? getDrumKitName(program)
                                          : juce::String(getGMProgramName(program)));
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

    // Drumkits op kanaal 10, op programmanummer (GM, GM2 en Roland GS).
    static juce::String getDrumKitName(int program)
    {
        switch (program)
        {
            case 0:   return "Standard Kit";
            case 1:   return "Standard Kit 2";
            case 2:   return "Standard Kit 3";
            case 8:   return "Room Kit";
            case 16:  return "Power Kit";
            case 24:  return "Electronic Kit";
            case 25:  return "TR-808 / Analog Kit";
            case 26:  return "Dance Kit";
            case 27:  return "CR-78 Kit";
            case 28:  return "TR-606 Kit";
            case 29:  return "TR-707 Kit";
            case 30:  return "TR-909 Kit";
            case 32:  return "Jazz Kit";
            case 40:  return "Brush Kit";
            case 48:  return "Orchestra Kit";
            case 49:  return "Ethnic Kit";
            case 50:  return "Kick & Snare Kit";
            case 56:  return "SFX Kit";
            case 127: return "CM-64/CM-32L Kit";
            default:  break;
        }

        // Onbekend programma: de dichtstbijzijnde bekende kit eronder
        // (bij GS valt een variant onder de kit ervoor), anders algemeen.
        static constexpr int known[] = { 0, 8, 16, 24, 32, 40, 48, 56 };
        int base = -1;

        for (const int kit : known)
            if (kit <= program)
                base = kit;

        return base >= 0 ? getDrumKitName(base) + " (variation)" : juce::String("Drum Kit");
    }

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

const std::array<juce::String, 8>& MainComponent::defaultFamilyLabels()
{
    static const std::array<juce::String, 8> labels =
    {
        "1 Drums CH10", "2 Bass", "3 Guitars", "4 Keys", "5 Strings", "6 Winds", "7 Others", "8 Click"
    };

    return labels;
}

// Namen uit de stems ("7-void.mp3" -> "7 void") op de familieknoppen en in de web-mixer.
void MainComponent::showFamilyNames(const std::array<std::string, 8>& names)
{
    for (std::size_t i = 0; i < familyControls.size(); ++i)
    {
        familyControls[i].labelButton.setButtonText(
            names[i].empty() ? defaultFamilyLabels()[i]
                             : juce::String(static_cast<int>(i) + 1) + " " + juce::String::fromUTF8(names[i].c_str()));
    }

    webServer.setFamilyNames(names);
}

void MainComponent::setTranspose(int semitones)
{
    if (songHasStems)
        semitones = 0;   // audio kan niet transponeren

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
    pollRender();
    updateTransportDisplay();
    channelTable.repaint();

    webServer.setPositionSamples(synthEngine.positionSamples());
    webServer.setTransportInfo(
        synthEngine.isPlaying(),
        nextSetlistEntry() != nullptr
            ? nextSetlistEntry()->shownName(false).toStdString()
            : std::string());

    const auto familySettings = synthEngine.getAllFamilySettings();

    // Niveaus en bron per familie (app en web).
    const auto peaks = synthEngine.takeMeters();
    const auto stemMask = synthEngine.stemFamilyMask();
    const auto bufferingMask = synthEngine.takeBufferingMask();
    std::array<float, 8> levels {};

    for (std::size_t i = 0; i < familyControls.size(); ++i)
    {
        auto& control = familyControls[i];
        control.meter.setPeak(peaks[i]);
        levels[i] = control.meter.level();

        // AUDIO als de familie een stem heeft (ook de klik: een click-bestand),
        // anders MIDI (de klik zelf: CLICK).
        {
            const bool audio = (stemMask & (1u << i)) != 0;
            if ((bufferingMask & (1u << i)) != 0)
                control.sourcePill.set("BUFFER", juce::Colour::fromRGB(245, 166, 35));   // stem kon niet op tijd van schijf
            else
                control.sourcePill.set(audio ? "AUDIO" : (i == 7 ? "CLICK" : "MIDI"),
                                       audio ? juce::Colour::fromRGB(0, 164, 235)
                                             : (i == 7 ? juce::Colour::fromRGB(160, 164, 178)
                                                       : juce::Colour::fromRGB(160, 120, 255)));
        }
    }

    webServer.setMeters(levels, stemMask);

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

    if (songInfoDirty && ++songInfoSaveTicks >= 40)
    {
        songInfoDirty = false;
        songInfoSaveTicks = 0;
        savePersistentState();
    }

    // Next heeft alleen zin als er nog een nummer na dit nummer komt.
    const bool nextAvailable = hasNextSong();

    if (nextButton.isEnabled() != nextAvailable)
        nextButton.setEnabled(nextAvailable);
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

        mainRoute = properties->getIntValue("main.route", ClickRoute::pairBase);
        clickRoute = properties->getIntValue("click.route", ClickRoute::automatic);
        loadRenderPrefs();

        for (std::size_t i = 0; i < stemRoutes.size(); ++i)
            stemRoutes[i] = properties->getIntValue("stem.route." + juce::String(static_cast<int>(i)), 0);
        synthEngine.setClickOffsetMs(properties->getIntValue("click.offsetMs", 0));
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

    const auto& familyNames = defaultFamilyLabels();

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
            juce::Justification::centredRight);

        addAndMakeVisible(control.valueLabel);

        // Bron (audio/MIDI/klik) en niveaumeter
        control.sourcePill.set(i == 7 ? "CLICK" : "MIDI",
                               i == 7 ? juce::Colour::fromRGB(160, 164, 178) : juce::Colour::fromRGB(160, 120, 255));
        addAndMakeVisible(control.sourcePill);
        addAndMakeVisible(control.meter);

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

    // Klik = alleen kijken; met Command (macOS) of Ctrl/Alt (Windows) meteen
    // met de besturingssleutel, zodat Play/Stop/Next en de mixer er ook zijn.
    lyricsButton.onClick = [this]
    {
        const auto modifiers = juce::ModifierKeys::currentModifiers;
        auto address = "http://localhost:" + juce::String(WebServer::defaultPort) + "/";

        if ((modifiers.isCommandDown() || modifiers.isAltDown()) && webControlKey.isNotEmpty())
            address += "?key=" + juce::URL::addEscapeChars(webControlKey, true);

        juce::URL(address).launchInDefaultBrowser();
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
        { &doubleClickToggle, &normalizeToggle, &continuousToggle, &updateCheckToggle })
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
                scanSongInfo();
            });

    songModel =
        std::make_unique<SongModel>(
            &setlists,
            &selectedSetlist);

    setlistBox.setModel(setlistModel.get());
    songBox.setModel(songModel.get());

    setlistBox.setRowHeight(19);
    songBox.setRowHeight(19);

    // Meerdere nummers tegelijk selecteren (Shift/Command-klik).
    songBox.setMultipleSelectionEnabled(true);

    refreshSetlistModel();
    refreshSongModel();

    setlistModel->onContextMenu = [this](int row) { showSetlistMenu(row); };
    setlistModel->onDeleteKey   = [this](int row) { removeSetlistAt(row); };

    songModel->onContextMenu = [this](int row) { showSongMenu(row); };
    songModel->onDeleteKey   = [this](int) { removeSongs(selectedSongRows()); };

    songModel->isCurrentSong = [this](int row)
    {
        return selectedSetlist == playingSetlist && row == currentRowInPlayingSetlist();
    };

    setlistModel->isPlayingSetlist = [this](int row)
    {
        return currentSongFromSetlist && row == playingSetlist;
    };

    songModel->onDoubleClick = [this](int row)
    {
        if (const auto* found = songModel->getEntry(row))
        {
            const SongEntry entry = *found;
            loadMidiFile(entry.file, true, &entry, row, selectedSetlist);

            if (doubleClickToggle.getToggleState())
                play();
        }
    };

    // ---- slepen: setlists herschikken, songs herschikken/verplaatsen/kopiëren
    // "song:<setlist>:<rij>,<rij>,..."
    auto parseSong = [](const juce::String& description, int& setlist, juce::Array<int>& rows)
    {
        const auto parts = juce::StringArray::fromTokens(description, ":", "");

        if (parts.size() != 3 || parts[0] != "song")
            return false;

        setlist = parts[1].getIntValue();
        rows.clear();

        for (const auto& token : juce::StringArray::fromTokens(parts[2], ",", ""))
            rows.add(token.getIntValue());

        return !rows.isEmpty();
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

            int sourceSetlist = 0;
            juce::Array<int> sourceRows;
            const int target = setlistBox.getRowContainingPosition(position.x, position.y);

            if (parseSong(description, sourceSetlist, sourceRows) && target >= 0 && target != sourceSetlist)
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

            int sourceSetlist = 0;
            juce::Array<int> sourceRows;
            const int target = setlistBox.getRowContainingPosition(position.x, position.y);

            if (parseSong(description, sourceSetlist, sourceRows)
                && sourceSetlist == selectedSetlist && target >= 0)
                moveOrCopySongs(sourceRows, target, copy);
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
            int sourceSetlist = 0;
            juce::Array<int> sourceRows;

            if (parseSong(description, sourceSetlist, sourceRows) && sourceSetlist == selectedSetlist)
                moveSongsWithinSetlist(sourceRows,
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

    listSplitter.onDragTo = [this](int x)
    {
        const int gap = 10;
        const int usable = listsBounds.getWidth() - gap;

        if (usable <= 0)
            return;

        setlistFraction = juce::jlimit(
            0.15f, 0.7f,
            static_cast<float>(x - listsBounds.getX() - gap / 2) / static_cast<float>(usable));

        resized();
    };
    addAndMakeVisible(listSplitter);

    // Kleine "+"-knoppen in de kop van de kolommen. Verwijderen kan via het
    // rechtermuismenu of de Delete-toets.
    newSetlistButton.setButtonText("+");
    addButton.setButtonText("+");

    newSetlistButton.setTooltip("New setlist");
    addButton.setTooltip("Add songs");

    for (auto* button : { &newSetlistButton, &addButton })
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

    addButton.onClick = [this]
    {
        // Start in de map van het laatst gebruikte nummer.
        const auto startFolder = currentMidiFile.existsAsFile()
            ? currentMidiFile.getParentDirectory()
            : juce::File();

        auto chooser = std::make_shared<juce::FileChooser>(
            "Add MIDI files",
            startFolder,
            "*.mid;*.midi;*.kar;*.merik;*.MID;*.MIDI;*.KAR;*.MERIK");

        // canSelectFiles ontbrak: zonder die vlag zijn in het dialoogvenster
        // alle bestanden grijs en niet te kiezen.
        constexpr int flags =
            juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles
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
                    scanSongInfo();
                }
            });
    };

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

    updateCheckToggle.onClick = [this]
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

    // Spatiebalk = play/pauze, ook als een lijst het toetsenbord heeft
    // (die gebruikt de spatie anders voor selecteren).
    setWantsKeyboardFocus(true);
    songBox.addKeyListener(this);
    setlistBox.addKeyListener(this);
    channelTable.addKeyListener(this);

    // -------------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------------

    // Meerkanaals (klik op een eigen uitgang): maximaal 64 uitgangen, en de
    // laatst gekozen instellingen terug.
    {
        std::unique_ptr<juce::XmlElement> savedAudio;

        if (auto* properties = appProperties.getUserSettings())
            savedAudio = properties->getXmlValue("audioDeviceState");

        setAudioChannels(0, 64, savedAudio.get());
    }

    enableAllOutputChannels(deviceManager);

    deviceListener.onChange = [this]
    {
        enableAllOutputChannels(deviceManager);

        // Tijdens het opnemen staan de ingangen aan: dat onthouden we niet.
        if (renderEngine != nullptr && renderEngine->isRunning())
            return;

        if (auto state = deviceManager.createStateXml())
            if (auto* properties = appProperties.getUserSettings())
            {
                properties->setValue("audioDeviceState", state.get());
                properties->saveIfNeeded();
            }
    };
    deviceManager.addChangeListener(&deviceListener);

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

    setlistIndexLabel.setColour(juce::Label::textColourId, secondaryTextColour);
    setlistIndexLabel.setFont(makeNotoFont(20.0f));
    setlistIndexLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(setlistIndexLabel);

    updatePill.onClick = [this] { showUpdateDialog(); };
    addChildComponent(updatePill);

    beatIndicator.positionSeconds = [this]
    {
        return synthEngine.isPlaying()
            ? static_cast<double>(synthEngine.positionSamples()) / audioSampleRate
            : -1.0;
    };
    addAndMakeVisible(beatIndicator);

    songScanner = std::make_unique<SongInfoScanner>(
        midiReadMutex,
        [safeThis = juce::Component::SafePointer<MainComponent>(this)]
        (const juce::File& file, const SongInfoScanner::Info& info)
        {
            if (safeThis != nullptr)
                safeThis->applySongInfo(file, info.length, info.lyrics, info.chords, info.midi, info.audio);
        });

    updateMissingFlags();
    scanSongInfo();

    // Een paar seconden na de start kijken of er een nieuwe versie is.
    juce::Timer::callAfterDelay(
        4000,
        [safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr && safe->updateCheckToggle.getToggleState())
                safe->checkForUpdates(false);
        });

    // Voortgangsscherm voor het laden van een .merik (bovenop alles).
    loadingOverlay = std::make_unique<LoadingOverlay>();
    loadingOverlay->onCancel = [this] { cancelPackageLoad(); };
    addChildComponent(*loadingOverlay);




    // Knoppen nemen geen toetsenbordfocus: de spatiebalk is altijd play/pauze
    // en kan geen losse knop (zoals Next) activeren.
    for (auto* child : getChildren())
        if (dynamic_cast<juce::Button*>(child) != nullptr)
            child->setWantsKeyboardFocus(false);

    startTimerHz(20);

    //setSize(1200, 850);

    
}

// ============================================================================
// Destructor
// ============================================================================

MainComponent::~MainComponent()
{
    stopTimer();
    cancelPackageLoad();

    if (renderJob != nullptr)
        renderJob->requestStop();

    renderJob.reset();

    if (renderEngine != nullptr)
        renderEngine->stop();

    // Eerst de achtergronddraad stoppen: die gebruikt de MIDI-lezer.
    songScanner.reset();

    if (songInfoDirty)
        savePersistentState();

    synthEngine.stop();

    deviceManager.removeChangeListener(&deviceListener);
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

    updateCheckToggle.setToggleState(
        properties->getBoolValue("checkForUpdates", true),
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

        const auto addSong = [&setlist](SongEntry entry, double length, int lyrics, int chords,
                                        int midi, int audio, juce::int64 storedTime)
        {
            // Eerder uitgelezen duur / lyrics / akkoorden; alleen geldig
            // zolang het bestand niet gewijzigd is.
            if (storedTime != 0 && storedTime == entry.file.getLastModificationTime().toMilliseconds())
            {
                entry.fileTime = storedTime;
                entry.lengthSeconds = length;
                entry.hasLyrics = lyrics;
                entry.hasChords = chords;
                entry.hasMidi = midi;
                entry.hasAudio = audio;
            }

            setlist.songs.push_back(std::move(entry));
        };

        const auto songsKey = "setlist." + juce::String(i) + ".songsXml";

        if (auto songsXml = properties->getXmlValue(songsKey))
        {
            for (auto* item : songsXml->getChildWithTagNameIterator("s"))
            {
                const auto path = item->getStringAttribute("file");

                if (path.isEmpty())
                    continue;

                // Bewust ook bewaren als het bestand momenteel niet beschikbaar is.
                SongEntry entry;
                entry.file = juce::File(path);
                entry.displayName = item->getStringAttribute("name");
                entry.transpose = item->getIntAttribute("transpose", 0);

                addSong(std::move(entry),
                        item->getDoubleAttribute("length", -1.0),
                        item->getIntAttribute("lyrics", -1),
                        item->getIntAttribute("chords", -1),
                        item->getIntAttribute("midi", -1),
                        item->getIntAttribute("audio", -1),
                        item->getStringAttribute("fileTime", "0").getLargeIntValue());
            }
        }
        else
        {
            // Oud formaat (losse sleutels per nummer): inlezen en opruimen.
            const int songCount =
                properties->getIntValue("setlist." + juce::String(i) + ".songCount", 0);

            for (int j = 0; j < songCount; ++j)
            {
                const auto base = "setlist." + juce::String(i) + ".song." + juce::String(j);
                const auto path = properties->getValue(base);

                if (path.isNotEmpty())
                {
                    SongEntry entry;
                    entry.file = juce::File(path);
                    entry.displayName = properties->getValue(base + ".name", "");
                    entry.transpose = properties->getIntValue(base + ".transpose", 0);

                    addSong(std::move(entry),
                            properties->getDoubleValue(base + ".length", -1.0),
                            properties->getIntValue(base + ".lyrics", -1),
                            properties->getIntValue(base + ".chords", -1),
                            properties->getIntValue(base + ".midi", -1),
                            properties->getIntValue(base + ".audio", -1),
                            properties->getValue(base + ".fileTime", "0").getLargeIntValue());
                }

                for (const char* suffix : { "", ".name", ".transpose", ".fileTime", ".length",
                                            ".lyrics", ".chords", ".midi", ".audio" })
                    properties->removeValue(base + suffix);
            }

            properties->removeValue("setlist." + juce::String(i) + ".songCount");
            songInfoDirty = true;
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
        "checkForUpdates",
        updateCheckToggle.getToggleState());

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

        // Alle nummers van een setlist in één waarde: duizenden losse sleutels
        // maakten het opslaan traag (elke sleutel zoeken is een lineaire scan).
        juce::XmlElement songsXml("songs");

        for (const auto& song : setlists[i].songs)
        {
            auto* item = songsXml.createNewChildElement("s");
            item->setAttribute("file", song.file.getFullPathName());

            if (song.displayName.isNotEmpty())
                item->setAttribute("name", song.displayName);

            if (song.transpose != 0)
                item->setAttribute("transpose", song.transpose);

            item->setAttribute("fileTime", juce::String(song.fileTime));
            item->setAttribute("length", song.lengthSeconds);
            item->setAttribute("lyrics", song.hasLyrics);
            item->setAttribute("chords", song.hasChords);
            item->setAttribute("midi", song.hasMidi);
            item->setAttribute("audio", song.hasAudio);
        }

        properties->setValue("setlist." + index + ".songsXml", &songsXml);
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
    updateMissingFlags();

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
    if (loadingOverlay != nullptr)
        loadingOverlay->setBounds(getLocalBounds());

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

        // Pil links, de waarde rechts.
        control.sourcePill.setBounds(control.valueLabel.getBounds().withWidth(54).translated(6, 0));
        control.sourcePill.setVisible(familyWidth >= 78);
        control.valueLabel.setBounds(control.valueLabel.getBounds().withTrimmedRight(8));

        control.meter.setBounds(
            familyBlockBounds[i].getX() + 6, familyBlockBounds[i].getBottom() - 8,
            familyBlockBounds[i].getWidth() - 12, 4);

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
    // Symmetrisch, zodat de titel gecentreerd blijft; rechts "6/10".
    setlistIndexLabel.setBounds(titleArea.removeFromRight(90));

    // Links op dezelfde hoogte als de songteller: de pil voor een nieuwe versie.
    updatePill.setBounds(titleArea.getX(),
                         setlistIndexLabel.getBounds().getCentreY() - 12,
                         124, 24);
    songTitle.setBounds(titleArea.withTrimmedLeft(90));

    // Rechts de maatsoort met ledjes; "Next:" blijft symmetrisch gecentreerd.
    const auto nextRow = player.removeFromTop(36);

    auto indicatorArea = nextRow;
    beatIndicator.setBounds(indicatorArea.removeFromRight(200));
    nextSong.setBounds(nextRow.reduced(210, 0));

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

    // Verdeling via de greep tussen de kolommen (standaard 1/3 - 2/3);
    // de verhouding blijft gelijk als het venster van grootte verandert.
    listsBounds = lists;

    const int usable = lists.getWidth() - listGap;
    const int minimumSetlistWidth = juce::jmin(180, usable / 2);
    const int maximumSetlistWidth = juce::jmax(minimumSetlistWidth, usable - 300);

    const int listWidth = juce::jlimit(
        minimumSetlistWidth,
        maximumSetlistWidth,
        juce::roundToInt(static_cast<float>(usable) * setlistFraction));

    // -------------------------------------------------------------------------
    // Setlists - linker kolom
    // -------------------------------------------------------------------------

    auto setlistArea =
        lists.removeFromLeft(listWidth);

    listSplitter.setBounds(lists.removeFromLeft(listGap));

    // Kop met een kleine "+"-knop rechts.
    auto setlistHeader = setlistArea.removeFromTop(22);
    setlistsTitle.setBounds(setlistHeader);
    newSetlistButton.setBounds(setlistHeader.removeFromRight(30).reduced(3, 2));

    setlistBox.setBounds(setlistArea);

    // -------------------------------------------------------------------------
    // Songs - rechter helft
    // -------------------------------------------------------------------------

    auto songArea = lists;

    auto songHeader = songArea.removeFromTop(22);
    songsTitle.setBounds(songHeader);
    addButton.setBounds(songHeader.removeFromRight(30).reduced(3, 2));

    songBox.setBounds(songArea);
}    





bool MainComponent::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file(path);

        if (file.hasFileExtension(".mid;.midi;.kar;.merik"))
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
        scanSongInfo();
    }
}


// ============================================================================
// Setlists en songs: toevoegen, verwijderen, herschikken, hernoemen
// ============================================================================

// Eigen dialoog in de stijl van de app (in plaats van de standaard AlertWindow).
namespace
{
class StyledDialog final : public juce::Component
{
public:
    StyledDialog(const juce::String& message,
                 const juce::String* initialText,
                 const juce::String& okText,
                 bool showCancel,
                 std::function<void(const juce::String&)> onAcceptedToUse,
                 const juce::String& cancelText = "Cancel",
                 const juce::String& extraText = {},
                 std::function<void()> extraCallback = nullptr)
        : onAccepted(std::move(onAcceptedToUse)),
          onExtra(std::move(extraCallback))
    {
        setLookAndFeel(&merikLookAndFeel);

        messageLabel.setText(message, juce::dontSendNotification);
        messageLabel.setColour(juce::Label::textColourId, textColour);
        messageLabel.setFont(merikLookAndFeel.getMediumFont(16.0f));
        messageLabel.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(messageLabel);

        hasEditor = initialText != nullptr;

        if (hasEditor)
        {
            editor.setText(*initialText, juce::dontSendNotification);
            editor.setColour(juce::TextEditor::backgroundColourId, panelLightColour);
            editor.setColour(juce::TextEditor::textColourId, textColour);
            editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            editor.setColour(juce::TextEditor::focusedOutlineColourId, merikBlue);
            editor.setColour(juce::CaretComponent::caretColourId, textColour);
            editor.setFont(merikLookAndFeel.getMediumFont(18.0f));
            editor.setIndents(10, 0);
            editor.setJustification(juce::Justification::centredLeft);
            editor.onReturnKey = [this] { accept(); };
            editor.onEscapeKey = [this] { close(); };
            addAndMakeVisible(editor);
        }

        okButton.setButtonText(okText);
        okButton.getProperties().set("accent", true);
        okButton.onClick = [this] { accept(); };
        addAndMakeVisible(okButton);

        hasCancel = showCancel;

        if (showCancel)
        {
            cancelButton.setButtonText(cancelText);
            cancelButton.onClick = [this] { close(); };
            addAndMakeVisible(cancelButton);
        }

        hasExtra = extraText.isNotEmpty();

        if (hasExtra)
        {
            extraButton.setButtonText(extraText);
            extraButton.onClick = [this]
            {
                if (onExtra)
                    onExtra();

                close();
            };
            addAndMakeVisible(extraButton);
        }

        const int lines = message.retainCharacters("\n").length() + 1;
        const int messageHeight = juce::jlimit(22, 120, lines * 24 + (message.length() > 60 ? 24 : 0));

        setSize(420, 34 + messageHeight + (hasEditor ? 50 : 8) + 64);
        setWantsKeyboardFocus(true);
    }

    ~StyledDialog() override
    {
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(backgroundColour);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(20, 18);

        const int messageHeight = getHeight() - 36 - 64 - (hasEditor ? 50 : 8);
        messageLabel.setBounds(area.removeFromTop(juce::jmax(22, messageHeight)));

        if (hasEditor)
        {
            area.removeFromTop(8);
            editor.setBounds(area.removeFromTop(34));
        }

        auto buttons = area.removeFromBottom(34);
        okButton.setBounds(buttons.removeFromRight(110));
        buttons.removeFromRight(8);

        if (hasCancel)
            cancelButton.setBounds(buttons.removeFromRight(110));

        if (hasExtra)
            extraButton.setBounds(buttons.removeFromLeft(150));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            accept();
            return true;
        }

        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }

        return false;
    }

    void parentHierarchyChanged() override
    {
        // Het venster is nog niet zichtbaar op het moment dat het hier
        // binnenkomt: focus pas even later zetten (en nog een keer voor de
        // zekerheid), zodat je meteen kunt typen.
        if (findParentComponentOfClass<juce::DialogWindow>() == nullptr)
            return;

        const auto focus = [safe = juce::Component::SafePointer<StyledDialog>(this)]
        {
            if (safe == nullptr)
                return;

            if (auto* window = safe->findParentComponentOfClass<juce::DialogWindow>())
                window->toFront(true);

            if (safe->hasEditor)
            {
                if (!safe->editor.hasKeyboardFocus(false))
                {
                    safe->editor.grabKeyboardFocus();
                    safe->editor.selectAll();
                }
            }
            else
            {
                safe->grabKeyboardFocus();
            }
        };

        juce::Timer::callAfterDelay(60, focus);
        juce::Timer::callAfterDelay(250, focus);
    }

private:
    void accept()
    {
        if (onAccepted)
            onAccepted(editor.getText().trim());

        close();
    }

    void close()
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(0);
    }

    std::function<void(const juce::String&)> onAccepted;
    juce::Label messageLabel;
    juce::TextEditor editor;
    std::function<void()> onExtra;
    juce::TextButton okButton, cancelButton, extraButton;
    bool hasEditor = false;
    bool hasCancel = false;
    bool hasExtra = false;
};

void showStyledDialog(juce::Component* centreAround,
                      const juce::String& title,
                      std::unique_ptr<StyledDialog> content)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = title;
    options.dialogBackgroundColour = backgroundColour;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = centreAround;
    options.launchAsync();
}
}

void MainComponent::promptRename(
    const juce::String& title,
    const juce::String& current,
    std::function<void(const juce::String&)> onAccepted)
{
    showStyledDialog(
        this, title,
        std::make_unique<StyledDialog>("New name:", &current, "OK", true, std::move(onAccepted)));
}

void MainComponent::confirmDialog(
    const juce::String& title,
    const juce::String& message,
    const juce::String& okText,
    std::function<void()> onOk)
{
    const bool cancellable = onOk != nullptr;

    showStyledDialog(
        this, title,
        std::make_unique<StyledDialog>(
            message, nullptr, okText, cancellable,
            [onOk = std::move(onOk)](const juce::String&)
            {
                if (onOk)
                    onOk();
            }));
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

        if (row == playingSetlist)
        {
            // Het huidige nummer hoort niet meer bij een setlist.
            currentSongFromSetlist = false;
            playingSetlist = -1;
            playingRow = -1;
        }
        else if (row < playingSetlist)
        {
            --playingSetlist;
        }

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

    confirmDialog(
        "Remove setlist",
        "Remove \"" + setlist.name + "\" with "
            + juce::String(static_cast<int>(setlist.songs.size()))
            + " songs?\nThe MIDI files themselves are not deleted.",
        "Remove",
        remove);
}

juce::Array<int> MainComponent::selectedSongRows() const
{
    juce::Array<int> rows;
    const auto selection = songBox.getSelectedRows();

    for (int i = 0; i < selection.size(); ++i)
        rows.add(selection[i]);

    return rows;
}

void MainComponent::removeSongs(juce::Array<int> rows)
{
    if (rows.isEmpty() || songModel == nullptr)
        return;

    // Van achteren naar voren, zodat de rijnummers kloppen.
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    bool removed = false;

    for (const int row : rows)
        removed = songModel->removeFile(row) || removed;

    if (!removed)
        return;

    refreshSongModel();

    const int remaining = songModel->getNumRows();

    if (remaining > 0)
        songBox.selectRow(juce::jmin(rows.getLast(), remaining - 1));

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

    // Ook de setlist waaruit het huidige nummer speelt verschuift mee.
    if (playingSetlist == from)
    {
        playingSetlist = to;
    }
    else if (playingSetlist >= 0)
    {
        if (from < playingSetlist)
            --playingSetlist;

        if (to <= playingSetlist)
            ++playingSetlist;
    }

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

void MainComponent::moveSongsWithinSetlist(juce::Array<int> rows, int insertBefore)
{
    if (selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
        return;

    auto& songs = setlists[static_cast<std::size_t>(selectedSetlist)].songs;
    const int count = static_cast<int>(songs.size());

    std::sort(rows.begin(), rows.end());
    for (int i = rows.size() - 1; i >= 0; --i)
        if (rows[i] < 0 || rows[i] >= count)
            rows.remove(i);

    // Dubbele rijnummers eruit.
    for (int i = rows.size() - 1; i > 0; --i)
        if (rows[i] == rows[i - 1])
            rows.remove(i);

    if (rows.isEmpty())
        return;

    insertBefore = juce::jlimit(0, count, insertBefore);

    // Een blok dat al op zijn plek staat, laten we ongemoeid.
    bool contiguous = true;
    for (int i = 1; i < rows.size(); ++i)
        contiguous = contiguous && rows[i] == rows[i - 1] + 1;

    if (contiguous && insertBefore >= rows.getFirst() && insertBefore <= rows.getLast() + 1)
        return;

    std::vector<SongEntry> moved;

    for (const int row : rows)
        moved.push_back(std::move(songs[static_cast<std::size_t>(row)]));

    int target = insertBefore;

    for (const int row : rows)
        if (row < insertBefore)
            --target;

    for (int i = rows.size() - 1; i >= 0; --i)
        songs.erase(songs.begin() + rows[i]);

    songs.insert(songs.begin() + target,
                 std::make_move_iterator(moved.begin()),
                 std::make_move_iterator(moved.end()));

    refreshSongModel();

    // Het verplaatste blok blijft geselecteerd.
    juce::SparseSet<int> selection;
    selection.addRange({ target, target + static_cast<int>(moved.size()) });
    songBox.setSelectedRows(selection);

    savePersistentState();
}

void MainComponent::moveOrCopySongs(juce::Array<int> rows, int targetSetlist, bool copy)
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

    std::sort(rows.begin(), rows.end());
    for (int i = rows.size() - 1; i >= 0; --i)
        if (rows[i] < 0 || rows[i] >= static_cast<int>(source.size()))
            rows.remove(i);

    if (rows.isEmpty())
        return;

    juce::Array<int> done;

    for (const int row : rows)
    {
        const SongEntry entry = source[static_cast<std::size_t>(row)];

        // Hetzelfde nummer mag meerdere keren in een setlist staan.
        target.push_back(entry);
        done.add(row);
    }

    if (!copy)
        for (int i = done.size() - 1; i >= 0; --i)
            source.erase(source.begin() + done[i]);

    if (done.isEmpty())
        return;

    refreshSongModel();
    setlistBox.repaint();
    savePersistentState();
}

void MainComponent::duplicateSetlist(int row)
{
    if (row < 0 || row >= static_cast<int>(setlists.size()))
        return;

    SetlistData copy = setlists[static_cast<std::size_t>(row)];

    const auto baseName = copy.name + " copy";
    auto candidate = baseName;
    int number = 2;

    const auto nameExists = [this](const juce::String& name)
    {
        return std::any_of(setlists.begin(), setlists.end(),
                           [&name](const SetlistData& setlist) { return setlist.name == name; });
    };

    while (nameExists(candidate))
        candidate = baseName + " " + juce::String(number++);

    copy.name = candidate;

    setlists.insert(setlists.begin() + row + 1, std::move(copy));
    selectedSetlist = row + 1;

    if (playingSetlist > row)
        ++playingSetlist;

    refreshSetlistModel();
    refreshSongModel();
    savePersistentState();
}

void MainComponent::clearSetlist(int row)
{
    if (row < 0 || row >= static_cast<int>(setlists.size()))
        return;

    auto& setlist = setlists[static_cast<std::size_t>(row)];

    if (setlist.songs.empty())
        return;

    const auto clear = [this, row]
    {
        if (row >= static_cast<int>(setlists.size()))
            return;

        setlists[static_cast<std::size_t>(row)].songs.clear();
        refreshSetlistModel();
        refreshSongModel();
        savePersistentState();
    };

    confirmDialog(
        "Remove all songs",
        "Remove all " + juce::String(static_cast<int>(setlist.songs.size())) + " songs from \"" + setlist.name
            + "\"?\nThe MIDI files themselves are not deleted.",
        "Remove all",
        clear);
}

void MainComponent::showSetlistMenu(int row)
{
    if (row < 0 || row >= static_cast<int>(setlists.size()))
        return;

    setlistBox.selectRow(row);

    const bool hasSongs = !setlists[static_cast<std::size_t>(row)].songs.empty();

    juce::PopupMenu menu;
    menu.setLookAndFeel(&merikLookAndFeel);
    menu.addItem(1, "New setlist");
    menu.addItem(2, "Duplicate setlist");
    menu.addItem(3, "Rename...");
    menu.addSeparator();
    menu.addItem(6, "Remove all songs", hasSongs);
    menu.addItem(7, "Delete setlist", setlists.size() > 1);

    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [this, row](int result)
        {
            if (result == 0 || row >= static_cast<int>(setlists.size()))
                return;

            switch (result)
            {
                case 1: addSetlist(); break;
                case 2: duplicateSetlist(row); break;

                case 3:
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
                    break;

                case 6: clearSetlist(row); break;
                case 7: removeSetlistAt(row); break;
                default: break;
            }
        });
}

// Regelafbreking voor lange paden in het menu, op '/' waar dat kan.
static juce::StringArray wrapPath(const juce::String& path, int maxChars)
{
    juce::StringArray lines;
    auto rest = path;

    while (rest.length() > maxChars)
    {
        int cut = rest.substring(0, maxChars).lastIndexOfChar('/');

        if (cut < maxChars / 3)
            cut = maxChars;
        else
            ++cut;

        lines.add(rest.substring(0, cut));
        rest = rest.substring(cut);
    }

    lines.add(rest);
    return lines;
}

void MainComponent::showSongMenu(int row)
{
    if (songModel == nullptr || songModel->getEntry(row) == nullptr)
        return;

    // Klik op een nummer buiten de selectie: alleen dat nummer selecteren.
    if (!songBox.getSelectedRows().contains(row))
        songBox.selectRow(row);

    const auto rows = selectedSongRows();

    if (rows.isEmpty())
        return;

    std::vector<SongEntry> entries;

    for (const int selectedRow : rows)
        if (const auto* found = songModel->getEntry(selectedRow))
            entries.push_back(*found);

    const bool single = entries.size() == 1;
    const auto& first = entries.front();

    int commonTranspose = first.transpose;
    bool anyRenamed = false;

    for (const auto& entry : entries)
    {
        if (entry.transpose != commonTranspose)
            commonTranspose = -1000;

        anyRenamed = anyRenamed || entry.displayName.isNotEmpty();
    }

    juce::PopupMenu transposeMenu;

    // -6..+6 dekt alle toonsoorten (een octaaf verder is dezelfde toonsoort).
    for (int semitones = 6; semitones >= -6; --semitones)
    {
        const auto label = semitones == 0
            ? juce::String("0  (original key)")
            : (semitones > 0 ? "+" : "") + juce::String(semitones);

        transposeMenu.addItem(100 + semitones + 6, label, true, semitones == commonTranspose);
    }

    juce::PopupMenu copyMenu, moveMenu;

    for (int i = 0; i < static_cast<int>(setlists.size()); ++i)
    {
        if (i == selectedSetlist)
            continue;

        copyMenu.addItem(200 + i, setlists[static_cast<std::size_t>(i)].name);
        moveMenu.addItem(300 + i, setlists[static_cast<std::size_t>(i)].name);
    }

    juce::PopupMenu menu;
    menu.setLookAndFeel(&merikLookAndFeel);
    const bool allowTranspose = std::any_of(entries.begin(), entries.end(), [](const SongEntry& entry)
    {
        return !MerikPackage::isPackage(entry.file);
    });

    menu.addSubMenu("Transpose", transposeMenu, allowTranspose);
    menu.addItem(1, "Rename...", single);
    menu.addItem(2, "Restore file name", anyRenamed);
    menu.addSeparator();
    menu.addSubMenu("Copy to setlist", copyMenu, setlists.size() > 1);
    menu.addSubMenu("Move to setlist", moveMenu, setlists.size() > 1);
    menu.addSeparator();
    menu.addItem(4, "Show in Finder",
                 first.file.exists() || first.file.getParentDirectory().isDirectory());
    menu.addSeparator();
    menu.addItem(3, single ? juce::String("Delete") : "Delete " + juce::String(static_cast<int>(entries.size())) + " songs");

    // Informatie onderin, onder een streep; lange regels worden afgebroken.
    menu.addSeparator();

    if (single)
    {
        if (first.missing)
        {
            menu.addItem(-1, "File not found", false);
        }
        else if (first.lengthSeconds >= 0.0)
        {
            menu.addItem(-1, "Length " + formatLength(first.lengthSeconds), false);
            menu.addItem(-1, juce::String("Contains: ") + (first.hasMidi == 1 ? "MIDI " : "")
                                 + (first.hasAudio == 1 ? "audio stems" : ""), false);
            menu.addItem(-1, juce::String("Lyrics: ") + (first.hasLyrics == 1 ? "yes" : "no")
                                 + "   Chords: " + (first.hasChords == 1 ? "yes" : "no"), false);
        }

        for (const auto& line : wrapPath(first.file.getFullPathName(), 40))
            menu.addItem(-1, line, false);
    }
    else
    {
        double total = 0.0;

        for (const auto& entry : entries)
            if (entry.lengthSeconds >= 0.0)
                total += entry.lengthSeconds;

        menu.addItem(-1, juce::String(static_cast<int>(entries.size())) + " songs selected", false);
        menu.addItem(-1, "Total length " + formatLength(total), false);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [this, rows, entries](int result)
        {
            if (result <= 0 || selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
                return;

            auto& songs = setlists[static_cast<std::size_t>(selectedSetlist)].songs;

            // De selectie moet nog kloppen met wat het menu toonde.
            for (int i = 0; i < rows.size(); ++i)
                if (rows[i] >= static_cast<int>(songs.size())
                    || songs[static_cast<std::size_t>(rows[i])].file != entries[static_cast<std::size_t>(i)].file)
                    return;

            if (result >= 100 && result <= 112)
            {
                const int transposeValue = result - 100 - 6;

                for (const int selectedRow : rows)
                {
                    auto& song = songs[static_cast<std::size_t>(selectedRow)];
                    song.transpose = transposeValue;

                    // Staat dit nummer nu aan, dan klinkt de nieuwe transpose meteen.
                    if (currentSongFromSetlist && currentMidiFile == song.file)
                        setTranspose(transposeValue);
                }
            }
            else if (result >= 200 && result < 300)
            {
                moveOrCopySongs(rows, result - 200, true);
                return;
            }
            else if (result >= 300 && result < 400)
            {
                moveOrCopySongs(rows, result - 300, false);
                return;
            }
            else if (result == 1)
            {
                const int row = rows[0];

                promptRename(
                    "Rename song",
                    songs[static_cast<std::size_t>(row)].shownName(false),
                    [this, row, file = entries[0].file](const juce::String& name)
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
                            webServer.setSongTitle(song.displayName.toStdString());
                        }

                        songBox.repaint();
                        savePersistentState();
                    });

                return;
            }
            else if (result == 2)
            {
                for (const int selectedRow : rows)
                {
                    auto& song = songs[static_cast<std::size_t>(selectedRow)];
                    song.displayName = {};

                    if (currentSongFromSetlist && currentMidiFile == song.file)
                    {
                        currentDisplayName = {};
                        updateSongDisplay();
                        webServer.setSongTitle({});
                    }
                }
            }
            else if (result == 3)
            {
                removeSongs(rows);
                return;
            }
            else if (result == 4)
            {
                // Bestaat het bestand niet meer, dan wordt de map geopend.
                const auto& file = songs[static_cast<std::size_t>(rows[0])].file;

                if (file.exists())
                    file.revealToUser();
                else
                    file.getParentDirectory().revealToUser();

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

void MainComponent::saveClickSettings(int route, int offsetMs)
{
    clickRoute = route;
    synthEngine.setClickOffsetMs(offsetMs);

    if (auto* properties = appProperties.getUserSettings())
    {
        properties->setValue("click.route", route);
        properties->setValue("click.offsetMs", offsetMs);
        properties->saveIfNeeded();
    }
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


// ============================================================================
// Duur, lyrics en akkoorden; ontbrekende bestanden; Next-knop
// ============================================================================

void MainComponent::scanSongInfo()
{
    if (songScanner == nullptr)
        return;

    std::vector<juce::File> pending;

    const auto collect = [&pending](const SetlistData& setlist)
    {
        for (const auto& song : setlist.songs)
        {
            if (!song.file.existsAsFile())
                continue;

            const bool changed =
                song.fileTime != song.file.getLastModificationTime().toMilliseconds();

            if ((song.lengthSeconds < 0.0 || song.hasMidi < 0 || changed)
                && std::find(pending.begin(), pending.end(), song.file) == pending.end())
            {
                pending.push_back(song.file);
            }
        }
    };

    // De getoonde setlist eerst: daar kijkt de gebruiker naar.
    if (selectedSetlist >= 0 && selectedSetlist < static_cast<int>(setlists.size()))
        collect(setlists[static_cast<std::size_t>(selectedSetlist)]);

    for (int i = 0; i < static_cast<int>(setlists.size()); ++i)
        if (i != selectedSetlist)
            collect(setlists[static_cast<std::size_t>(i)]);

    songScanner->setQueue(pending);
}

void MainComponent::applySongInfo(const juce::File& file, double length, int lyrics, int chords,
                                  int midi, int audio)
{
    const auto modified = file.getLastModificationTime().toMilliseconds();

    for (auto& setlist : setlists)
    {
        for (auto& song : setlist.songs)
        {
            if (song.file != file)
                continue;

            song.lengthSeconds = length;
            song.hasLyrics = lyrics;
            song.hasChords = chords;
            song.hasMidi = midi;
            song.hasAudio = audio;
            song.fileTime = modified;
        }
    }

    setlistBox.repaint();
    songBox.repaint();

    // Niet bij elk resultaat de hele instellingenlijst wegschrijven (bij
    // honderden nummers is dat traag): de timer doet dat gebundeld.
    songInfoDirty = true;
}

void MainComponent::updateMissingFlags()
{
    for (auto& setlist : setlists)
        for (auto& song : setlist.songs)
            song.missing = !song.file.existsAsFile();
}

bool MainComponent::hasNextSong() const
{
    if (selectedSetlist < 0 || selectedSetlist >= static_cast<int>(setlists.size()))
        return false;

    const auto& songs = setlists[static_cast<std::size_t>(selectedSetlist)].songs;

    int index = currentRowInPlayingSetlist();

    if (index < 0)
        index = songBox.getSelectedRow();

    return index + 1 >= 0 && index + 1 < static_cast<int>(songs.size());
}


// ============================================================================
// Nieuwe versie (GitHub)
// ============================================================================

void MainComponent::checkForUpdates(bool userInitiated)
{
    const auto current = juce::JUCEApplication::getInstance()->getApplicationVersion();

    UpdateChecker::check(
        current,
        [safe = juce::Component::SafePointer<MainComponent>(this), current, userInitiated]
        (UpdateChecker::Result result)
        {
            if (safe != nullptr)
                safe->handleUpdateResult(result, current, userInitiated);
        });
}

void MainComponent::handleUpdateResult(const UpdateChecker::Result& result,
                                       const juce::String& currentVersion,
                                       bool userInitiated)
{
    if (!result.ok)
    {
        if (userInitiated)
            confirmDialog("Check for updates",
                          "Could not check for updates.\nCheck your internet connection and try again.",
                          "OK", nullptr);
        return;
    }

    if (!result.updateAvailable)
    {
        if (userInitiated)
            confirmDialog("Check for updates",
                          "You have the latest version (" + currentVersion + ").",
                          "OK", nullptr);
        return;
    }

    auto* properties = appProperties.getUserSettings();

    // Een overgeslagen versie krijgt geen pil meer (tenzij je zelf controleert).
    if (!userInitiated && properties != nullptr && properties->getValue("update.skipped") == result.latestVersion)
        return;

    pendingUpdateUrl = result.url;
    pendingUpdateVersion = result.latestVersion;
    pendingUpdateCurrent = currentVersion;

    updatePill.setVisible(true);
    resized();

    // Zelf op "Check for updates" gedrukt: dan meteen het venster. Bij de
    // controle bij het opstarten blijft het bij de pil.
    if (userInitiated)
        showUpdateDialog();
}

void MainComponent::showUpdateDialog()
{
    if (pendingUpdateVersion.isEmpty())
        return;

    const auto url = pendingUpdateUrl;
    const auto version = pendingUpdateVersion;
    const auto currentVersion = pendingUpdateCurrent;

    showStyledDialog(
        this, "Update available",
        std::make_unique<StyledDialog>(
            "Merik " + version + " is available.\nYou have " + currentVersion + ".",
            nullptr, "Download", true,
            [url](const juce::String&) { juce::URL(url).launchInDefaultBrowser(); },
            "Later",
            "Skip this version",
            [this, version]
            {
                if (auto* settings = appProperties.getUserSettings())
                {
                    settings->setValue("update.skipped", version);
                    settings->saveIfNeeded();
                }

                // Overgeslagen: de pil verdwijnt.
                updatePill.setVisible(false);
                pendingUpdateVersion = {};
            }));
}
