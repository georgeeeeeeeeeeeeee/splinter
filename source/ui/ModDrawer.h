#pragma once
// The mod drawer (brief §5): the four modulators with their scopes, the envelope follower, the CC lanes
// with learn buttons, and the 8 route slots. Every source has a chip to drag onto a knob.

#include "plugin/PluginProcessor.h"
#include "ui/Controls.h"

namespace fault::ui
{
class ModDrawer final : public juce::Component
{
public:
    explicit ModDrawer (FaultProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick();   // editor timer: scopes, meters, learn state, enablement

private:
    struct ModCard
    {
        std::unique_ptr<SourceChip> chip;
        Scope scope;
        std::unique_ptr<ParamCombo> shape, sync, polarity;
        std::unique_ptr<ParamSlider> rate, phase;
    };
    struct CcCard
    {
        std::unique_ptr<SourceChip> chip;
        Scope scope;
        juce::TextButton learn;
        std::unique_ptr<ParamCombo> polarity;
    };
    struct RouteRow
    {
        std::unique_ptr<ParamCombo> source, dest;
        std::unique_ptr<ParamSlider> depth;
        juce::TextButton clear { "x" };
    };

    FaultProcessor& proc;
    std::array<ModCard, dsp::kNumModulators> mods;
    std::unique_ptr<SourceChip> envChip;
    Scope envScope;
    std::unique_ptr<ParamSlider> envAttack, envRelease, envGain;
    std::array<CcCard, dsp::kNumCcLanes> ccs;
    std::array<RouteRow, dsp::kNumRoutes> routes;
    juce::Rectangle<int> modArea, srcArea, routeArea;
};
} // namespace fault::ui
