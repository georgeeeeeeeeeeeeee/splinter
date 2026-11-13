#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "dsp/Params.h"

namespace fault
{
namespace
{
juce::String toJuce (std::string_view s) { return juce::String (s.data(), s.size()); }

juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto& g = params::gain;
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { toJuce (g.id), 1 }, toJuce (g.name),
                                                        juce::NormalisableRange<float> (g.min, g.max, 0.01f), g.def,
                                                        juce::AudioParameterFloatAttributes().withLabel (toJuce (g.unit))));
    return l;
}
} // namespace

FaultProcessor::FaultProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FaultState", layout())
{
    gainDb = apvts.getRawParameterValue (toJuce (params::gain.id));
}

bool FaultProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) && in.size() <= out.size() && ! in.isDisabled();
}

void FaultProcessor::prepareToPlay (double sampleRate, int)
{
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (params::dbToGain (gainDb->load()));
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
        else continue;
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            fifoData[(size_t) scope.startIndex1] = e;   // full: dropped (the log is only a monitor)
    }

    gain.setTargetValue (params::dbToGain (gainDb->load()));
    const int ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
    for (int ch = ins; ch < outs; ++ch)   // mono in, stereo out: copy
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
    for (int i = 0; i < n; ++i)
    {
        const float g = gain.getNextValue();
        for (int ch = 0; ch < outs; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
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
