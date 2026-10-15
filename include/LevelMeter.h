#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

// Dunne niveaubalk: lineaire piek (0..1) wordt in dB getoond (-48..0), met
// snelle aanzet en rustige afloop.
class LevelMeter final : public juce::Component
{
public:
    LevelMeter() { setInterceptsMouseClicks(false, false); }

    // Geeft true als de balk veranderd is (de aanroeper hoeft dan niets te doen).
    void setPeak(float peak)
    {
        const float dbValue = peak > 0.00001f ? 20.0f * std::log10(peak) : -100.0f;
        const float wanted = std::clamp((dbValue + 48.0f) / 48.0f, 0.0f, 1.0f);

        shown = wanted > shown ? wanted : std::max(wanted, shown * 0.82f - 0.01f);

        if (shown < 0.005f)
            shown = 0.0f;

        if (std::abs(shown - painted) > 0.004f)
        {
            painted = shown;
            repaint();
        }
    }

    [[nodiscard]] float level() const noexcept { return shown; }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();

        g.setColour(juce::Colour::fromRGB(30, 32, 37));
        g.fillRoundedRectangle(area, area.getHeight() * 0.5f);

        const float width = area.getWidth() * painted;

        if (width < 1.0f)
            return;

        const auto fill = area.withWidth(width);
        juce::ColourGradient gradient(juce::Colour::fromRGB(46, 204, 113), area.getX(), 0.0f,
                                      juce::Colour::fromRGB(255, 85, 85), area.getRight(), 0.0f, false);
        gradient.addColour(0.70, juce::Colour::fromRGB(46, 204, 113));
        gradient.addColour(0.85, juce::Colour::fromRGB(245, 166, 35));

        g.setGradientFill(gradient);
        g.fillRoundedRectangle(fill, area.getHeight() * 0.5f);
    }

private:
    float shown = 0.0f;
    float painted = -1.0f;
};
