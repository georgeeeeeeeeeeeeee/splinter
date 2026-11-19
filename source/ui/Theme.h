#pragma once
// Splinter's visual identity: a dark instrument-panel ground with one signal colour per function
// (buffer teal, Bend amber, Break violet, Corrupt red, modulation lime). Original; not modelled on any
// hardware panel.

#include <juce_gui_basics/juce_gui_basics.h>

namespace fault::ui
{
namespace colours
{
inline const juce::Colour bg { 0xff0e0e11 }, panel { 0xff16161b }, raised { 0xff1f1f26 }, line { 0xff2c2c36 };
inline const juce::Colour text { 0xffe8e5dd }, dim { 0xff8d8b95 }, faint { 0xff4a4954 };
inline const juce::Colour buffer { 0xff4fd1b5 }, bend { 0xffffb454 }, brk { 0xffb79cff }, corrupt { 0xffff5c6c };
inline const juce::Colour mod { 0xffc6f26b }, freeze { 0xff6aa8ff }, reverse { 0xffffb454 };
} // namespace colours

class Look final : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        using namespace colours;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, raised);
        setColour (juce::ComboBox::outlineColourId, line);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, raised.brighter (0.15f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, dim);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, raised.brighter (0.25f));
        setColour (juce::TextButton::textColourOffId, dim);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, buffer);
        setColour (juce::Slider::backgroundColourId, line);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, buffer);
        setColour (juce::TextEditor::backgroundColourId, raised);
        setColour (juce::TextEditor::outlineColourId, line);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (12.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (std::min (12.5f, (float) h * 0.55f), juce::Font::bold); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        g.setColour (on ? accent.withAlpha (0.22f) : colours::raised.brighter (over ? 0.08f : 0.0f).darker (down ? 0.2f : 0.0f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? accent : colours::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId).brighter (0.4f) : colours::dim.brighter (0.2f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.hasKeyboardFocus (false) ? colours::dim : colours::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
        g.setColour (colours::dim);
        g.fillPath (arrow);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 0, box.getWidth() - 18, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        // A thin track, the value as a bar from the low end (or from the centre for bipolar ranges).
        const float cy = (float) y + (float) h * 0.5f;
        const auto accent = s.findColour (juce::Slider::trackColourId);
        g.setColour (colours::line);
        g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? (float) x + (float) w * 0.5f : (float) x;
        g.setColour (accent);
        g.fillRoundedRectangle (std::min (from, pos), cy - 1.5f, std::abs (pos - from), 3.0f, 1.5f);
        g.setColour (colours::text);
        g.fillEllipse (pos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
        g.setColour (b.getToggleState() ? b.findColour (juce::ToggleButton::tickColourId) : colours::raised.brighter (over ? 0.1f : 0.0f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1);
    }
};

// Small caps-style section label.
inline void drawCaption (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c = colours::dim,
                         juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.12f));
    g.drawText (s.toUpperCase(), r, j, false);
}
} // namespace fault::ui
