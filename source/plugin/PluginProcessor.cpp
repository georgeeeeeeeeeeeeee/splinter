#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "dsp/Params.h"

namespace fault
{
namespace
{
juce::String toJuce (std::string_view s) { return juce::String (s.data(), s.size()); }

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const params::FloatSpec& s, juce::NormalisableRange<float> range,
                                                      std::function<juce::String (float, int)> text)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { toJuce (s.id), 1 }, toJuce (s.name), range, s.def,
                                                        juce::AudioParameterFloatAttributes().withLabel (toJuce (s.unit)).withStringFromValueFunction (std::move (text)));
}

juce::StringArray names (std::initializer_list<std::string_view> list)
{
    juce::StringArray a;
    for (auto s : list) a.add (toJuce (s));
    return a;
}

juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto pct = [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + "%"; };
    l.add (makeFloat (params::time, { 0.0f, 1.0f }, [] (float v, int) {
        const double s = dsp::Clock::internalPeriodSeconds (v);   // the internal period; synced: see the step names
        return (s >= 1.0 ? juce::String (s, 2) + " s" : juce::String (s * 1000.0, 1) + " ms") + "  /  " + dsp::Clock::kRateNames[(size_t) dsp::Clock::hostStep (v)];
    }));
    // Repeats: an exponential knob over whole steps 1..32 (more travel for the low counts).
    juce::NormalisableRange<float> rep (1.0f, 32.0f,
                                        [] (float, float, float x) { return std::round (std::pow (32.0f, x)); },
                                        [] (float, float, float v) { return std::log (std::max (1.0f, v)) / std::log (32.0f); },
                                        [] (float, float, float v) { return std::clamp (std::round (v), 1.0f, 32.0f); });
    l.add (makeFloat (params::repeats, rep, [] (float v, int) { return juce::String ((int) v); }));
    l.add (makeFloat (params::mix, { 0.0f, 100.0f }, pct));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { toJuce (params::freezeId), 1 }, "Freeze", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { toJuce (params::freezeButtonModeId), 1 }, "Freeze Button",
                                                         names ({ params::kFreezeModes[0], params::kFreezeModes[1] }), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { toJuce (params::clockSourceId), 1 }, "Clock",
                                                         names ({ params::kClockSources[0], params::kClockSources[1], params::kClockSources[2] }), 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { toJuce (params::resetId), 1 }, "Reset", false));
    l.add (makeFloat (params::windowing, { 0.0f, 100.0f, 0.0f, 0.5f }, pct));
    l.add (makeFloat (params::width, { 0.0f, 100.0f }, pct));

    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { toJuce (params::modeId), 1 }, "Mode", names ({ params::kModes[0], params::kModes[1] }), 0));
    const params::FloatSpec bendSpec { params::bendId, "Bend", 0.0f, 1.0f, 0.5f, "" }, breakSpec { params::breakId, "Break", 0.0f, 1.0f, 0.0f, "" };
    l.add (makeFloat (bendSpec, { 0.0f, 1.0f }, [] (float v, int) {   // shown as Micro's speed; Macro reads it as zones
        const float oct = -3.0f + 6.0f * v;
        return (oct >= 0.0f ? "+" : "") + juce::String (oct, 2) + " oct";
    }));
    l.add (makeFloat (breakSpec, { 0.0f, 1.0f }, [] (float v, int) { return juce::String (v * 100.0f, 0) + "%"; }));
    const auto toggle = [&l] (std::string_view id, const char* name) {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { toJuce (id), 1 }, name, false));
    };
    toggle (params::bendOnMacroId, "Bend On (Macro)");
    toggle (params::bendOnMicroId, "Reverse (Micro)");
    toggle (params::breakOnMacroId, "Break On (Macro)");
    toggle (params::breakOnMicroId, "Break Silence (Micro)");
    toggle (params::noteToSpeedId, "Note to Speed");
    return l;
}
} // namespace

FaultProcessor::FaultProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FaultState", layout())
{
    const auto get = [this] (std::string_view id) { return apvts.getRawParameterValue (toJuce (id)); };
    raw = { get (params::time.id), get (params::repeats.id), get (params::mix.id), get (params::windowing.id), get (params::width.id),
            get (params::freezeId), get (params::resetId), get (params::clockSourceId), get (params::freezeButtonModeId),
            get (params::modeId), get (params::bendId), get (params::breakId), get (params::bendOnMacroId), get (params::bendOnMicroId),
            get (params::breakOnMacroId), get (params::breakOnMicroId), get (params::noteToSpeedId) };
    engine_.setParams (readParams());
    engine_.prepare (48000.0, 512);   // playable even before prepareToPlay; re-prepared there
}

dsp::EngineParams FaultProcessor::readParams() const noexcept
{
    dsp::EngineParams p;
    p.time = raw.time->load();
    p.repeats = (int) std::round (raw.repeats->load());
    p.mix = raw.mix->load() * 0.01f;
    p.windowing = raw.windowing->load() * 0.01f;
    p.width = raw.width->load() * 0.01f;
    p.freeze = raw.freeze->load() > 0.5f;
    p.freezeMode = raw.freezeMode->load() > 0.5f ? dsp::FreezeMode::momentary : dsp::FreezeMode::latching;
    const int clock = (int) raw.clock->load();
    p.clock = clock == 1 ? dsp::ClockSource::host : dsp::ClockSource::internal;   // Note clock: Stage 5
    p.mode = raw.mode->load() > 0.5f ? dsp::Mode::micro : dsp::Mode::macro;
    p.bend = raw.bend->load();
    p.brk = raw.brk->load();
    p.bendOnMacro = raw.bendOnMacro->load() > 0.5f;
    p.bendOnMicro = raw.bendOnMicro->load() > 0.5f;
    p.breakOnMacro = raw.breakOnMacro->load() > 0.5f;
    p.breakOnMicro = raw.breakOnMicro->load() > 0.5f;
    p.noteToSpeed = raw.noteToSpeed->load() > 0.5f;
    return p;
}

bool FaultProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && in.size() <= out.size() && ! in.isDisabled();
}

void FaultProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.setParams (readParams());
    engine_.prepare (sampleRate, samplesPerBlock);   // allocates the ~65 s buffer: never on the audio thread
}

void FaultProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        MidiLogEvent e;
        e.channel = m.getChannel();
        e.sampleOffset = meta.samplePosition;
        e.blockSize = n;
        if (m.isNoteOn()) { e.kind = MidiLogEvent::Kind::noteOn; e.data1 = m.getNoteNumber(); e.data2 = m.getVelocity(); ++counts[0]; }
        else if (m.isNoteOff()) { e.kind = MidiLogEvent::Kind::noteOff; e.data1 = m.getNoteNumber(); ++counts[1]; }
        else if (m.isController()) { e.kind = MidiLogEvent::Kind::cc; e.data1 = m.getControllerNumber(); e.data2 = m.getControllerValue(); ++counts[2]; }
        else if (m.isPitchWheel()) { e.kind = MidiLogEvent::Kind::pitchBend; e.data1 = m.getPitchWheelValue(); ++counts[3]; }
        if (m.isNoteOnOrOff())   // into the engine, at its sample
            engine_.queueNote ({ meta.samplePosition, m.getNoteNumber(), m.getVelocity(), m.isNoteOn() });
        else continue;
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            fifoData[(size_t) scope.startIndex1] = e;   // full: dropped (the log is only a monitor)
    }

    engine_.setParams (readParams());
    const bool reset = raw.reset->load() > 0.5f;   // Reset fires on the button's rising edge
    if (reset && ! resetHeld)
        engine_.triggerReset();
    resetHeld = reset;

    dsp::HostTransport t;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            t.valid = pos->getBpm().hasValue() && pos->getPpqPosition().hasValue();
            t.playing = pos->getIsPlaying();
            t.bpm = pos->getBpm().orFallback (120.0);
            t.ppq = pos->getPpqPosition().orFallback (0.0);
        }
    engine_.setTransport (t);

    const int ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
    for (int ch = ins; ch < outs; ++ch)   // mono in, stereo out: copy
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
    if (n == 0 || outs == 0)
        return;
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (outs > 1 ? 1 : 0);
    if (outs > 1)
        engine_.process (l, r, l, r, n);
    else
    {
        engine_.process (l, l, l, l, n);   // mono out: the engine's left
    }
}

int FaultProcessor::popMidi (MidiLogEvent* dest, int max) noexcept
{
    const auto scope = fifo.read (juce::jmin (max, fifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = fifoData[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = fifoData[(size_t) (scope.startIndex2 + i)];
    return n;
}

void FaultProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", params::kStateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void FaultProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* FaultProcessor::createEditor() { return new FaultEditor (*this); }
} // namespace fault

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new fault::FaultProcessor(); }
