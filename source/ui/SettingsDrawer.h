#pragma once
// The settings drawer (the hardware's shift-menu equivalent, brief §5): windowing, width, stereo mode,
// seed, note to speed, the attenuators, gate and freeze behaviour, Corrupt set and Corrupt-as-reset, the
// gate note mappings (learnable), and Restore defaults.

#include "plugin/PluginProcessor.h"
#include "ui/Controls.h"

namespace fault::ui
{
class SettingsDrawer final : public juce::Component
{
public:
    explicit SettingsDrawer (FaultProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick();

private:
    FaultProcessor& proc;
    std::vector<std::unique_ptr<juce::Component>> left, right;   // rows, top to bottom
    std::array<juce::TextButton, dsp::kNumGates> gateLearn;
    juce::TextButton restore { "Restore defaults" };
    juce::StringArray midiLines;   // the last few MIDI events, with their sample offsets
    juce::Rectangle<int> leftArea, rightArea, gateArea;
};
} // namespace fault::ui
