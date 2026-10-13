#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Greep tussen twee kolommen om de ruimte te verdelen. Meldt tijdens het
// slepen de x-positie van de muis in de coördinaten van de parent.
class ListSplitter final : public juce::Component
{
public:
    std::function<void(int xInParent)> onDragTo;

    ListSplitter()
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const float alpha = hover || dragging ? 0.95f : 0.45f;

        g.setColour(juce::Colour::fromRGB(158, 164, 178).withAlpha(alpha));

        // Drie puntjes onder elkaar, in het midden.
        const float x = static_cast<float>(getWidth()) * 0.5f - 1.5f;
        const float y = static_cast<float>(getHeight()) * 0.5f - 9.0f;

        for (int i = 0; i < 3; ++i)
            g.fillEllipse(x, y + static_cast<float>(i) * 7.0f, 3.0f, 3.0f);
    }

    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override { dragging = true; repaint(); }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; repaint(); }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (onDragTo != nullptr && getParentComponent() != nullptr)
            onDragTo(event.getEventRelativeTo(getParentComponent()).x);
    }

private:
    bool hover = false;
    bool dragging = false;
};
