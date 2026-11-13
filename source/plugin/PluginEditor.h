#pragma once
// Stage 0 debug editor: the gain slider and a MIDI monitor. Replaced in Stage 6.

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace fault
{
class FaultEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FaultEditor (FaultProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    FaultProcessor& proc;
    juce::Slider gain;
    std::unique_ptr<juce::SliderParameterAttachment> gainAttach;
    juce::TextEditor log;
    juce::Label counts;
};
} // namespace fault
