#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Kleine pil met een label (MIDI / AUDIO / CLICK), in dezelfde vorm en kleuren
// als de labels in de songlijst. Links uitgelijnd in het eigen vlak.
class SourcePill final : public juce::Component
{
public:
    SourcePill() { setInterceptsMouseClicks(false, false); }

    void set(const juce::String& newText, juce::Colour newColour)
    {
        if (text == newText && colour == newColour)
            return;

        text = newText;
        colour = newColour;
        repaint();
    }

    [[nodiscard]] const juce::String& getText() const noexcept { return text; }

    void paint(juce::Graphics& g) override
    {
        const auto height = 15.0f;
        const auto font = juce::FontOptions(11.0f);
        const float width = juce::GlyphArrangement::getStringWidth(juce::Font(font), text) + 14.0f;

        const juce::Rectangle<float> pill(0.0f, (static_cast<float>(getHeight()) - height) * 0.5f, width, height);

        g.setColour(colour.withAlpha(0.28f));
        g.fillRoundedRectangle(pill, height * 0.5f);

        g.setColour(juce::Colour::fromRGB(236, 238, 242));
        g.setFont(font);
        g.drawText(text, pill.toNearestInt(), juce::Justification::centred);
    }

private:
    juce::String text;
    juce::Colour colour;
};
