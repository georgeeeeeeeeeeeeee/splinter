#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "dsp/Params.h"
#include "plugin/TablesJson.h"

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
    const auto pct = [] (float v, int) { return (std::abs (v) < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + "%"; };
    const auto ms = [] (float v, int) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; };
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
    l.add (makeFloat (breakSpec, { 0.0f, 1.0f }, [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }));
    const auto toggle = [&l] (std::string_view id, const char* name) {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { toJuce (id), 1 }, name, false));
    };
    toggle (params::bendOnMacroId, "Bend On (Macro)");
    toggle (params::bendOnMicroId, "Reverse (Micro)");
    toggle (params::breakOnMacroId, "Break On (Macro)");
    toggle (params::breakOnMicroId, "Break Silence (Micro)");
    toggle (params::noteToSpeedId, "Note to Speed");

    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { toJuce (params::stereoModeId), 1 }, "Stereo",
                                                         names ({ params::kStereoModes[0], params::kStereoModes[1] }), 0));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { toJuce (params::seedId), 1 }, "Seed", 0, params::kMaxSeed, 0,
                                                      juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
                                                          return v == 0 ? juce::String ("Random") : juce::String (v);
                                                      })));

    const params::FloatSpec corruptSpec { params::corruptId, "Corrupt", 0.0f, 1.0f, 0.0f, "" };
    l.add (makeFloat (corruptSpec, { 0.0f, 1.0f }, [] (float v, int) { return v <= 0.0f ? juce::String ("Off") : juce::String (juce::roundToInt (v * 100.0f)) + "%"; }));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { toJuce (params::corruptTypeId), 1 }, "Corrupt Type",
                                                         names ({ params::kCorruptTypes[0], params::kCorruptTypes[1], params::kCorruptTypes[2],
                                                                  params::kCorruptTypes[3], params::kCorruptTypes[4] }), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { toJuce (params::corruptSetId), 1 }, "Corrupt Set",
                                                         names ({ params::kCorruptSets[0], params::kCorruptSets[1] }), 0));

    // Stage 5: modulation and gates.
    const auto choice = [&l] (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def) {
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    };
    const auto pctParam = [&l, pct] (const juce::String& id, const juce::String& name, float lo, float def) {
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, 100.0f), def,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pct)));
    };
    pctParam (toJuce (params::bendAttenId), "Bend Atten", 0.0f, 100.0f);
    pctParam (toJuce (params::breakAttenId), "Break Atten", 0.0f, 100.0f);
    pctParam (toJuce (params::corruptAttenId), "Corrupt Atten", 0.0f, 100.0f);
    choice (toJuce (params::gateModeId), "Gate Mode", names ({ params::kGateModes[0], params::kGateModes[1] }), 0);
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { toJuce (params::corruptAsResetId), 1 }, "Corrupt As Reset", false));

    juce::StringArray shapes, syncs { "Free" }, polar, sources, dests;
    for (auto x : params::kModShapes) shapes.add (toJuce (x));
    for (const auto& d : dsp::kSyncDivisions) syncs.add (d.name);
    for (auto x : params::kPolarities) polar.add (toJuce (x));
    for (auto x : params::kModSources) sources.add (toJuce (x));
    for (auto x : params::kModDests) dests.add (toJuce (x));
    const int defaultShape[] = { 0, 1, 4, 5 };   // sine, triangle, S&H, smooth random
    for (int m = 1; m <= dsp::kNumModulators; ++m)
    {
        const juce::String id = "mod" + juce::String (m), nm = "Mod " + juce::String (m) + " ";
        choice (id + "Shape", nm + "Shape", shapes, defaultShape[m - 1]);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "Rate", 1 }, nm + "Rate", juce::NormalisableRange<float> (0.01f, 40.0f, 0.0f, 0.25f), 1.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
                                                                return juce::String (v, v < 1.0f ? 3 : 2) + " Hz"; })));
        choice (id + "Sync", nm + "Sync", syncs, 0);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "Phase", 1 }, nm + "Phase", juce::NormalisableRange<float> (0.0f, 360.0f), 0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("deg").withStringFromValueFunction ([] (float v, int) {
                                                                return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); })));
        choice (id + "Polarity", nm + "Polarity", polar, 1);
    }
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { toJuce (params::envAttackId), 1 }, "Env Attack", juce::NormalisableRange<float> (0.1f, 500.0f, 0.0f, 0.3f), 10.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (ms)));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { toJuce (params::envReleaseId), 1 }, "Env Release", juce::NormalisableRange<float> (1.0f, 5000.0f, 0.0f, 0.3f), 200.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (ms)));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { toJuce (params::envGainId), 1 }, "Env Gain", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
                                                            return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    for (int c = 1; c <= dsp::kNumCcLanes; ++c)
        choice ("cc" + juce::String (c) + "Polarity", "CC " + juce::String (c) + " Polarity", polar, 0);
    for (int r = 1; r <= dsp::kNumRoutes; ++r)
    {
        const juce::String id = "route" + juce::String (r), nm = "Route " + juce::String (r) + " ";
        choice (id + "Source", nm + "Source", sources, 0);
        choice (id + "Dest", nm + "Dest", dests, 0);
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "Depth", 1 }, nm + "Depth", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) {
                                                                return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + "%"; })));
    }
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
            get (params::breakOnMacroId), get (params::breakOnMicroId), get (params::noteToSpeedId),
            get (params::stereoModeId), get (params::seedId),
            get (params::corruptId), get (params::corruptTypeId), get (params::corruptSetId),
            get (params::bendAttenId), get (params::breakAttenId), get (params::corruptAttenId), get (params::gateModeId), get (params::corruptAsResetId),
            get (params::envAttackId), get (params::envReleaseId), get (params::envGainId) };
    const auto getS = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    for (int m = 0; m < dsp::kNumModulators; ++m)
    {
        const juce::String id = "mod" + juce::String (m + 1);
        rawMod[(size_t) m] = { getS (id + "Shape"), getS (id + "Rate"), getS (id + "Sync"), getS (id + "Phase"), getS (id + "Polarity") };
    }
    for (int r = 0; r < dsp::kNumRoutes; ++r)
    {
        const juce::String id = "route" + juce::String (r + 1);
        rawRoute[(size_t) r] = { getS (id + "Source"), getS (id + "Dest"), getS (id + "Depth") };
    }
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
    {
        rawCcPolarity[(size_t) c] = getS ("cc" + juce::String (c + 1) + "Polarity");
        ccNumbers[(size_t) c].store (params::kDefaultCcNumbers[c]);
    }
    for (int g = 0; g < dsp::kNumGates; ++g)
        gateNotes[(size_t) g].store (params::kDefaultGateNotes[g]);
    // Seed 0: a fresh seed each session, so the dice never repeat between sessions.
    engine_.setSessionSeed ((uint64_t) juce::Random::getSystemRandom().nextInt64() ^ (uint64_t) juce::Time::getHighResolutionTicks());
    engine_.setParams (readParams());
    engine_.prepare (48000.0, 512);   // playable even before prepareToPlay; re-prepared there
    presets_.onLoad = [this] { engine_.clearGates(); };
#ifdef FAULT_TABLES_PATH
    tablesFile_ = juce::File (FAULT_TABLES_PATH);
    tablesStatus_ = "Macro tables: watching " + tablesFile_.getFullPathName();
    timerCallback();
    startTimer (400);
#endif
}

void FaultProcessor::timerCallback()
{
    // Dev builds: reload the tables when the file's saved, and hand them to the engine (lock-free; if
    // the engine hasn't taken the previous set yet, try again next time).
    if (tablesWaiting_.has_value() && engine_.setTables (*tablesWaiting_))
        tablesWaiting_.reset();
    if (! tablesFile_.existsAsFile())
        return;
    const auto t = tablesFile_.getLastModificationTime();
    if (t == tablesTime_)
        return;
    tablesTime_ = t;
    auto tables = dsp::MacroTables::defaults();
    juce::String err;
    if (! tablesFromJson (tablesFile_.loadFileAsString(), tables, err))
    {
        tablesStatus_ = "Macro tables: " + err + " (kept the previous ones)";
        return;
    }
    if (! engine_.setTables (tables))
        tablesWaiting_ = tables;
    tablesUi_ = tables;
    tablesStatus_ = "Macro tables: loaded " + juce::Time::getCurrentTime().toString (false, true, true, true) + " from " + tablesFile_.getFileName();
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
    p.clock = clock == 1 ? dsp::ClockSource::host : clock == 2 ? dsp::ClockSource::note : dsp::ClockSource::internal;
    p.mode = raw.mode->load() > 0.5f ? dsp::Mode::micro : dsp::Mode::macro;
    p.bend = raw.bend->load();
    p.brk = raw.brk->load();
    p.bendOnMacro = raw.bendOnMacro->load() > 0.5f;
    p.bendOnMicro = raw.bendOnMicro->load() > 0.5f;
    p.breakOnMacro = raw.breakOnMacro->load() > 0.5f;
    p.breakOnMicro = raw.breakOnMicro->load() > 0.5f;
    p.noteToSpeed = raw.noteToSpeed->load() > 0.5f;
    p.stereo = raw.stereo->load() > 0.5f ? dsp::StereoMode::shared : dsp::StereoMode::unique;
    p.seed = (int) std::lround (raw.seed->load());
    p.corrupt = raw.corrupt->load();
    p.corruptType = (dsp::CorruptType) std::clamp ((int) std::lround (raw.corruptType->load()), 0, dsp::kNumCorruptTypes - 1);
    p.corruptSet = raw.corruptSet->load() > 0.5f ? dsp::CorruptSet::original3 : dsp::CorruptSet::all;
    p.mod.bendAtten = raw.bendAtten->load() * 0.01f;
    p.mod.breakAtten = raw.breakAtten->load() * 0.01f;
    p.mod.corruptAtten = raw.corruptAtten->load() * 0.01f;
    p.gateMode = raw.gateMode->load() > 0.5f ? dsp::GateMode::momentary : dsp::GateMode::latching;
    p.corruptAsReset = raw.corruptAsReset->load() > 0.5f;
    p.mod.env = { raw.envAttack->load(), raw.envRelease->load(), raw.envGain->load() };
    for (int m = 0; m < dsp::kNumModulators; ++m)
    {
        const auto& r = rawMod[(size_t) m];
        auto& q = p.mod.mods[(size_t) m];
        q.shape = (dsp::ModShape) std::clamp ((int) std::lround (r[0]->load()), 0, dsp::kNumModShapes - 1);
        q.rateHz = r[1]->load();
        q.sync = (int) std::lround (r[2]->load()) - 1;   // 0 = Free
        q.phase = r[3]->load() / 360.0f;
        q.bipolar = r[4]->load() > 0.5f;
    }
    for (int k = 0; k < dsp::kNumRoutes; ++k)
    {
        const auto& r = rawRoute[(size_t) k];
        p.mod.routes[(size_t) k] = { (dsp::ModSource) std::clamp ((int) std::lround (r[0]->load()), 0, dsp::kNumModSources - 1),
                                     (dsp::ModDest) std::clamp ((int) std::lround (r[1]->load()), 0, dsp::kNumModDests - 1), r[2]->load() * 0.01f };
    }
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
    {
        p.mod.ccBipolar[(size_t) c] = rawCcPolarity[(size_t) c]->load() > 0.5f;
        p.mod.ccNumbers[(size_t) c] = ccNumbers[(size_t) c].load();
    }
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
        // MIDI learn (from the editor): the next CC / note-on takes the lane / gate.
        if (m.isController() && learnCc.load() >= 0)
        {
            ccNumbers[(size_t) std::clamp (learnCc.load(), 0, dsp::kNumCcLanes - 1)].store (m.getControllerNumber());
            learnCc.store (-1);
        }
        if (m.isNoteOn() && learnGate.load() >= 0)
        {
            gateNotes[(size_t) std::clamp (learnGate.load(), 0, dsp::kNumGates - 1)].store (m.getNoteNumber());
            learnGate.store (-1);
        }
        if (m.isNoteOnOrOff())   // into the engine, at its sample
            engine_.queueNote ({ meta.samplePosition, m.getNoteNumber(), m.getVelocity(), m.isNoteOn() });
        else if (m.isController())
            engine_.queueCC (meta.samplePosition, m.getControllerNumber(), m.getControllerValue());
        else continue;
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            fifoData[(size_t) scope.startIndex1] = e;   // full: dropped (the log is only a monitor)
    }

    std::array<int, dsp::kNumGates> gates {};
    for (int g = 0; g < dsp::kNumGates; ++g) gates[(size_t) g] = gateNotes[(size_t) g].load (std::memory_order_relaxed);
    engine_.setGateNotes (gates);
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
    juce::StringArray cc, gates;
    for (auto& c : ccNumbers) cc.add (juce::String (c.load()));
    for (auto& g : gateNotes) gates.add (juce::String (g.load()));
    state.setProperty ("ccNumbers", cc.joinIntoString (","), nullptr);
    state.setProperty ("gateNotes", gates.joinIntoString (","), nullptr);
    state.setProperty ("preset", presets_.currentName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void FaultProcessor::setStateInformation (const void* data, int size)
{
    // Any version's state (brief Stage 7). Parameters an older state didn't have take their defaults
    // (APVTS does that); ones a newer state has that this version doesn't are ignored; a value that
    // isn't a finite number takes the default; lists missing from an older state take their defaults.
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        auto child = tree.getChild (i);
        auto* prm = apvts.getParameter (child.getProperty ("id").toString());
        if (prm == nullptr)
            continue;
        const auto text = child.getProperty ("value").toString().trim();
        const double v = text.getDoubleValue();
        const bool number = text.isNotEmpty() && text.containsOnly ("0123456789+-.eE") && std::isfinite (v);
        const float plain = number ? (float) v : prm->convertFrom0to1 (prm->getDefaultValue());
        child.setProperty ("value", prm->convertFrom0to1 (prm->convertTo0to1 (plain)), nullptr);   // clamped, snapped
    }
    const auto readList = [&tree] (const char* key, auto& into, const int* defaults) {
        juce::StringArray a;
        a.addTokens (tree.getProperty (key).toString(), ",", "");
        for (int i = 0; i < (int) into.size(); ++i)
        {
            const auto t = i < a.size() ? a[i].trim() : juce::String();
            const bool ok = t.isNotEmpty() && t.containsOnly ("0123456789");
            into[(size_t) i].store (ok ? juce::jlimit (0, 127, t.getIntValue()) : defaults[i]);
        }
    };
    readList ("ccNumbers", ccNumbers, params::kDefaultCcNumbers);
    readList ("gateNotes", gateNotes, params::kDefaultGateNotes);
    apvts.replaceState (tree);
    presets_.setCurrentName (tree.getProperty ("preset", "Init").toString());
}

int FaultProcessor::assignRoute (int source, int dest)
{
    const auto param = [this] (int slot, const char* what) { return apvts.getParameter ("route" + juce::String (slot + 1) + what); };
    const auto index = [] (juce::RangedAudioParameter* p) { return (int) std::lround (p->convertFrom0to1 (p->getValue())); };
    int slot = -1;
    for (int r = 0; r < dsp::kNumRoutes && slot < 0; ++r)
        if (index (param (r, "Source")) == source && index (param (r, "Dest")) == dest) slot = r;
    const bool fresh = slot < 0;
    for (int r = 0; r < dsp::kNumRoutes && slot < 0; ++r)
        if (index (param (r, "Source")) == 0 || index (param (r, "Dest")) == 0) slot = r;
    if (slot < 0)
        return -1;
    const auto set = [] (juce::RangedAudioParameter* p, float plain) { p->setValueNotifyingHost (p->convertTo0to1 (plain)); };
    set (param (slot, "Source"), (float) source);
    set (param (slot, "Dest"), (float) dest);
    if (fresh) set (param (slot, "Depth"), 50.0f);
    return slot;
}

void FaultProcessor::restoreDefaults()
{
    // Brief §4: windowing 2%, Bend / Break / Freeze off, Macro mode, Unique stereo, latching gates and
    // freeze, Corrupt gate back to normal, the routes cleared. Message thread.
    const auto set = [this] (const juce::String& id, float plain) {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    };
    set (toJuce (params::windowing.id), params::windowing.def);
    for (auto id : { params::bendOnMacroId, params::bendOnMicroId, params::breakOnMacroId, params::breakOnMicroId, params::freezeId })
        set (toJuce (id), 0.0f);
    set (toJuce (params::modeId), 0.0f);
    set (toJuce (params::stereoModeId), 0.0f);
    set (toJuce (params::gateModeId), 0.0f);
    set (toJuce (params::freezeButtonModeId), 0.0f);
    set (toJuce (params::corruptAsResetId), 0.0f);
    for (int r = 1; r <= dsp::kNumRoutes; ++r)
    {
        set ("route" + juce::String (r) + "Source", 0.0f);
        set ("route" + juce::String (r) + "Dest", 0.0f);
        set ("route" + juce::String (r) + "Depth", 0.0f);
    }
    engine_.clearGates();
    presets_.setCurrentName ("Init");
}

juce::AudioProcessorEditor* FaultProcessor::createEditor() { return new FaultEditor (*this); }
} // namespace fault

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new fault::FaultProcessor(); }
