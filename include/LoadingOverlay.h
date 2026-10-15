#pragma once

#include "PackageLoader.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

// Eigen voortgangsscherm bij het laden van een .merik: een kaart met de naam van
// het nummer, een rustig oplopende balk, het aantal stems en een Annuleer-knop.
// De balk beweegt altijd (vloeiend naar de echte voortgang), dus het scherm
// ziet er nooit vastgelopen uit.
class LoadingOverlay final : public juce::Component,
                             private juce::Timer
{
public:
    std::function<void()> onCancel;

    LoadingOverlay()
    {
        cancelButton.setButtonText("Cancel");
        cancelButton.onClick = [this]
        {
            if (onCancel)
                onCancel();
        };
        cancelButton.setWantsKeyboardFocus(false);
        addAndMakeVisible(cancelButton);
        setVisible(false);
    }

    void start(const juce::String& songName, PackageLoader* loaderToWatch)
    {
        title = songName;
        loader = loaderToWatch;
        shown = 0.0f;
        phase = 0.0f;
        setVisible(true);
        toFront(false);
        startTimerHz(60);
        repaint();
    }

    void stop()
    {
        stopTimer();
        loader = nullptr;
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.62f));

        const auto card = cardBounds().toFloat();
        g.setColour(juce::Colour::fromRGB(27, 29, 34));
        g.fillRoundedRectangle(card, 14.0f);
        g.setColour(juce::Colour::fromRGB(52, 55, 62));
        g.drawRoundedRectangle(card.reduced(0.5f), 14.0f, 1.0f);

        auto area = card.reduced(26.0f, 22.0f);

        g.setColour(juce::Colour::fromRGB(158, 164, 178));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("LOADING", area.removeFromTop(16.0f).toNearestInt(), juce::Justification::centredLeft);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
        g.drawText(title, area.removeFromTop(34.0f).toNearestInt(), juce::Justification::centredLeft, true);

        area.removeFromTop(10.0f);

        // De balk
        const auto bar = area.removeFromTop(12.0f);
        g.setColour(juce::Colour::fromRGB(38, 41, 47));
        g.fillRoundedRectangle(bar, 6.0f);

        const float width = std::max(12.0f, bar.getWidth() * shown);
        const auto fill = bar.withWidth(width);
        g.setColour(juce::Colour::fromRGB(24, 118, 255));
        g.fillRoundedRectangle(fill, 6.0f);

        // Een zacht glanzend vlak dat over de balk loopt: het beeld blijft in beweging.
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addRoundedRectangle(fill, 6.0f);
            g.reduceClipRegion(clip);

            const float x = fill.getX() - 80.0f + (fill.getWidth() + 160.0f) * phase;
            g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.0f), x, 0.0f,
                                                   juce::Colours::white.withAlpha(0.35f), x + 40.0f, 0.0f, false));
            g.fillRect(juce::Rectangle<float>(x, fill.getY(), 80.0f, fill.getHeight()));
        }

        area.removeFromTop(10.0f);

        g.setColour(juce::Colour::fromRGB(158, 164, 178));
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(statusText(), area.removeFromTop(20.0f).toNearestInt(), juce::Justification::centredLeft);

        g.drawText(juce::String(juce::roundToInt(shown * 100.0f)) + "%",
                   bar.withY(bar.getBottom() + 10.0f).withHeight(20.0f).toNearestInt(),
                   juce::Justification::centredRight);
    }

    void resized() override
    {
        const auto card = cardBounds();
        cancelButton.setBounds(card.getRight() - 26 - 100, card.getBottom() - 22 - 30, 100, 30);
    }

    void mouseDown(const juce::MouseEvent&) override {}   // alles eronder blijft onbereikbaar

private:
    juce::Rectangle<int> cardBounds() const
    {
        const int width = juce::jmin(460, getWidth() - 40);
        return juce::Rectangle<int>(width, 176).withCentre(getLocalBounds().getCentre());
    }

    juce::String statusText() const
    {
        if (loader == nullptr)
            return {};

        const int total = loader->progress.stemsTotal.load();
        const int done = loader->progress.stemsDone.load();

        if (total <= 0)
            return "Opening the file...";

        if (done >= total)
            return "Almost ready...";

        return "Decoding stems: " + juce::String(done) + " of " + juce::String(total) + " done";
    }

    void timerCallback() override
    {
        const float target = loader != nullptr ? loader->progress.fraction.load() : 0.0f;

        // Vloeiend naar het echte getal, nooit terug, en altijd een klein stapje
        // vooruit zolang het niet klaar is (geen "vast" beeld).
        shown = std::max(shown, shown + (target - shown) * 0.12f);

        if (target < 1.0f)
            shown = std::min(0.97f, std::max(shown, std::min(target + 0.04f, shown + 0.0006f)));

        phase += 0.012f;

        if (phase > 1.0f)
            phase -= 1.0f;

        repaint();
    }

    PackageLoader* loader = nullptr;
    juce::String title;
    float shown = 0.0f;
    float phase = 0.0f;
    juce::TextButton cancelButton;
};
