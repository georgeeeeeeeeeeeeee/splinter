#pragma once
// The buffer view (brief §5): the active window's waveform (left channel above, right below) with its
// slice divisions, each voice's playhead (teal forward, amber reverse), silenced spans hatched, a ghost
// of the older audio that was in view when Time shrinks, and a flash on every tick and slice jump.
// Reads only the engine's atomics and its lock-free overview.

#include "dsp/Engine.h"
#include "ui/Theme.h"

namespace fault::ui
{
class BufferView final : public juce::Component
{
public:
    explicit BufferView (const dsp::Engine& e) : engine (e) {}

    // From the editor's timer (~60 Hz).
    void tick();
    void paint (juce::Graphics&) override;

    std::function<juce::String()> clockText;   // "x2", "0.50 s" ...

private:
    void drawLane (juce::Graphics&, juce::Rectangle<float> lane, int ch);

    const dsp::Engine& engine;
    double wStart = 0.0, wLen = 0.0, viewLen = 0.0, written = 0.0;
    std::array<double, 2> head {};
    std::array<float, 2> speed { 1.0f, 1.0f }, duty {};
    std::array<int, 2> pieces { 1, 1 }, slice {};
    std::array<uint32_t, 2> jumps {};
    std::array<float, 2> jumpFlash {};
    uint64_t ticks = 0;
    uint32_t clips = 0;
    float clipFlash = 0.0f;   // the safety clipper acted (held ~0.5 s)
    float tickFlash = 0.0f;
    bool frozen = false;
    double lastMs = 0.0;
};
} // namespace fault::ui
