#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// ListBox die drag-and-drop-bron en -doel tegelijk is, met visuele
// feedback: een invoeglijn tussen rijen (herschikken), een gemarkeerde
// rij (neerzetten op een andere lijst) en een "+" bij kopiëren.
class DropListBox final : public juce::ListBox,
                          public juce::DragAndDropTarget
{
public:
    struct Visual
    {
        int insertIndex = -1;     // invoeglijn vóór deze rij (-1 = geen)
        int highlightRow = -1;    // rij waarop neergezet wordt (-1 = geen)
        bool canCopy = false;     // Command-toets maakt er een kopie van
    };

    std::function<bool(const juce::String& description)> accepts;
    std::function<Visual(const juce::String& description, juce::Point<int> position)> visualFor;
    std::function<void(const juce::String& description, juce::Point<int> position, bool copy)> onDrop;

    juce::Colour accent { juce::Colour::fromRGB(24, 118, 255) };

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return accepts != nullptr && accepts(details.description.toString());
    }

    void itemDragEnter(const SourceDetails& details) override
    {
        hovering = true;
        update(details);
    }

    void itemDragMove(const SourceDetails& details) override
    {
        update(details);

        // Scroll mee als je bij de boven- of onderrand sleept.
        if (auto* viewport = getViewport())
            viewport->autoScroll(details.localPosition.x,
                                 details.localPosition.y - viewport->getY(),
                                 24, 10);
    }

    void itemDragExit(const SourceDetails&) override
    {
        clear();
    }

    void itemDropped(const SourceDetails& details) override
    {
        const bool copy = isCopyModifierDown();
        const auto description = details.description.toString();
        const auto position = details.localPosition;

        clear();

        if (onDrop != nullptr)
            onDrop(description, position, copy);
    }

    juce::MouseCursor getMouseCursor() override
    {
        // Kopiëren kan alleen naar een andere setlist. Binnen de eigen
        // songlijst (herschikken) geen "+", ook niet met Command.
        if (hovering)
            return (visual.canCopy && isCopyModifierDown())
                       ? juce::MouseCursor(juce::MouseCursor::CopyingCursor)
                       : juce::MouseCursor(juce::MouseCursor::NormalCursor);

        return juce::ListBox::getMouseCursor();
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (!hovering)
            return;

        // Dun kader: dit is een geldige plek om neer te zetten.
        g.setColour(accent.withAlpha(0.55f));
        g.drawRect(getLocalBounds(), 1);

        if (visual.insertIndex >= 0)
        {
            const int rows = getListBoxModel() != nullptr ? getListBoxModel()->getNumRows() : 0;
            int y = 0;

            if (rows > 0)
                y = visual.insertIndex < rows
                        ? getRowPosition(visual.insertIndex, true).getY()
                        : getRowPosition(rows - 1, true).getBottom();

            g.setColour(accent);
            g.fillRect(6, y - 1, getWidth() - 12, 2);
            g.fillEllipse(2.0f, static_cast<float>(y) - 3.0f, 6.0f, 6.0f);
        }

        if (visual.highlightRow >= 0)
        {
            const auto row = getRowPosition(visual.highlightRow, true).toFloat().reduced(1.0f);

            g.setColour(accent.withAlpha(0.20f));
            g.fillRoundedRectangle(row, 4.0f);
            g.setColour(accent);
            g.drawRoundedRectangle(row, 4.0f, 2.0f);

            if (visual.canCopy && isCopyModifierDown())
            {
                const auto badge = juce::Rectangle<float>(16.0f, 16.0f)
                                       .withCentre({ row.getRight() - 16.0f, row.getCentreY() });

                g.setColour(juce::Colour::fromRGB(46, 204, 113));
                g.fillEllipse(badge);
                g.setColour(juce::Colours::white);
                g.drawLine(badge.getCentreX() - 4.0f, badge.getCentreY(),
                           badge.getCentreX() + 4.0f, badge.getCentreY(), 1.8f);
                g.drawLine(badge.getCentreX(), badge.getCentreY() - 4.0f,
                           badge.getCentreX(), badge.getCentreY() + 4.0f, 1.8f);
            }
        }
    }

private:
    static bool isCopyModifierDown()
    {
        return juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown();
    }

    void update(const SourceDetails& details)
    {
        if (visualFor != nullptr)
            visual = visualFor(details.description.toString(), details.localPosition);

        repaint();
    }

    void clear()
    {
        hovering = false;
        visual = {};
        repaint();
    }

    bool hovering = false;
    Visual visual;
};
