#include "plugin/PluginEditor.h"
#include "dsp/Params.h"

namespace fault
{
namespace
{
juce::String corruptName (int t)
{
    const char* n[] = { "Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl" };
    return t >= 0 && t < 5 ? juce::String (n[t]) : juce::String ("off");
}
} // namespace

FaultEditor::FaultEditor (FaultProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (auto* prm : p.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        Row row;
        row.label = std::make_unique<juce::Label> (juce::String(), r->getName (24));
        row.label->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (r))
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (ch->choices, 1);
            row.c = std::make_unique<juce::ComboBoxParameterAttachment> (*ch, *box);
            row.control = std::move (box);
        }
        else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (r))
        {
            auto t = std::make_unique<juce::ToggleButton>();
            row.b = std::make_unique<juce::ButtonParameterAttachment> (*b, *t);
            row.control = std::move (t);
        }
        else
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 140, 20);
            row.s = std::make_unique<juce::SliderParameterAttachment> (*r, *s);
            row.control = std::move (s);
        }
        rowHolder.addAndMakeVisible (*row.label);
        rowHolder.addAndMakeVisible (*row.control);
        rows.push_back (std::move (row));
    }
    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
    {
        ccLearn[(size_t) c].onClick = [this, c] { proc.learnCc.store (proc.learnCc.load() == c ? -1 : c); };
        addAndMakeVisible (ccLearn[(size_t) c]);
    }
    for (int g = 0; g < dsp::kNumGates; ++g)
    {
        gateLearn[(size_t) g].onClick = [this, g] { proc.learnGate.store (proc.learnGate.load() == g ? -1 : g); };
        addAndMakeVisible (gateLearn[(size_t) g]);
    }
    restore.onClick = [this] { proc.restoreDefaults(); };
    addAndMakeVisible (restore);
    modInfo.setColour (juce::Label::textColourId, juce::Colour (0xffe0b050));
    modInfo.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (modInfo);
    tickInfo.setColour (juce::Label::textColourId, juce::Colour (0xff4fd1b5));
    addAndMakeVisible (tickInfo);
    tablesInfo.setColour (juce::Label::textColourId, juce::Colour (0xffb0b6bf));
    tablesInfo.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (tablesInfo);
    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setCaretVisible (false);
    log.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    log.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171b));
    log.setText ("MIDI in (channel, data, sample offset / block size)\n");
    addAndMakeVisible (log);
    counts.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (counts);
    setSize (680, 900);
    startTimerHz (30);
}

void FaultEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0e11));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("FAULT", 16, 10, 200, 28, juce::Justification::centredLeft);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Stage 5 debug UI  v" JucePlugin_VersionString, 200, 10, 344, 28, juce::Justification::centredRight);
}

void FaultEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (34);
    viewport.setBounds (r.removeFromTop (440));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    rowHolder.setSize (w, (int) rows.size() * 26);
    int y = 0;
    for (auto& row : rows)
    {
        juce::Rectangle<int> line (0, y, w, 26);
        y += 26;
        row.label->setBounds (line.removeFromLeft (140));
        row.control->setBounds (line.reduced (0, 2));
    }
    r.removeFromTop (6);
    auto learnRow = r.removeFromTop (26);
    for (auto& b : ccLearn) b.setBounds (learnRow.removeFromLeft (learnRow.getWidth() / 5).reduced (2, 1));
    restore.setBounds (learnRow.reduced (2, 1));
    auto gateRow = r.removeFromTop (26);
    const int gw = gateRow.getWidth() / dsp::kNumGates;
    for (auto& b : gateLearn) b.setBounds (gateRow.removeFromLeft (gw).reduced (2, 1));
    modInfo.setBounds (r.removeFromTop (36));
    tickInfo.setBounds (r.removeFromTop (24));
    tablesInfo.setBounds (r.removeFromTop (20));
    r.removeFromTop (6);
    counts.setBounds (r.removeFromBottom (22));
    log.setBounds (r);
}

void FaultEditor::timerCallback()
{
    std::array<MidiLogEvent, 128> ev;
    const int n = proc.popMidi (ev.data(), (int) ev.size());
    for (int i = 0; i < n; ++i)
    {
        const auto& e = ev[(size_t) i];
        juce::String line;
        switch (e.kind)
        {
            case MidiLogEvent::Kind::noteOn: line << "note on   " << juce::MidiMessage::getMidiNoteName (e.data1, true, true, 3) << " vel " << e.data2; break;
            case MidiLogEvent::Kind::noteOff: line << "note off  " << juce::MidiMessage::getMidiNoteName (e.data1, true, true, 3); break;
            case MidiLogEvent::Kind::cc: line << "CC " << e.data1 << " = " << e.data2; break;
            case MidiLogEvent::Kind::pitchBend: line << "bend " << e.data1; break;
            case MidiLogEvent::Kind::other: break;
        }
        line = line.paddedRight (' ', 22) + "ch " + juce::String (e.channel) + "  @" + juce::String (e.sampleOffset) + "/" + juce::String (e.blockSize) + "\n";
        log.moveCaretToEnd();
        log.insertTextAtCaret (line);
    }
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
        ccLearn[(size_t) c].setButtonText (proc.learnCc.load() == c ? "CC " + juce::String (c + 1) + ": move a CC..."
                                                                     : "CC " + juce::String (c + 1) + " = " + juce::String (proc.ccNumbers[(size_t) c].load()));
    for (int g = 0; g < dsp::kNumGates; ++g)
        gateLearn[(size_t) g].setButtonText (juce::String (params::kGateNames[g].data(), params::kGateNames[g].size()) + " "
                                             + (proc.learnGate.load() == g ? juce::String ("...") : juce::MidiMessage::getMidiNoteName (proc.gateNotes[(size_t) g].load(), true, true, 3)));
    {
        const auto& st = proc.engine().status();
        juce::String m ("modulated  time " + juce::String (st.modulated[1].load(), 2) + "  repeats " + juce::String (st.modulated[2].load(), 2)
                        + "  mix " + juce::String (st.modulated[3].load(), 2) + "  bend " + juce::String (st.modulated[4].load(), 2)
                        + "  break " + juce::String (st.modulated[5].load(), 2) + "  corrupt " + juce::String (st.modulated[6].load(), 2) + "\nsources ");
        const char* sn[] = { "", "m1", "m2", "m3", "m4", "env", "cc1", "cc2", "cc3", "cc4" };
        for (int k = 1; k < dsp::kNumModSources; ++k) m << sn[k] << " " << juce::String (st.sources[(size_t) k].load(), 2) << "  ";
        m << (st.bendOn.load() ? " BEND" : "") << (st.breakOn.load() ? " BREAK" : "");
        modInfo.setText (m, juce::dontSendNotification);
    }
    const auto& w = proc.engine().window();
    tablesInfo.setText (proc.tablesStatus(), juce::dontSendNotification);
    tickInfo.setText ("ticks " + juce::String ((juce::int64) proc.engine().ticksWritten()) + "   window " + juce::String (w.length / proc.engine().sampleRate(), 3)
                          + " s   slices " + juce::String (w.repeats) + "   speed L " + juce::String (proc.engine().status().speed.load(), 2)
                          + "x  R " + juce::String (proc.engine().status().speedR.load(), 2) + "x" + (proc.engine().buffer().frozen() ? "   FROZEN" : "")
                          + "   corrupt " + corruptName (proc.engine().status().corrupt.load()),
                      juce::dontSendNotification);
    counts.setText ("note-ons " + juce::String (proc.counts[0].load()) + "   note-offs " + juce::String (proc.counts[1].load())
                        + "   CCs " + juce::String (proc.counts[2].load()) + "   pitch bends " + juce::String (proc.counts[3].load()),
                    juce::dontSendNotification);
}
} // namespace fault
