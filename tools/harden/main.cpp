// fault-harden (Stage 7): the real FaultProcessor, offline.
//
//   fault-harden --state [--fixtures tests/state]   state round trips, old / future / damaged state, and
//                                                   the committed fixtures from earlier versions
//   fault-harden --write-fixture tests/state/v6.xml writes this version's fixture (do it once per version)
//   fault-harden --fuzz [--minutes 10] [--seed 1]   random automation, MIDI, transport, block sizes and
//                                                   sample rates; fails on NaN/Inf, a peak over +24 dBFS,
//                                                   a stuck gate or a heap allocation in processBlock
//   fault-harden --cpu [--seconds 20]               processing time against real time, per scenario

#include "dsp/Params.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <new>
#include <random>

// ---- Allocation watch: any operator new on the "audio thread" while processBlock runs is counted. ----
namespace
{
thread_local bool inAudio = false;
std::atomic<int64_t> audioAllocs { 0 };
} // namespace
void* operator new (std::size_t n)
{
    if (inAudio) audioAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
using Clock = std::chrono::steady_clock;

struct FakeHost final : juce::AudioPlayHead
{
    bool valid = true, playing = true;
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! valid) return {};
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

std::vector<juce::RangedAudioParameter*> allParams (fault::FaultProcessor& p)
{
    std::vector<juce::RangedAudioParameter*> v;
    for (auto* q : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q)) v.push_back (r);
    return v;
}

juce::MemoryBlock stateOf (fault::FaultProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb;
}

juce::MemoryBlock wrap (const juce::XmlElement& xml)
{
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (xml, mb);
    return mb;
}

// Every parameter's normalised value, the CC numbers, the gate notes and the preset name.
juce::String describe (fault::FaultProcessor& p)
{
    juce::String s;
    for (auto* r : allParams (p)) s << r->paramID << "=" << juce::String (r->getValue(), 5) << "\n";
    for (auto& c : p.ccNumbers) s << "cc " << c.load() << "\n";
    for (auto& g : p.gateNotes) s << "gate " << g.load() << "\n";
    s << "preset " << p.presetManager().currentName() << "\n";
    return s;
}

void randomise (fault::FaultProcessor& p, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* r : allParams (p)) r->setValueNotifyingHost (r->convertTo0to1 (r->convertFrom0to1 (u (rng))));   // snapped
    for (auto& c : p.ccNumbers) c.store ((int) (rng() % 128));
    for (auto& g : p.gateNotes) g.store ((int) (rng() % 128));
    p.presetManager().setCurrentName ("Fuzzed " + juce::String ((int) (rng() % 1000)));
}

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
}

// ---- --state ----------------------------------------------------------------------------------------
int runState (const juce::File& fixtures)
{
    std::mt19937 rng (7);
    std::cout << "State" << std::endl;

    // 1. Round trip: randomise everything, save, load into a fresh instance, save again: identical.
    for (int trial = 0; trial < 20; ++trial)
    {
        fault::FaultProcessor a, b;
        randomise (a, rng);
        randomise (b, rng);   // b starts somewhere else entirely
        const auto mb = stateOf (a);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        if (describe (a) != describe (b) || stateOf (b) != mb)
        {
            check (false, "round trip " + juce::String (trial));
            return 1;
        }
    }
    check (true, "20 random round trips (all 78 parameters, CC numbers, gate notes, preset name)");

    // 2. An older version's state (only some parameters): the missing ones go to their defaults, not to
    //    whatever the instance had before.
    {
        fault::FaultProcessor fresh, target;
        randomise (target, rng);
        juce::XmlElement old ("FaultState");
        old.setAttribute ("stateVersion", 2);
        for (auto id : { "time", "repeats", "mix" })
        {
            auto* e = old.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", juce::String (id) == "repeats" ? 4.0 : 0.25);
        }
        const auto mb = wrap (old);
        target.setStateInformation (mb.getData(), (int) mb.getSize());
        bool defaults = true;
        juce::String first;
        for (auto* r : allParams (target))
        {
            const auto id = r->paramID;
            if (id == "time" || id == "repeats" || id == "mix") continue;
            if (std::abs (r->getValue() - r->getDefaultValue()) > 1.0e-6f && first.isEmpty()) { defaults = false; first = id; }
        }
        const auto* time = target.getApvts().getRawParameterValue ("time");
        const auto* reps = target.getApvts().getRawParameterValue ("repeats");
        check (std::abs (time->load() - 0.25f) < 1.0e-6f && std::abs (reps->load() - 4.0f) < 1.0e-6f, "old state: the saved parameters load");
        check (defaults, "old state: parameters it didn't have go to their defaults" + (first.isEmpty() ? juce::String() : " (" + first + " didn't)"));
        bool ccDefault = true;
        for (int c = 0; c < fault::dsp::kNumCcLanes; ++c) ccDefault = ccDefault && target.ccNumbers[(size_t) c].load() == fault::params::kDefaultCcNumbers[c];
        for (int g = 0; g < fault::dsp::kNumGates; ++g) ccDefault = ccDefault && target.gateNotes[(size_t) g].load() == fault::params::kDefaultGateNotes[g];
        check (ccDefault, "old state: CC numbers and gate notes go to their defaults");
        check (target.presetManager().currentName() == "Init", "old state: preset name is Init");
        (void) fresh;
    }

    // 3. A newer version's state (unknown parameters and properties): loads what it knows, ignores the rest.
    {
        fault::FaultProcessor a, b;
        randomise (a, rng);
        auto xml = juce::AudioProcessor::getXmlFromBinary (stateOf (a).getData(), (int) stateOf (a).getSize());
        xml->setAttribute ("stateVersion", 99);
        xml->setAttribute ("somethingNew", "x");
        auto* e = xml->createNewChildElement ("PARAM");
        e->setAttribute ("id", "futureKnob");
        e->setAttribute ("value", 0.5);
        const auto mb = wrap (*xml);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (describe (a) == describe (b), "newer state: known values load, unknown ones are ignored");
    }

    // 4. Damaged state: nothing crashes, out-of-range and non-numbers are clamped or ignored.
    {
        fault::FaultProcessor p;
        const auto before = describe (p);
        juce::Random r (3);
        for (int i = 0; i < 200; ++i)
        {
            juce::MemoryBlock junk ((size_t) r.nextInt (400));
            r.fillBitsRandomly (junk.getData(), junk.getSize());
            p.setStateInformation (junk.getData(), (int) junk.getSize());
        }
        p.setStateInformation (nullptr, 0);
        auto mb = stateOf (p);
        mb.setSize (mb.getSize() / 2);   // truncated
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        juce::XmlElement other ("SomeOtherPlugin");
        other.createNewChildElement ("PARAM")->setAttribute ("id", "time");
        const auto ob = wrap (other);
        p.setStateInformation (ob.getData(), (int) ob.getSize());
        check (describe (p) == before, "random bytes, empty, truncated and foreign state change nothing");

        juce::XmlElement bad ("FaultState");
        bad.setAttribute ("stateVersion", 6);
        bad.setAttribute ("ccNumbers", "999,-5,abc");
        bad.setAttribute ("gateNotes", "1,2");
        for (auto [id, v] : { std::pair { "time", "nan" }, { "repeats", "1e9" }, { "mix", "-40" }, { "seed", "inf" }, { "corruptType", "banana" } })
        {
            auto* e = bad.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", v);
        }
        const auto bb = wrap (bad);
        p.setStateInformation (bb.getData(), (int) bb.getSize());
        bool finite = true, inRange = true;
        for (auto* q : allParams (p))
        {
            finite = finite && std::isfinite (q->getValue());
            inRange = inRange && q->getValue() >= 0.0f && q->getValue() <= 1.0f;
        }
        bool ccOk = true;
        for (auto& c : p.ccNumbers) ccOk = ccOk && c.load() >= 0 && c.load() <= 127;
        for (auto& g : p.gateNotes) ccOk = ccOk && g.load() >= 0 && g.load() <= 127;
        check (finite && inRange, "bad values (nan, inf, 1e9, -40, text) load finite and in range");
        check (std::abs (p.getApvts().getRawParameterValue ("time")->load() - fault::params::time.def) < 1.0e-6f, "a nan value loads as the default");
        check (ccOk && p.gateNotes[2].load() == fault::params::kDefaultGateNotes[2], "bad / short CC and gate lists are clamped, the rest default");
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.prepareToPlay (48000.0, 512);
        bool ok = true;
        for (int i = 0; i < 200; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 512; ++s) buf.setSample (ch, s, std::sin ((float) (i * 512 + s) * 0.05f));
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 512; ++s) ok = ok && std::isfinite (buf.getSample (ch, s));
        }
        check (ok, "and it plays finite audio afterwards");
    }

    // 5. Fixtures saved by earlier versions (tests/state/v*.xml, each with an "expect" child listing
    //    the values it must load as).
    int fixtureCount = 0;
    for (const auto& f : fixtures.findChildFiles (juce::File::findFiles, false, "v*.xml"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr) { check (false, f.getFileName() + " parses"); continue; }
        auto* expect = xml->getChildByName ("expect");
        if (expect == nullptr) { check (false, f.getFileName() + " has an <expect> block"); continue; }
        juce::XmlElement state (*xml->getChildByName ("FaultState"));
        fault::FaultProcessor p, junk;
        randomise (p, rng);
        const auto mb = wrap (state);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        juce::StringArray want, got;
        want.addLines (expect->getAllSubText().trim());
        got.addLines (describe (p).trim());
        want.trim();
        bool same = true;
        juce::String diff;
        for (const auto& line : want)
            if (! got.contains (line)) { same = false; if (diff.isEmpty()) diff = line; }
        check (same, f.getFileName() + " loads as it did when written" + (diff.isEmpty() ? juce::String() : " (expected " + diff + ")"));
        ++fixtureCount;
    }
    check (fixtureCount > 0, juce::String (fixtureCount) + " fixture(s) from " + fixtures.getFullPathName());
    std::cout << (failures == 0 ? "state: all passed" : "state: " + juce::String (failures) + " failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

int writeFixture (const juce::File& f)
{
    // A deliberately non-default state (seeded), plus what it must load as, for every later version
    // to check against.
    fault::FaultProcessor p;
    std::mt19937 rng (2026);
    randomise (p, rng);
    p.presetManager().setCurrentName ("Fixture");
    const auto mb = stateOf (p);
    auto state = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    juce::XmlElement doc ("FaultStateFixture");
    doc.setAttribute ("note", "Saved by Fault " JucePlugin_VersionString "; never edit. <expect> is what it must load as.");
    doc.addChildElement (new juce::XmlElement (*state));
    doc.createNewChildElement ("expect")->addTextElement ("\n" + describe (p));
    f.getParentDirectory().createDirectory();
    if (! doc.writeTo (f)) return 1;
    std::cout << "wrote " << f.getFullPathName() << std::endl;
    return 0;
}

// ---- --fuzz -----------------------------------------------------------------------------------------
struct Signal
{
    int kind = 0;
    float gain = 1.0f, phase = 0.0f, freq = 0.01f;
    float next (std::mt19937& rng)
    {
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        phase += freq;
        switch (kind)
        {
            case 0: return 0.0f;                                             // silence
            case 1: return gain * u (rng);                                   // noise
            case 2: return gain * std::sin (phase);                          // sine
            case 3: return gain * (std::sin (phase) > 0.0f ? 1.0f : -1.0f);  // square
            case 4: return (rng() % 4000 == 0) ? gain : 0.0f;                 // impulses
            case 5: return gain;                                             // DC
            case 6: return 1.0e-30f * u (rng);                               // denormal-sized
            default: freq = std::min (0.5f, freq * 1.00002f); return gain * std::sin (phase);   // rising sweep
        }
    }
};

bool args_verbose = false;
int runFuzz (double minutes, uint32_t seed)
{
    std::mt19937 rng (seed);
    const auto rnd = [&] (int n) { return (int) (rng() % (uint32_t) n); };
    const auto chance = [&] (double p) { return std::uniform_real_distribution<double> (0.0, 1.0) (rng) < p; };
    std::uniform_real_distribution<float> u (0.0f, 1.0f);

    fault::FaultProcessor p;
    auto params = allParams (p);
    auto* seedParam = p.getApvts().getParameter ("seed");
    auto* gateMode = p.getApvts().getParameter ("gateMode");
    FakeHost host;
    p.setPlayHead (&host);
    constexpr float kOver = 15.8489f;   // +24 dBFS
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 22050.0 };
    const int maxBlocks[] = { 16, 64, 128, 256, 512, 1024, 2048, 4096 };

    const auto start = Clock::now();
    const auto deadline = start + std::chrono::duration<double> (minutes * 60.0);
    double audioSeconds = 0.0;
    float worstPeak = 0.0f;
    int64_t nonFinite = 0, overs = 0, stuck = 0, episodes = 0, blocks = 0, notes = 0, prepares = 0;
    juce::String firstProblem;
    double worstGain = 0.0;
    juce::String worstGainWhere;
    const auto problem = [&] (const juce::String& s) {
        if (firstProblem.isEmpty()) firstProblem = s + " (seed " + juce::String (seed) + ", episode " + juce::String (episodes) + ")";
    };

    while (Clock::now() < deadline)
    {
        ++episodes;
        // A new "session": sample rate and block size, sometimes a fresh state.
        const double sr = rates[rnd (6)];
        const int maxBlock = maxBlocks[rnd (8)];
        p.prepareToPlay (sr, maxBlock);
        ++prepares;
        for (auto* r : params)
            if (chance (0.5)) r->setValueNotifyingHost (u (rng));
        if (seedParam->getValue() == 0.0f) seedParam->setValueNotifyingHost (seedParam->convertTo0to1 (1.0f));   // reproducible
        Signal sig;
        sig.kind = rnd (8);
        sig.gain = std::pow (10.0f, (-60.0f + 72.0f * u (rng)) / 20.0f);   // -60 .. +12 dBFS
        sig.freq = 0.0005f + 0.3f * u (rng) * u (rng);
        host.bpm = chance (0.1) ? (chance (0.5) ? 20.0 : 300.0) : 60.0 + 140.0 * u (rng);
        host.valid = ! chance (0.1);
        host.playing = chance (0.8);
        std::vector<std::pair<int, int64_t>> held;   // note, the block it went down in
        const double episodeSeconds = 2.0 + 28.0 * u (rng);
        const auto episodeSamples = (int64_t) (episodeSeconds * sr);
        juce::AudioBuffer<float> buf (2, maxBlock);
        juce::MidiBuffer midi;
        int ramping = -1;
        static float lastInPeak = 1.0e-6f;   // the buffer still holds the last session's audio
        float inPeak = lastInPeak, thisPeak = 1.0e-6f;
        float rampTo = 0.0f, rampStep = 0.0f;

        for (int64_t pos = 0; pos < episodeSamples;)
        {
            const int n = chance (0.02) ? 0 : (chance (0.6) ? maxBlock : 1 + rnd (maxBlock));
            // Automation: jumps (often to the ends) and ramps.
            if (chance (0.3))
                for (int k = rnd (4); k >= 0; --k)
                {
                    auto* r = params[(size_t) rnd ((int) params.size())];
                    if (r == seedParam) continue;
                    r->setValueNotifyingHost (chance (0.3) ? (chance (0.5) ? 0.0f : 1.0f) : u (rng));
                }
            if (ramping < 0 && chance (0.02))
            {
                ramping = rnd ((int) params.size());
                rampTo = u (rng);
                rampStep = (rampTo - params[(size_t) ramping]->getValue()) / (float) (10 + rnd (400));
            }
            if (ramping >= 0 && params[(size_t) ramping] != seedParam)
            {
                auto* r = params[(size_t) ramping];
                const float v = r->getValue() + rampStep;
                r->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
                if ((rampStep >= 0.0f && v >= rampTo) || (rampStep < 0.0f && v <= rampTo) || rampStep == 0.0f) ramping = -1;
            }
            // MIDI: gate notes, other notes, the CC lanes, other CCs, all-notes-off, pitch bend.
            midi.clear();
            if (n > 0)
                for (int k = chance (0.15) ? 1 + rnd (6) : 0; k > 0; --k)
                {
                    const int at = rnd (n);
                    const int kind = rnd (10);
                    if (kind < 4)
                    {
                        const int note = chance (0.7) ? p.gateNotes[(size_t) rnd (fault::dsp::kNumGates)].load() : rnd (128);
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (1 + rnd (127))), at);
                        held.push_back ({ note, blocks });
                        ++notes;
                    }
                    else if (kind < 7 && ! held.empty())
                    {
                        // Only notes from earlier blocks (a note-off before its note-on in the same
                        // block would be a different test).
                        const auto i = (size_t) rnd ((int) held.size());
                        if (held[i].second == blocks) continue;
                        midi.addEvent (juce::MidiMessage::noteOff (1, held[i].first), at);
                        held.erase (held.begin() + (long) i);
                    }
                    else if (kind < 9)
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, chance (0.7) ? p.ccNumbers[(size_t) rnd (4)].load() : rnd (120), rnd (128)), at);
                    else
                        midi.addEvent (chance (0.5) ? juce::MidiMessage::pitchWheel (1, rnd (16384)) : juce::MidiMessage::allNotesOff (1), at);
                }
            // Transport: tempo changes, loops back, stops and starts.
            if (chance (0.005)) host.playing = ! host.playing;
            if (chance (0.003)) host.ppq = std::max (0.0, host.ppq - 16.0 * u (rng));
            if (chance (0.003)) host.bpm = 40.0 + 200.0 * u (rng);

            // Sometimes a block larger than prepareToPlay said (some hosts do).
            const int len = chance (0.01) ? maxBlock * 2 : n;
            if (buf.getNumSamples() < len) buf.setSize (2, len, false, false, true);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, len);
            for (int s = 0; s < len; ++s)
            {
                const float x = sig.next (rng);
                view.setSample (0, s, x);
                view.setSample (1, s, chance (0.5) ? x : sig.next (rng));
                thisPeak = std::max ({ thisPeak, std::abs (view.getSample (0, s)), std::abs (view.getSample (1, s)) });
                inPeak = std::max (inPeak, thisPeak);
            }
            inAudio = true;
            p.processBlock (view, midi);
            inAudio = false;
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < len; ++s)
                {
                    const float v = view.getSample (ch, s);
                    if (! std::isfinite (v)) { ++nonFinite; problem ("NaN/Inf output"); continue; }
                    const float a = std::abs (v);
                    worstPeak = std::max (worstPeak, a);
                    if (a > 0.25f && a / inPeak > worstGain)
                    {
                        worstGain = a / inPeak;
                        worstGainWhere = "signal " + juce::String (sig.kind) + ", in peak " + juce::String (20.0 * std::log10 (inPeak), 1) + " dBFS; "
                                         + p.getApvts().copyState().toXmlString().removeCharacters ("\n").substring (0, 4000);
                    }
                    if (a > kOver) { ++overs; problem ("peak " + juce::String (20.0 * std::log10 (a), 1) + " dBFS"); }
                }
            if (host.playing) host.ppq += len / sr * host.bpm / 60.0;
            pos += len;
            audioSeconds += len / sr;
            ++blocks;
        }

        lastInPeak = thisPeak;
        // Release everything held; in momentary mode no gate may stay flipped.
        // (In chunks: the engine takes at most kMaxNotes events a block.)
        juce::AudioBuffer<float> tail (2, 64);
        do
        {
            midi.clear();
            for (int k = 0; k < 256 && ! held.empty(); ++k)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, held.back().first), 0);
                held.pop_back();
            }
            tail.clear();
            inAudio = true;
            p.processBlock (tail, midi);
            inAudio = false;
        } while (! held.empty());
        if (gateMode->getValue() > 0.5f && ! p.engine().gatesIdle())
        {
            ++stuck;
            problem ("a momentary gate stayed flipped after every note was released");
        }
    }
    p.setPlayHead (nullptr);
    const double wall = std::chrono::duration<double> (Clock::now() - start).count();
    std::cout << "Fuzz, seed " << seed << ": " << episodes << " sessions (" << prepares << " prepares), " << blocks << " blocks, "
              << notes << " note-ons, " << juce::String (audioSeconds / 60.0, 1) << " min of audio in " << juce::String (wall / 60.0, 1) << " min\n"
              << "  worst peak     " << juce::String (20.0 * std::log10 (std::max (1.0e-9f, worstPeak)), 1) << " dBFS (limit +24)\n"
              << "  NaN/Inf        " << nonFinite << "\n"
              << "  over +24 dBFS  " << overs << "\n"
              << "  stuck gates    " << stuck << "\n"
              << "  allocations in processBlock  " << audioAllocs.load() << std::endl;
    if (args_verbose) std::cout << "  most gain over the input peak  " << juce::String (20.0 * std::log10 (std::max (1.0e-9, worstGain)), 1) << " dB" << std::endl;
    if (args_verbose) std::cout << "    " << worstGainWhere << std::endl;
    if (firstProblem.isNotEmpty()) std::cout << "  first problem: " << firstProblem << std::endl;
    const bool ok = nonFinite == 0 && overs == 0 && stuck == 0 && audioAllocs.load() == 0;
    std::cout << (ok ? "fuzz: passed" : "fuzz: FAILED") << std::endl;
    return ok ? 0 : 1;
}


// ---- --gain: full-scale input through the Corrupt types, amounts, Width and Mix; the loudest outputs. ----
int runGain()
{
    struct Row { float peak; juce::String what; };
    std::vector<Row> rows;
    const char* corrupts[] = { "Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl" };
    const char* sigs[] = { "square 100 Hz", "noise", "sine 60 Hz", "kick-like" };
    for (int sigK = 0; sigK < 4; ++sigK)
        for (int c = -1; c < 5; ++c)
            for (int a = 0; a <= 10; ++a)
                for (float width : { 50.0f, 100.0f })
                    for (float mixV : { 50.0f, 100.0f })
                        for (int mode : { 0, 1 })
                        {
                            if (c < 0 && a > 0) continue;
                            fault::FaultProcessor p;
                            auto& st = p.getApvts();
                            const auto set = [&] (const juce::String& id, float plain) { auto* r = st.getParameter (id); r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
                            set ("seed", 3);
                            set ("width", width);
                            set ("mix", mixV);
                            set ("mode", (float) mode);
                            set ("repeats", 4);
                            if (mode == 1) { set ("bendOnMicro", 1); set ("bend", 0.75f); }
                            else { set ("bendOnMacro", 1); set ("breakOnMacro", 1); set ("bend", 0.6f); set ("break", 0.6f); }
                            if (c >= 0) { set ("corruptType", (float) c); set ("corrupt", a / 10.0f); }
                            FakeHost host;
                            p.setPlayHead (&host);
                            p.prepareToPlay (48000.0, 512);
                            juce::AudioBuffer<float> buf (2, 512);
                            juce::MidiBuffer midi;
                            juce::Random rnd (5);
                            float peak = 0.0f;
                            for (int64_t pos = 0; pos < 48000 * 6; pos += 512)
                            {
                                for (int i = 0; i < 512; ++i)
                                {
                                    const double t = (pos + i) / 48000.0;
                                    float x = 0.0f;
                                    if (sigK == 0) x = std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * t) > 0.0 ? 1.0f : -1.0f;
                                    else if (sigK == 1) x = rnd.nextFloat() * 2.0f - 1.0f;
                                    else if (sigK == 2) x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t);
                                    else { const double ph = std::fmod (t, 0.5); x = (float) (std::exp (-ph * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 200.0 * std::exp (-ph * 40.0)) * ph)); }
                                    buf.setSample (0, i, x);
                                    buf.setSample (1, i, sigK == 1 ? rnd.nextFloat() * 2.0f - 1.0f : x);
                                }
                                host.ppq = pos / 48000.0 * 2.0;
                                p.processBlock (buf, midi);
                                for (int ch = 0; ch < 2; ++ch)
                                    for (int i = 0; i < 512; ++i) peak = std::max (peak, std::abs (buf.getSample (ch, i)));
                            }
                            p.setPlayHead (nullptr);
                            rows.push_back ({ peak, juce::String (sigs[sigK]) + ", " + (c < 0 ? juce::String ("no Corrupt") : juce::String (corrupts[c]) + " " + juce::String (a * 10) + "%")
                                                        + ", width " + juce::String ((int) width) + ", mix " + juce::String ((int) mixV) + (mode ? ", Micro" : ", Macro") });
                        }
    std::sort (rows.begin(), rows.end(), [] (const Row& x, const Row& y) { return x.peak > y.peak; });
    std::cout << "Loudest of " << rows.size() << " (input peak 0 dBFS):" << std::endl;
    for (size_t i = 0; i < rows.size(); ++i)
        std::cout << "  " << juce::String (20.0 * std::log10 (rows[i].peak), 1).paddedLeft (' ', 6) << " dBFS  " << rows[i].what << std::endl;
    return 0;
}

// ---- --cpu ------------------------------------------------------------------------------------------
int runCpu (double seconds)
{
    struct Scenario { const char* name; std::vector<std::pair<const char*, float>> set; };
    const std::vector<Scenario> scenarios {
        { "default (Macro, Repeats 1)", {} },
        { "Macro, everything on", { { "repeats", 32 }, { "bendOnMacro", 1 }, { "breakOnMacro", 1 }, { "bend", 0.9f }, { "break", 0.9f }, { "time", 0.8f }, { "windowing", 100 } } },
        { "Micro, everything on", { { "mode", 1 }, { "repeats", 32 }, { "bendOnMicro", 1 }, { "breakOnMicro", 1 }, { "bend", 0.1f }, { "break", 0.6f }, { "time", 0.95f }, { "windowing", 100 } } },
    };
    const char* corrupts[] = { "Decimate", "Dropout", "Destroy", "DJ Filter", "Vinyl" };
    struct Setup { double sr; int block; };
    const Setup setups[] = { { 48000.0, 512 }, { 48000.0, 64 }, { 96000.0, 128 }, { 44100.0, 32 } };
    std::cout << "CPU: processing time as % of one core in real time (" << seconds << " s of noise each)\n";
    double worst = 0.0;
    juce::String worstName;
    for (const auto& setup : setups)
    {
        std::cout << "  " << (int) setup.sr << " Hz, " << setup.block << "-sample blocks" << std::endl;
        for (const auto& sc : scenarios)
            for (int c = -1; c < 5; ++c)
            {
                fault::FaultProcessor p;
                auto& s = p.getApvts();
                const auto set = [&] (const juce::String& id, float plain) { auto* r = s.getParameter (id); r->setValueNotifyingHost (r->convertTo0to1 (plain)); };
                for (auto& [id, v] : sc.set) set (id, v);
                set ("seed", 1);
                if (c >= 0) { set ("corruptType", (float) c); set ("corrupt", c == 3 ? 0.15f : 0.8f); }
                // Every route busy: four fast modulators and the envelope onto all six knobs.
                for (int m = 1; m <= 4; ++m) { set ("mod" + juce::String (m) + "Rate", 20.0f); set ("mod" + juce::String (m) + "Shape", (float) (m + 1)); }
                for (int r = 1; r <= 8; ++r)
                {
                    set ("route" + juce::String (r) + "Source", (float) (1 + (r - 1) % 5));
                    set ("route" + juce::String (r) + "Dest", (float) (1 + (r - 1) % 6));
                    set ("route" + juce::String (r) + "Depth", 30.0f);
                }
                FakeHost host;
                p.setPlayHead (&host);
                p.prepareToPlay (setup.sr, setup.block);
                juce::AudioBuffer<float> buf (2, setup.block);
                juce::MidiBuffer midi;
                juce::Random rnd (1);
                const auto total = (int64_t) (seconds * setup.sr);
                std::chrono::duration<double> spent {};
                double worstBlock = 0.0;
                for (int64_t pos = 0; pos < total; pos += setup.block)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < setup.block; ++i) buf.setSample (ch, i, rnd.nextFloat() * 0.5f - 0.25f);
                    host.ppq = pos / setup.sr * 2.0;
                    const auto t0 = Clock::now();
                    p.processBlock (buf, midi);
                    const auto dt = Clock::now() - t0;
                    spent += dt;
                    worstBlock = std::max (worstBlock, std::chrono::duration<double> (dt).count());
                }
                p.setPlayHead (nullptr);
                const double pct = 100.0 * spent.count() / seconds;
                const double blockPct = 100.0 * worstBlock / (setup.block / setup.sr);
                const auto name = juce::String (sc.name) + (c >= 0 ? juce::String (" + ") + corrupts[c] : juce::String());
                std::cout << "    " << name.paddedRight (' ', 42) << juce::String (pct, 2).paddedLeft (' ', 6) << " %   (slowest block "
                          << juce::String (blockPct, 1) << " % of its time)" << std::endl;
                if (pct > worst) { worst = pct; worstName = name + " at " + juce::String ((int) setup.sr) + "/" + juce::String (setup.block); }
            }
    }
    std::cout << "worst: " << juce::String (worst, 2) << " % (" << worstName << "); budget 5 %" << std::endl;
    return worst < 5.0 ? 0 : 1;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::map<juce::String, juce::String> args;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a.startsWith ("--")) args[a] = (i + 1 < argc && ! juce::String (argv[i + 1]).startsWith ("--")) ? juce::String (argv[++i]) : juce::String ("1");
    }
    const auto opt = [&] (const char* k, juce::String def = {}) { auto it = args.find (k); return it == args.end() ? def : it->second; };
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    if (args.count ("--state")) return runState (cwd.getChildFile (opt ("--fixtures", "tests/state")));
    if (args.count ("--write-fixture")) return writeFixture (cwd.getChildFile (opt ("--write-fixture")));
    args_verbose = args.count ("--verbose") > 0;
    if (args.count ("--fuzz")) return runFuzz (opt ("--minutes", "10").getDoubleValue(), (uint32_t) opt ("--seed", "1").getLargeIntValue());
    if (args.count ("--gain")) return runGain();
    if (args.count ("--cpu")) return runCpu (opt ("--seconds", "20").getDoubleValue());
    std::cerr << "usage: fault-harden --state [--fixtures dir] | --write-fixture file | --fuzz [--minutes 10] [--seed 1] | --cpu [--seconds 20]" << std::endl;
    return 1;
}
