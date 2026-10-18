#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Kleine knop met een getekend icoon (geen lettertype nodig).
class IconButton final : public juce::TextButton
{
public:
    enum class Icon { playFile, openFile, search };

    explicit IconButton(Icon iconToUse) : icon(iconToUse) {}

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        getLookAndFeel().drawButtonBackground(g, *this, findColour(buttonColourId), highlighted, down);

        const auto area = getLocalBounds().toFloat().reduced(0.0f, 0.0f);
        const auto centre = area.getCentre();

        g.setColour(findColour(textColourOffId));

        if (icon == Icon::playFile)
        {
            // Een afspeeldriehoek.
            juce::Path triangle;
            triangle.addTriangle(centre.x - 4.0f, centre.y - 5.5f,
                                 centre.x - 4.0f, centre.y + 5.5f,
                                 centre.x + 6.0f, centre.y);
            g.fillPath(triangle);
        }
        else if (icon == Icon::search)
        {
            // Een vergrootglas.
            g.drawEllipse(centre.x - 6.0f, centre.y - 6.5f, 10.5f, 10.5f, 1.8f);
            g.drawLine(centre.x + 2.0f, centre.y + 2.2f, centre.x + 6.5f, centre.y + 6.8f, 2.2f);
        }
        else if (icon == Icon::openFile)
        {
            // Een mapje: "open een bestand".
            juce::Path folder;
            const float x = centre.x - 7.5f;
            const float y = centre.y - 5.0f;

            folder.addRoundedRectangle(x, y, 6.5f, 3.0f, 1.0f);            // het tabje
            folder.addRoundedRectangle(x, y + 2.0f, 15.0f, 8.5f, 1.5f);   // de map
            g.fillPath(folder);
        }
    }

private:
    Icon icon;
};
