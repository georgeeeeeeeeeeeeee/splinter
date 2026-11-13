#pragma once
// Debug editor (Stages 0-5): one control per parameter, the clock's state and a MIDI monitor. Replaced in Stage 6.

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
    struct Row { std::unique_ptr<juce::Label> label; std::unique_ptr<juce::Component> control; std::unique_ptr<juce::SliderParameterAttachment> s;
                 std::unique_ptr<juce::ComboBoxParameterAttachment> c; std::unique_ptr<juce::ButtonParameterAttachment> b; };
    std::vector<Row> rows;
    juce::Component rowHolder;
    juce::Viewport viewport;
    std::array<juce::TextButton, dsp::kNumCcLanes> ccLearn;
    std::array<juce::TextButton, dsp::kNumGates> gateLearn;
    juce::TextButton restore { "Restore defaults" };
    juce::Label tickInfo, tablesInfo, modInfo;
    juce::TextEditor log;
    juce::Label counts;
};
} // namespace fault
