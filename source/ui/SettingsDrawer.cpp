#include "ui/SettingsDrawer.h"

#include "dsp/Params.h"

namespace fault::ui
{
namespace
{
juce::String str (std::string_view s) { return juce::String (s.data(), s.size()); }
} // namespace

SettingsDrawer::SettingsDrawer (FaultProcessor& p) : proc (p)
{
    auto& s = p.getApvts();
    const auto slider = [&s] (const char* id, const char* label, juce::Colour c = colours::buffer) {
        auto x = std::make_unique<ParamSlider> (s, id, label, c);
        x->labelWidth = 92;
        x->valueWidth = 58;
        return x;
    };
    const auto combo = [&s] (const char* id, const char* label) {
        auto x = std::make_unique<ParamCombo> (s, id, label);
        x->labelWidth = 92;
        return x;
    };
    const auto toggle = [&s] (const char* id, const char* label, juce::Colour c) {
        auto x = std::make_unique<ParamButton> (s, id, label, c);
        return x;
    };
    left.push_back (slider ("windowing", "Windowing"));
    left.push_back (slider ("width", "Width"));
    left.push_back (combo ("stereoMode", "Stereo"));
    left.push_back (slider ("seed", "Seed"));
    left.push_back (toggle ("noteToSpeed", "Note pitch sets speed", colours::bend));
    left.push_back (slider ("bendAtten", "Bend atten", colours::bend));
    left.push_back (slider ("breakAtten", "Break atten", colours::brk));
    left.push_back (slider ("corruptAtten", "Corrupt atten", colours::corrupt));
    right.push_back (combo ("gateMode", "Gates"));
    right.push_back (combo ("freezeButtonMode", "Freeze button"));
    right.push_back (combo ("corruptSet", "Corrupt set"));
    right.push_back (toggle ("corruptAsReset", "Corrupt gate resets", colours::corrupt));
    right.push_back (slider ("output", "Output"));
    right.push_back (toggle ("safetyClip", "Output clip (0 dBFS)", colours::corrupt));
    for (auto& c : left) addAndMakeVisible (*c);
    for (auto& c : right) addAndMakeVisible (*c);
    for (int g = 0; g < dsp::kNumGates; ++g)
    {
        gateLearn[(size_t) g].onClick = [this, g] { proc.learnGate.store (proc.learnGate.load() == g ? -1 : g); };
        gateLearn[(size_t) g].setTooltip ("Click, then play a note to make it the " + str (params::kGateNames[g]) + " gate");
        gateLearn[(size_t) g].setColour (juce::TextButton::buttonOnColourId, colours::mod);
        addAndMakeVisible (gateLearn[(size_t) g]);
    }
    restore.onClick = [this] { proc.restoreDefaults(); };
    restore.setTooltip ("Windowing 2%, Output 0 dB, Bend / Break / Freeze off, Macro, Unique stereo, latching gates and freeze, routes cleared");
    addAndMakeVisible (restore);
}

void SettingsDrawer::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel.withAlpha (0.98f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    drawCaption (g, "Playback", leftArea.withHeight (14).translated (0, -16));
    drawCaption (g, "Gates", rightArea.withHeight (14).translated (0, -16));
    drawCaption (g, "Output", outputArea.withHeight (14));
    drawCaption (g, "Gate notes (click, then play a note)", gateArea.withHeight (14).translated (0, -16));
    // MIDI in: what arrived, and where in its block (sample offset / block size).
    auto m = restore.getBounds().withX (restore.getRight() + 24).withRight (getWidth() - 14).withHeight (64).translated (0, -4);
    drawCaption (g, "MIDI in (sample offset / block)", m.removeFromTop (14));
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.setColour (colours::dim);
    for (const auto& l : midiLines) g.drawText (l, m.removeFromTop (14), juce::Justification::centredLeft);
}

void SettingsDrawer::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (18);
    auto top = r.removeFromTop (8 * 30);
    leftArea = top.removeFromLeft (top.getWidth() / 2 - 8);
    top.removeFromLeft (16);
    rightArea = top;
    auto la = leftArea, ra = rightArea;
    for (auto& c : left) c->setBounds (la.removeFromTop (30).reduced (0, 4));
    for (size_t i = 0; i < right.size(); ++i)
    {
        if (i == kFirstOutputRow)   // the Output trim and clip get their own heading
        {
            ra.removeFromTop (8);
            outputArea = ra.removeFromTop (16);
        }
        right[i]->setBounds (ra.removeFromTop (30).reduced (0, 4));
    }
    r.removeFromTop (26);
    gateArea = r.removeFromTop (56);
    const int gw = gateArea.getWidth() / 4;
    for (int g = 0; g < dsp::kNumGates; ++g)
        gateLearn[(size_t) g].setBounds (juce::Rectangle<int> (gateArea.getX() + (g % 4) * gw, gateArea.getY() + (g / 4) * 28, gw, 28).reduced (3, 2));
    r.removeFromTop (14);
    restore.setBounds (r.removeFromTop (28).removeFromLeft (160));
}

void SettingsDrawer::tick()
{
    std::array<MidiLogEvent, 64> ev;
    bool changed = false;
    for (int n; (n = proc.popMidi (ev.data(), (int) ev.size())) > 0;)
        for (int i = 0; i < n; ++i)
        {
            const auto& e = ev[(size_t) i];
            juce::String line;
            switch (e.kind)
            {
                case MidiLogEvent::Kind::noteOn: line << "note on  " << juce::MidiMessage::getMidiNoteName (e.data1, true, true, 3) << " vel " << e.data2; break;
                case MidiLogEvent::Kind::noteOff: line << "note off " << juce::MidiMessage::getMidiNoteName (e.data1, true, true, 3); break;
                case MidiLogEvent::Kind::cc: line << "CC " << e.data1 << " = " << e.data2; break;
                case MidiLogEvent::Kind::pitchBend:
                case MidiLogEvent::Kind::other: continue;
            }
            midiLines.add (line.paddedRight (' ', 20) + "@ " + juce::String (e.sampleOffset) + " / " + juce::String (e.blockSize));
            changed = true;
        }
    while (midiLines.size() > 3) midiLines.remove (0);
    if (changed) repaint();
    for (int g = 0; g < dsp::kNumGates; ++g)
    {
        const bool learning = proc.learnGate.load() == g;
        gateLearn[(size_t) g].setButtonText (str (params::kGateNames[g]) + "  "
                                             + (learning ? juce::String ("play a note...")
                                                       : proc.gateNotes[(size_t) g].load() < 0 ? juce::String ("none")
                                                       : juce::MidiMessage::getMidiNoteName (proc.gateNotes[(size_t) g].load(), true, true, 3)));
        gateLearn[(size_t) g].setToggleState (learning, juce::dontSendNotification);
    }
}
} // namespace fault::ui
