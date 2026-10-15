#pragma once

#include "Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

// Toont de maatsoort ("4/4") en een rij ledjes die in de maat meelopen.
// De eerste tel heeft een andere kleur. De tijdstippen komen uit de maatsoort
// en het tempo van het nummer, dus tempo- en maatwisselingen lopen vanzelf mee.
class BeatIndicator final : public juce::Component,
                            private juce::Timer
{
public:
    // Geeft de speelpositie in seconden, of een negatief getal als er niet gespeeld wordt.
    std::function<double()> positionSeconds;

    BeatIndicator()
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(60);
    }

    void setSong(std::shared_ptr<const Song> newSong)
    {
        song = std::move(newSong);
        lastKey = -1;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (song == nullptr)
            return;

        const auto beat = currentBeat();

        const int count = std::clamp(beat.numerator, 1, 12);
        const float diameter = count > 8 ? 12.0f : 16.0f;
        const float gap = count > 8 ? 6.0f : 9.0f;

        // Rechts uitgelijnd: [maatsoort] [led led led led]
        const float ledsWidth = static_cast<float>(count) * diameter + static_cast<float>(count - 1) * gap;
        const float ledsStart = static_cast<float>(getWidth()) - ledsWidth;
        float x = ledsStart;
        const float cy = static_cast<float>(getHeight()) * 0.5f;

        const auto accent = juce::Colour::fromRGB(255, 176, 32);   // eerste tel
        const auto normal = juce::Colour::fromRGB(24, 118, 255);   // overige tellen

        for (int i = 0; i < count; ++i)
        {
            const auto colour = i == 0 ? accent : normal;
            const float intensity = (beat.index == i) ? beat.intensity : 0.0f;

            const juce::Rectangle<float> led(x, cy - diameter * 0.5f, diameter, diameter);

            // Donkere "lamp" met een dun randje.
            g.setColour(juce::Colour::fromRGB(38, 41, 47));
            g.fillEllipse(led);
            g.setColour(colour.withAlpha(0.35f));
            g.drawEllipse(led.reduced(0.5f), 1.0f);

            if (intensity > 0.0f)
            {
                // Zachte gloed rond de brandende led.
                g.setGradientFill(juce::ColourGradient(
                    colour.withAlpha(0.45f * intensity), led.getCentre(),
                    colour.withAlpha(0.0f), led.getCentre().translated(diameter * 1.1f, 0.0f), true));
                g.fillEllipse(led.expanded(diameter * 0.55f));

                g.setGradientFill(juce::ColourGradient(
                    colour.brighter(0.5f).withAlpha(intensity), led.getCentre().translated(-diameter * 0.18f, -diameter * 0.18f),
                    colour.withAlpha(intensity), led.getBottomRight(), true));
                g.fillEllipse(led.reduced(1.5f));
            }

            x += diameter + gap;
        }

        // Maatsoort links van de ledjes.
        g.setColour(juce::Colour::fromRGB(158, 164, 178));
        g.setFont(juce::FontOptions(20.0f));
        g.drawText(juce::String(beat.numerator) + "/" + juce::String(beat.denominator),
                   juce::Rectangle<int>(0, 0, static_cast<int>(ledsStart) - 10, getHeight()),
                   juce::Justification::centredRight);
    }

private:
    struct Beat
    {
        int numerator = 4;
        int denominator = 4;
        int index = -1;           // -1 = er wordt niet gespeeld
        float intensity = 0.0f;   // 0..1
    };

    // Tel en fase op dit moment.
    Beat currentBeat() const
    {
        Beat beat;

        if (song == nullptr || song->timeSignatures.empty())
            return beat;

        const auto& signatures = song->timeSignatures;
        beat.numerator = signatures.front().numerator;
        beat.denominator = signatures.front().denominator;

        const double seconds = positionSeconds ? positionSeconds() : -1.0;

        // Maatsoort die op het begin hoort (ook als er niet gespeeld wordt).
        if (seconds < 0.0)
            return beat;

        const auto& tempo = song->tempoMap;
        const double ppq = static_cast<double>(song->ticksPerQuarterNote);

        // Seconden -> tick via de tempokaart.
        double tick = seconds * 2.0 * ppq;   // 120 bpm als er geen tempokaart is

        if (!tempo.empty())
        {
            std::size_t i = 0;

            while (i + 1 < tempo.size() && tempo[i + 1].seconds <= seconds)
                ++i;

            tick = static_cast<double>(tempo[i].tick)
                 + (seconds - tempo[i].seconds) * tempo[i].bpm * ppq / 60.0;
        }

        // Actieve maatsoort op deze tick.
        const TimeSignaturePoint* active = &signatures.front();

        for (const auto& point : signatures)
            if (static_cast<double>(point.tick) <= tick)
                active = &point;

        beat.numerator = active->numerator;
        beat.denominator = active->denominator;

        const double beatTicks = ppq * 4.0 / static_cast<double>(active->denominator);
        const double beats = (tick - static_cast<double>(active->tick)) / beatTicks;

        if (beats < 0.0)
            return beat;

        const auto whole = static_cast<long long>(std::floor(beats));
        const double phase = beats - static_cast<double>(whole);

        beat.index = static_cast<int>(whole % active->numerator);

        // Flits aan het begin van de tel, daarna zachter maar nog zichtbaar aan.
        beat.intensity = static_cast<float>(1.0 - 0.65 * std::pow(phase, 0.7));

        return beat;
    }

    void timerCallback() override
    {
        // Alleen opnieuw tekenen als er iets zichtbaars verandert.
        const auto beat = currentBeat();
        const int key = (beat.index + 2) * 1000 + static_cast<int>(beat.intensity * 24.0f)
                        + beat.numerator * 100000 + beat.denominator * 10000000;

        if (key != lastKey)
        {
            lastKey = key;
            repaint();
        }
    }

    std::shared_ptr<const Song> song;
    int lastKey = -1;
};
