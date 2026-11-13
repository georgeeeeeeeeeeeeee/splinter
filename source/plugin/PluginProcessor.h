#pragma once
// The plug-in shell around dsp::Engine: parameters, host playhead -> clock, state. MIDI input is still
// only logged to the editor (lock-free FIFO, with the event's sample offset in the block) until Stage 5.

#include "dsp/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace fault
{
struct MidiLogEvent
{
    enum class Kind : uint8_t { noteOn, noteOff, cc, pitchBend, other } kind = Kind::other;
    int channel = 0, data1 = 0, data2 = 0, sampleOffset = 0, blockSize = 0;
};

class FaultProcessor final : public juce::AudioProcessor
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

private:
    dsp::EngineParams readParams() const noexcept;
    juce::AudioProcessorValueTreeState apvts;
    struct Raw { std::atomic<float> *time, *repeats, *mix, *windowing, *width, *freeze, *reset, *clock, *freezeMode; } raw {};
    dsp::Engine engine_;
    bool resetHeld = false;
    static constexpr int kFifo = 512;
    juce::AbstractFifo fifo { kFifo };
    std::array<MidiLogEvent, kFifo> fifoData {};
};
} // namespace fault
