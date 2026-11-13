// fault-render: runs a WAV through Fault's DSP engine.
//
//   fault-render --in in.wav --out out.wav [--params p.json] [--png out.png] [--ticks ticks.csv]
//
// p.json (every key optional):
//   { "time": 0.5, "repeats": 4, "mix": 1.0, "freeze": false, "freezeMode": "latching"|"momentary",
//     "clock": "internal"|"host", "windowing": 0.02, "width": 0.5,
//     "tempo": { "bpm": 120, "playing": true },          // the fake host transport
//     "block": 512 | "random",  "tail": 2.0,             // host block size; seconds rendered after the input
//     "automation": [ { "time": 3.0, "param": "freeze", "value": true }, ... ],
//     "mode": "macro"|"micro", "bend": 0.5, "break": 0, "reverse": false, "silence": false, "noteToSpeed": false,
//     "midi": [ { "time": 1.0, "note": 60, "on": true, "velocity": 100 }, ... ] }   // sample-accurate

#include "dsp/Engine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

#include <iostream>
#include <map>

namespace
{
int fail (const juce::String& m)
{
    std::cerr << "error: " << m << std::endl;
    return 1;
}

bool apply (fault::dsp::EngineParams& p, const juce::String& key, const juce::var& v)
{
    using namespace fault::dsp;
    if (key == "time") p.time = (float) (double) v;
    else if (key == "repeats") p.repeats = (int) v;
    else if (key == "mix") p.mix = (float) (double) v;
    else if (key == "freeze") p.freeze = (bool) v;
    else if (key == "freezeMode") p.freezeMode = v.toString() == "momentary" ? FreezeMode::momentary : FreezeMode::latching;
    else if (key == "clock") p.clock = v.toString() == "host" ? ClockSource::host : ClockSource::internal;
    else if (key == "windowing") p.windowing = (float) (double) v;
    else if (key == "width") p.width = (float) (double) v;
    else if (key == "mode") p.mode = v.toString() == "micro" ? Mode::micro : Mode::macro;
    else if (key == "bend") p.bend = (float) (double) v;
    else if (key == "break") p.brk = (float) (double) v;
    else if (key == "bendOnMacro") p.bendOnMacro = (bool) v;
    else if (key == "bendOnMicro" || key == "reverse") p.bendOnMicro = (bool) v;
    else if (key == "breakOnMacro") p.breakOnMacro = (bool) v;
    else if (key == "breakOnMicro" || key == "silence") p.breakOnMicro = (bool) v;
    else if (key == "noteToSpeed") p.noteToSpeed = (bool) v;
    else return false;
    return true;
}

struct Event { double time; juce::String param; juce::var value; };
struct Midi { double time; int note, velocity; bool on; };
} // namespace

int main (int argc, char* argv[])
{
    // "--key value" pairs (also "--key=value").
    std::map<juce::String, juce::String> args;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (! a.startsWith ("--"))
            continue;
        if (a.contains ("="))
            args[a.upToFirstOccurrenceOf ("=", false, false)] = a.fromFirstOccurrenceOf ("=", false, false);
        else if (i + 1 < argc)
            args[a] = juce::String (argv[++i]);
    }
    const auto opt = [&args] (const char* k) { const auto it = args.find (k); return it == args.end() ? juce::String() : it->second; };
    if (opt ("--in").isEmpty() || opt ("--out").isEmpty())
        return fail ("usage: fault-render --in in.wav --out out.wav [--params p.json] [--png out.png] [--ticks ticks.csv]");
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (cwd.getChildFile (opt ("--in"))));
    if (reader == nullptr)
        return fail ("cannot read " + opt ("--in"));
    const double sr = reader->sampleRate;
    const int inLen = (int) reader->lengthInSamples;

    fault::dsp::EngineParams p;
    double bpm = 120.0, tail = 2.0;
    bool playing = true, randomBlocks = false;
    int block = 512;
    std::vector<Event> events;
    std::vector<Midi> midi;
    if (opt ("--params").isNotEmpty())
    {
        const auto json = juce::JSON::parse (cwd.getChildFile (opt ("--params")));
        auto* obj = json.getDynamicObject();
        if (obj == nullptr)
            return fail ("params file is not a JSON object");
        for (const auto& prop : obj->getProperties())
        {
            const auto k = prop.name.toString();
            if (k == "tempo") { bpm = prop.value.getProperty ("bpm", 120.0); playing = prop.value.getProperty ("playing", true); }
            else if (k == "block") { randomBlocks = prop.value.toString() == "random"; if (! randomBlocks) block = juce::jlimit (1, 65536, (int) prop.value); }
            else if (k == "tail") tail = prop.value;
            else if (k == "midi") { for (const auto& e : *prop.value.getArray()) midi.push_back ({ (double) e["time"], (int) e["note"], (int) e.getProperty ("velocity", 100), (bool) e.getProperty ("on", true) }); }
            else if (k == "automation") { for (const auto& e : *prop.value.getArray()) events.push_back ({ (double) e["time"], e["param"].toString(), e["value"] }); }
            else if (! apply (p, k, prop.value)) return fail ("unknown key '" + k + "'");
        }
        std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });
        std::stable_sort (midi.begin(), midi.end(), [] (const Midi& a, const Midi& b) { return a.time < b.time; });
    }

    const int total = inLen + (int) (tail * sr);
    juce::AudioBuffer<float> in (2, total), out (2, total);
    in.clear();
    reader->read (&in, 0, inLen, 0, true, true);
    if (reader->numChannels == 1)
        in.copyFrom (1, 0, in, 0, 0, inLen);

    fault::dsp::Engine engine;
    engine.setParams (p);
    engine.prepare (sr, 65536);
    std::vector<fault::dsp::TickEvent> ticks;
    uint64_t seen = 0;
    size_t nextEvent = 0, nextMidi = 0;
    uint32_t rng = 0xC0FFEEu;
    for (int pos = 0; pos < total;)
    {
        bool changed = false;
        while (nextEvent < events.size() && events[nextEvent].time * sr <= pos)
        {
            if (! apply (p, events[nextEvent].param, events[nextEvent].value))
                return fail ("unknown automation param '" + events[nextEvent].param + "'");
            ++nextEvent;
            changed = true;
        }
        if (changed)
            engine.setParams (p);
        int n = block;
        if (randomBlocks) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; n = 1 + (int) (rng % 1024u); }
        n = std::min (n, total - pos);
        if (nextEvent < events.size())   // land automation on its sample
            n = std::max (1, std::min (n, (int) std::ceil (events[nextEvent].time * sr) - pos));
        engine.setTransport ({ true, playing, bpm, pos / sr * bpm / 60.0 });
        while (nextMidi < midi.size() && (int) std::llround (midi[nextMidi].time * sr) < pos + n)
        {
            const auto& m = midi[nextMidi++];
            engine.queueNote ({ std::max (0, (int) std::llround (m.time * sr) - pos), m.note, m.velocity, m.on });
        }
        engine.process (in.getReadPointer (0, pos), in.getReadPointer (1, pos), out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
        for (; seen < engine.ticksWritten(); ++seen)
            ticks.push_back (engine.tick (seen));
        pos += n;
    }

    // ---- WAV
    {
        auto file = cwd.getChildFile (opt ("--out"));
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), sr, 2, 24, {}, 0));
        if (w == nullptr)
            return fail ("cannot write " + file.getFullPathName());
        w->writeFromAudioSampleBuffer (out, 0, total);
    }
    int nonFinite = 0;
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const float v = out.getSample (ch, i);
            nonFinite += std::isfinite (v) ? 0 : 1;
            peak = std::max (peak, std::abs (v));
        }

    // ---- tick log
    if (opt ("--ticks").isNotEmpty())
    {
        juce::String csv ("sample,seconds,window_start,window_length,period,repeats,frozen\n");
        for (const auto& t : ticks)
            csv << t.sample << "," << juce::String (t.sample / sr, 6) << "," << juce::String (t.windowStart, 1) << "," << juce::String (t.windowLength, 1)
                << "," << juce::String (t.period, 2) << "," << t.repeats << "," << (t.frozen ? 1 : 0) << "\n";
        cwd.getChildFile (opt ("--ticks")).replaceWithText (csv);
    }

    // ---- PNG: input (grey) behind output (teal), ticks (white), slice divisions (dim), frozen ticks (blue)
    if (opt ("--png").isNotEmpty())
    {
        const int W = 1800, H = 420, top = 28, mid = top + (H - top) / 2, half = (H - top) / 2 - 8;
        juce::Image img (juce::Image::RGB, W, H, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0e1013));
        const auto col = [&] (int i) { return (float) i / (float) total * (float) W; };
        for (int x = 0; x < W; ++x)
        {
            const int a = (int) ((int64_t) x * total / W), b = std::max (a + 1, (int) ((int64_t) (x + 1) * total / W));
            float imn = 0, imx = 0, omn = 0, omx = 0;
            for (int i = a; i < b && i < total; ++i)
            {
                imn = std::min (imn, in.getSample (0, i)); imx = std::max (imx, in.getSample (0, i));
                omn = std::min (omn, out.getSample (0, i)); omx = std::max (omx, out.getSample (0, i));
            }
            g.setColour (juce::Colour (0xff3a3f47));
            g.drawVerticalLine (x, (float) mid - imx * (float) half, (float) mid - imn * (float) half + 1.0f);
            g.setColour (juce::Colour (0xff4fd1b5));
            g.drawVerticalLine (x, (float) mid - omx * (float) half, (float) mid - omn * (float) half + 1.0f);
        }
        for (size_t k = 0; k < ticks.size(); ++k)
        {
            const auto& t = ticks[k];
            const double end = k + 1 < ticks.size() ? (double) ticks[k + 1].sample : (double) total;
            const double slice = t.windowLength / std::max (1, t.repeats);
            if (t.repeats > 1 && slice * W / total > 3.0)
            {
                g.setColour (juce::Colour (0x40ffffff));
                for (double s = (double) t.sample + slice; s < end - 1.0; s += slice)
                    g.drawVerticalLine ((int) col ((int) s), (float) top, (float) H);
            }
            g.setColour (t.frozen ? juce::Colour (0xff6aa8ff) : juce::Colour (0xc0ffffff));
            g.drawVerticalLine ((int) col ((int) t.sample), (float) top, (float) H);
        }
        g.setColour (juce::Colours::white);
        g.setFont (14.0f);
        g.drawText (juce::File (opt ("--out")).getFileNameWithoutExtension() + "   " + juce::String ((int) ticks.size()) + " ticks   peak "
                        + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS",
                    10, 4, W - 20, 20, juce::Justification::centredLeft);
        auto file = cwd.getChildFile (opt ("--png"));
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = file.createOutputStream())
            png.writeImageToStream (img, *os);
    }

    std::cout << "rendered " << opt ("--out") << ": " << total / sr << " s, " << ticks.size() << " ticks, peak "
              << juce::Decibels::gainToDecibels (peak) << " dBFS, non-finite " << nonFinite << std::endl;
    return nonFinite == 0 ? 0 : 2;
}
