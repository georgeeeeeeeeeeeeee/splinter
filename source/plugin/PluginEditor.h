#pragma once
// Splinter's editor (Stage 6): fixed-ratio and resizable, laid out at 900 x 560 and scaled. The buffer view
// sits in the centre between the time knobs (Time, Repeats, Mix) and the damage knobs (Bend, Break,
// Corrupt); the transport row is underneath. The mod and settings drawers slide over the centre, leaving
// the knobs free as drop targets. Reads the engine only through atomics, at 60 fps.

#include "plugin/PluginProcessor.h"
#include "ui/BufferView.h"
#include "ui/Knob.h"
#include "ui/ModDrawer.h"
#include "ui/SettingsDrawer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace fault
{
class FaultEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 900, kBaseH = 600;
    explicit FaultEditor (FaultProcessor&);
    ~FaultEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (ui::colours::bg); }

    // For the offline snapshot tool: open a drawer without clicking.
    void showDrawer (int which);   // 0 none, 1 mod, 2 settings
    void refreshNow() { timerCallback(); }

private:
    class Content final : public juce::Component, public juce::DragAndDropContainer
    {
    public:
        void paint (juce::Graphics&) override;
    };
    void timerCallback() override;
    void layout();
    void refreshPresetBox();
    void savePreset();
    juce::String timeText() const, bendText() const, breakText() const, corruptText() const, clockText() const;
    float param01 (const char* id) const;

    FaultProcessor& proc;
    ui::Look look;
    Content content;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<ui::Knob> time, repeats, mix, bend, brk, corrupt;
    ui::BufferView view;
    ui::ParamButton bendOn, breakOn, freeze;
    std::unique_ptr<ui::ParamCombo> corruptType;
    juce::TextButton macroBtn { "Macro" }, microBtn { "Micro" }, intBtn { "Int" }, hostBtn { "Host" }, noteBtn { "Note" }, resetBtn { "Reset" };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, modBtn { "Mod" }, setBtn { "Settings" };
    juce::ComboBox presetBox;
    // Break this bar: the button (momentary, like Reset), its length, amount, and the auto mode.
    juce::TextButton fillBtn { "Break this bar" };
    std::unique_ptr<ui::ParamCombo> fillLength, fillEvery;
    std::unique_ptr<ui::ParamSlider> fillAmount, fillChance;
    ui::ModDrawer modDrawer;
    ui::SettingsDrawer settingsDrawer;
    int presetCount = -1;
    juce::String shownPreset;
    std::unique_ptr<juce::AlertWindow> saveWindow;
};
} // namespace fault
