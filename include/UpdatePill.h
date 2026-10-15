#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Donkerrode pil "Update available": laat een nieuwe versie zien zonder dat er
// een venster op het scherm springt. Pas een klik opent het venster.
class UpdatePill final : public juce::Component
{
public:
    std::function<void()> onClick;

    UpdatePill()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        auto colour = juce::Colour::fromRGB(122, 20, 28);

        if (isMouseOver())
            colour = colour.brighter(0.25f);

        g.setColour(colour);
        g.fillRoundedRectangle(area, area.getHeight() * 0.5f);

        g.setColour(juce::Colour::fromRGB(255, 120, 120).withAlpha(0.45f));
        g.drawRoundedRectangle(area, area.getHeight() * 0.5f, 1.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText("Update available", getLocalBounds(), juce::Justification::centred);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked() && getLocalBounds().contains(event.getPosition()) && onClick)
            onClick();
    }
};
