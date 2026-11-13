#include "plugin/PluginEditor.h"

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
        addAndMakeVisible (*row.label);
        addAndMakeVisible (*row.control);
        rows.push_back (std::move (row));
    }
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
    setSize (620, 840);
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
    g.drawText ("Stage 4 debug UI  v" JucePlugin_VersionString, 200, 10, 344, 28, juce::Justification::centredRight);
}

void FaultEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (34);
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (26);
        row.label->setBounds (line.removeFromLeft (120));
        row.control->setBounds (line.reduced (0, 2));
    }
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
