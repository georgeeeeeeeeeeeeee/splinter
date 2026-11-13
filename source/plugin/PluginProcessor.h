#pragma once
// The plug-in shell around dsp::Engine: parameters, host playhead -> clock, state. MIDI input is still
// only logged to the editor (lock-free FIFO, with the event's sample offset in the block) until Stage 5.

#include "dsp/Engine.h"
#include "plugin/Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <optional>
#include <atomic>

namespace fault
{
struct MidiLogEvent
{
    enum class Kind : uint8_t { noteOn, noteOff, cc, pitchBend, other } kind = Kind::other;
    int channel = 0, data1 = 0, data2 = 0, sampleOffset = 0, blockSize = 0;
};

class FaultProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    FaultProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }
    int popMidi (MidiLogEvent* dest, int max) noexcept;   // UI thread
    std::array<std::atomic<int>, 4> counts {};             // notes on, notes off, CCs, pitch bends
    const dsp::Engine& engine() const noexcept { return engine_; }
    // Dev builds: tuning/macro_tables.json, reloaded when saved. Message thread only.
    juce::String tablesStatus() const { return tablesStatus_; }

    // Stage 5: learnable CC lanes and gate notes (plug-in state, not parameters). The editor sets
    // learnCc / learnGate; the next CC / note-on is taken and the learn ends.
    std::array<std::atomic<int>, dsp::kNumCcLanes> ccNumbers {};
    std::array<std::atomic<int>, dsp::kNumGates> gateNotes {};
    std::atomic<int> learnCc { -1 }, learnGate { -1 };
    void restoreDefaults();   // message thread

    // Stage 6.
    PresetManager& presetManager() noexcept { return presets_; }
    // Drag-to-assign: source (dsp::ModSource) onto destination (dsp::ModDest). Reuses a route that
    // already joins the two, else the first empty slot (depth 50%). Returns the slot, or -1 if all 8 are
    // taken. Message thread.
    int assignRoute (int source, int dest);
    const dsp::MacroTables& macroTables() const noexcept { return tablesUi_; }   // message thread

private:
    void timerCallback() override;   // dev builds: watch the tables file
    dsp::EngineParams readParams() const noexcept;
    juce::File tablesFile_;
    juce::Time tablesTime_;
    std::optional<dsp::MacroTables> tablesWaiting_;
    juce::String tablesStatus_ { "Macro tables: compiled in" };
    juce::AudioProcessorValueTreeState apvts;
    struct Raw { std::atomic<float> *time, *repeats, *mix, *windowing, *width, *freeze, *reset, *clock, *freezeMode,
                       *mode, *bend, *brk, *bendOnMacro, *bendOnMicro, *breakOnMacro, *breakOnMicro, *noteToSpeed,
                       *stereo, *seed, *corrupt, *corruptType, *corruptSet,
                       *bendAtten, *breakAtten, *corruptAtten, *gateMode, *corruptAsReset, *envAttack, *envRelease, *envGain; } raw {};
    std::array<std::array<std::atomic<float>*, 5>, dsp::kNumModulators> rawMod {};   // shape, rate, sync, phase, polarity
    std::array<std::array<std::atomic<float>*, 3>, dsp::kNumRoutes> rawRoute {};     // source, dest, depth
    std::array<std::atomic<float>*, dsp::kNumCcLanes> rawCcPolarity {};

    dsp::Engine engine_;
    dsp::MacroTables tablesUi_ = dsp::MacroTables::defaults();
    PresetManager presets_ { apvts };
    bool resetHeld = false;
    static constexpr int kFifo = 512;
    juce::AbstractFifo fifo { kFifo };
    std::array<MidiLogEvent, kFifo> fifoData {};
};
} // namespace fault
