#pragma once
// Small parameter controls for the drawers: a labelled slider with its value, a combo, a toggle
// button, a draggable source chip and a mini scope.

#include "ui/Spotlight.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace fault::ui
{
inline juce::RangedAudioParameter& param (juce::AudioProcessorValueTreeState& s, const juce::String& id)
{
    auto* p = s.getParameter (id);
    jassert (p != nullptr);
    return *p;
}

class ParamSlider final : public juce::Component
{
public:
    ParamSlider (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::String label, juce::Colour accent = colours::buffer)
        : p (param (s, id)), name (std::move (label)), att (p, slider)
    {
        lvrs::Spotlight::tag (*this, id);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour (juce::Slider::trackColourId, accent);
        slider.setDoubleClickReturnValue (true, (double) p.convertFrom0to1 (p.getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
        addAndMakeVisible (slider);
    }
    int labelWidth = 52, valueWidth = 54;
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedLeft (labelWidth).withTrimmedRight (valueWidth)); }
    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (colours::dim);
        g.drawText (name, getLocalBounds().removeFromLeft (labelWidth), juce::Justification::centredLeft);
        g.setColour (isEnabled() ? colours::text : colours::faint);
        g.drawText (p.getCurrentValueAsText(), getLocalBounds().removeFromRight (valueWidth), juce::Justification::centredRight);
    }
    void enablementChanged() override { slider.setAlpha (isEnabled() ? 1.0f : 0.35f); repaint(); }

private:
    juce::RangedAudioParameter& p;
    juce::String name;
    juce::Slider slider;
    juce::SliderParameterAttachment att;
};

class ParamCombo final : public juce::Component
{
public:
    ParamCombo (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::String label = {})
        : name (std::move (label))
    {
        lvrs::Spotlight::tag (*this, id);
        auto& p = param (s, id);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))
            box.addItemList (c->choices, 1);
        att = std::make_unique<juce::ComboBoxParameterAttachment> (p, box);
        addAndMakeVisible (box);
    }
    int labelWidth = 0;
    void resized() override { box.setBounds (getLocalBounds().withTrimmedLeft (name.isEmpty() ? 0 : labelWidth)); }
    void paint (juce::Graphics& g) override
    {
        if (name.isEmpty()) return;
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (colours::dim);
        g.drawText (name, getLocalBounds().removeFromLeft (labelWidth), juce::Justification::centredLeft);
    }
    juce::ComboBox box;

private:
    juce::String name;
    std::unique_ptr<juce::ComboBoxParameterAttachment> att;
};

// A toggle as a lit button.
class ParamButton final : public juce::TextButton
{
public:
    ParamButton (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text, juce::Colour accent)
        : juce::TextButton (text)
    {
        setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, accent);
        attach (s, id);
    }
    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        if (id == attachedId) return;
        att.reset();
        attachedId = id;
        lvrs::Spotlight::tag (*this, id);
        att = std::make_unique<juce::ButtonParameterAttachment> (param (s, id), *this);
    }

private:
    juce::String attachedId;
    std::unique_ptr<juce::ButtonParameterAttachment> att;
};

// Drag one of these onto a knob to route that source there.
class SourceChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SourceChip (int sourceIndex, juce::String label) : source (sourceIndex), text (std::move (label))
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag onto Time, Repeats, Mix, Bend, Break or Corrupt to route " + text + " there");
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours::mod.withAlpha (0.14f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (colours::mod.withAlpha (0.8f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent&) override
    {
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this); c != nullptr && ! c->isDragAndDropActive())
            c->startDragging ("src:" + juce::String (source), this);
    }

private:
    int source;
    juce::String text;
};

// A rolling trace of a source's value (pushed by the editor's timer).
class Scope final : public juce::Component
{
public:
    void push (float v, bool bipolarRange)
    {
        hist[(size_t) pos] = v;
        pos = (pos + 1) % (int) hist.size();
        bipolar = bipolarRange;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::bg);
        g.fillRoundedRectangle (r, 3.0f);
        const float lo = bipolar ? -1.0f : 0.0f;
        const auto y = [&] (float v) { return r.getBottom() - 2.0f - (std::clamp (v, lo, 1.0f) - lo) / (1.0f - lo) * (r.getHeight() - 4.0f); };
        g.setColour (colours::line);
        g.drawHorizontalLine ((int) y (bipolar ? 0.0f : 0.5f), r.getX(), r.getRight());
        juce::Path path;
        const int n = (int) hist.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float v = hist[(size_t) ((pos + i) % n)];
            if (i == 0) path.startNewSubPath (x, y (v)); else path.lineTo (x, y (v));
        }
        g.setColour (colours::mod);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }

private:
    std::array<float, 120> hist {};
    int pos = 0;
    bool bipolar = true;
};
} // namespace fault::ui
