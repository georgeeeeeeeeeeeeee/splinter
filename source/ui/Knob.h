#pragma once
// A parameter knob: rotary arc in the function's colour, the name and the value below, and an outer
// modulation ring from the knob's own position to where modulation has taken it (read from the engine's
// atomics). It's also a drop target for source chips from the mod drawer: dropping one assigns a route.

#include "ui/Spotlight.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace fault::ui
{
inline constexpr float kRotaryStart = juce::MathConstants<float>::pi * 1.22f, kRotaryEnd = juce::MathConstants<float>::pi * 2.78f;

class KnobLook final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float size = (float) std::min (w, h), r = size * 0.5f - 7.0f;
        const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
        const float ang = a0 + pos * (a1 - a0);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
        g.setColour (colours::line);
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (from, ang), std::max (from, ang), true);
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The body: a dark disc with a pointer.
        const float br = r - 7.0f;
        g.setColour (colours::raised);
        g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
        g.setColour (colours::line.brighter (0.1f));
        g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);
        const juce::Point<float> tip (c.x + std::sin (ang) * (br - 3.0f), c.y - std::cos (ang) * (br - 3.0f));
        const juce::Point<float> base (c.x + std::sin (ang) * br * 0.25f, c.y - std::cos (ang) * br * 0.25f);
        g.setColour (colours::text);
        g.drawLine ({ base, tip }, 2.2f);
    }
};

class Knob final : public juce::Component, public juce::DragAndDropTarget
{
public:
    Knob (juce::RangedAudioParameter& p, juce::String name, juce::Colour accent, int dest = 0, bool bipolar = false)
        : param (p), title (std::move (name)), colour (accent), destination (dest),
          attachment (p, slider)
    {
        lvrs::Spotlight::tag (*this, p.getParameterID());
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (kRotaryStart, kRotaryEnd, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setLookAndFeel (&look);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setDoubleClickReturnValue (true, (double) p.convertFrom0to1 (p.getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
    }
    ~Knob() override { slider.setLookAndFeel (nullptr); }

    std::function<juce::String()> valueText;              // default: the parameter's own text
    std::function<float()> modulated;                      // 0..1 position after modulation (nullptr = none)
    std::function<void (int source, int dest)> onAssign;   // a chip was dropped here

    void refresh()
    {
        const float m = modulated ? modulated() : -1.0f;
        const auto t = text();
        if (std::abs (m - shownMod) > 0.002f || t != shownText)
        {
            shownMod = m;
            shownText = t;
            repaint();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (30);
        slider.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        auto bottom = r.removeFromBottom (30);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title, bottom.removeFromTop (15), juce::Justification::centred);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (text(), bottom, juce::Justification::centred);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const auto b = slider.getBounds().toFloat();
        const auto c = b.getCentre();
        const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - 1.5f;
        const float own = (float) param.getValue();
        if (modulated && shownMod >= 0.0f && std::abs (shownMod - own) > 0.004f)
        {
            const float a = kRotaryStart + own * (kRotaryEnd - kRotaryStart), m = kRotaryStart + shownMod * (kRotaryEnd - kRotaryStart);
            juce::Path ring;
            ring.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (a, m), std::max (a, m), true);
            g.setColour (colours::mod);
            g.strokePath (ring, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (c.x + std::sin (m) * r - 3.0f, c.y - std::cos (m) * r - 3.0f, 6.0f, 6.0f);
        }
        if (dropHover)
        {
            g.setColour (colours::mod.withAlpha (0.18f));
            g.fillEllipse (b.reduced (4.0f));
            g.setColour (colours::mod);
            g.drawEllipse (b.reduced (4.0f), 1.5f);
        }
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return destination > 0 && d.description.toString().startsWith ("src:");
    }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHover = false;
        repaint();
        if (onAssign)
            onAssign (d.description.toString().fromFirstOccurrenceOf ("src:", false, false).getIntValue(), destination);
    }

    juce::Slider slider;

private:
    juce::String text() const { return valueText ? valueText() : param.getCurrentValueAsText(); }

    juce::RangedAudioParameter& param;
    juce::String title;
    juce::Colour colour;
    int destination;
    KnobLook look;
    juce::SliderParameterAttachment attachment;
    float shownMod = -1.0f;
    juce::String shownText;
    bool dropHover = false;
};
} // namespace fault::ui
