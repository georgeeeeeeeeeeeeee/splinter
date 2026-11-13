#include "plugin/PluginEditor.h"

namespace fault
{
FaultEditor::FaultEditor (FaultProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
    gainAttach = std::make_unique<juce::SliderParameterAttachment> (*p.getApvts().getParameter ("gain"), gain);
    addAndMakeVisible (gain);
    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setCaretVisible (false);
    log.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    log.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171b));
    log.setText ("MIDI in (channel, data, sample offset / block size)\n");
    addAndMakeVisible (log);
    counts.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (counts);
    setSize (560, 380);
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
    g.drawText ("Stage 0 host check  v" JucePlugin_VersionString, 200, 10, 344, 28, juce::Justification::centredRight);
}

void FaultEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (34);
    gain.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
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
    counts.setText ("note-ons " + juce::String (proc.counts[0].load()) + "   note-offs " + juce::String (proc.counts[1].load())
                        + "   CCs " + juce::String (proc.counts[2].load()) + "   pitch bends " + juce::String (proc.counts[3].load()),
                    juce::dontSendNotification);
}
} // namespace fault
