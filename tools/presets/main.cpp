// fault-presets: the real FaultProcessor, offline.
//
//   fault-presets --check --in build/fixtures/drums.wav [--out build/renders/presets]
//       loads every factory preset strictly, renders the input through it (120 BPM, playing), prints
//       peak / RMS against the dry input, and fails on a bad preset, NaN/Inf or a peak over +6 dBFS.
//   fault-presets --snapshot ui.png --in drums.wav [--preset "Sixteenth Stutter"] [--drawer mod|settings] [--scale 1.5] [--seconds 3]

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <map>

namespace
{
struct FakeHost final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (true);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

bool readWav (const juce::File& f, juce::AudioBuffer<float>& out, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    sr = r->sampleRate;
    out.setSize (2, (int) r->lengthInSamples);
    r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
    if (r->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, out.getNumSamples());
    return true;
}

// Runs `in` (plus a tail) through the processor in 512-sample blocks, host playing at 120 BPM.
juce::AudioBuffer<float> run (fault::FaultProcessor& p, const juce::AudioBuffer<float>& in, double sr, double seconds)
{
    FakeHost host;
    p.setPlayHead (&host);
    p.prepareToPlay (sr, 512);
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        juce::AudioBuffer<float> block (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                block.setSample (ch, i, pos + i < in.getNumSamples() ? in.getSample (ch, pos + i) : 0.0f);
        host.ppq = pos / sr * host.bpm / 60.0;
        p.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, block, ch, 0, n);
    }
    p.setPlayHead (nullptr);
    return out;
}

double rms (const juce::AudioBuffer<float>& b, int n)
{
    double s = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return std::sqrt (s / std::max (1, 2 * n));
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
    const auto opt = [&] (const char* k) { auto it = args.find (k); return it == args.end() ? juce::String() : it->second; };
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    juce::AudioBuffer<float> in;
    double sr = 48000.0;
    if (opt ("--in").isNotEmpty() && ! readWav (cwd.getChildFile (opt ("--in")), in, sr))
    {
        std::cerr << "error: cannot read " << opt ("--in") << std::endl;
        return 1;
    }

    if (opt ("--check").isNotEmpty())
    {
        fault::FaultProcessor probe;
        const auto list = probe.presetManager().presets();
        int failures = 0;
        const double dryRms = in.getNumSamples() > 0 ? rms (in, in.getNumSamples()) : 0.0;
        const auto outDir = opt ("--out").isNotEmpty() ? cwd.getChildFile (opt ("--out")) : juce::File();
        if (outDir != juce::File()) outDir.createDirectory();
        for (const auto& pr : list)
        {
            if (pr.user) continue;
            fault::FaultProcessor p;
            juce::String err;
            const bool ok = fault::PresetManager::apply (p.getApvts(), juce::JSON::parse (pr.json), err, true);
            juce::String line = (pr.category + " / " + pr.name).paddedRight (' ', 32);
            if (! ok)
            {
                std::cout << line << "  BAD PRESET: " << err << std::endl;
                ++failures;
                continue;
            }
            if (in.getNumSamples() > 0)
            {
                const auto out = run (p, in, sr, in.getNumSamples() / sr + 1.0);
                int nonFinite = 0;
                float peak = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                    {
                        const float v = out.getSample (ch, i);
                        nonFinite += std::isfinite (v) ? 0 : 1;
                        peak = std::max (peak, std::abs (v));
                    }
                const double r = rms (out, out.getNumSamples());
                const double peakDb = 20.0 * std::log10 (std::max (1.0e-9f, peak)), rmsDb = 20.0 * std::log10 (std::max (1.0e-12, r / std::max (1.0e-12, dryRms)));
                line << "peak " << juce::String (peakDb, 1).paddedLeft (' ', 6) << " dBFS   rms vs dry " << juce::String (rmsDb, 1).paddedLeft (' ', 6) << " dB";
                if (nonFinite > 0 || peakDb > 6.0) { line << "   FAIL"; ++failures; }
                if (outDir != juce::File())
                {
                    const auto f = outDir.getChildFile (juce::File::createLegalFileName (pr.category + " - " + pr.name) + ".wav");
                    f.deleteFile();
                    juce::WavAudioFormat wav;
                    if (std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0)); w != nullptr)
                        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
                }
            }
            std::cout << line << std::endl;
        }
        std::cout << list.size() << " presets, " << failures << " failed" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    if (opt ("--snapshot").isNotEmpty())
    {
        fault::FaultProcessor p;
        if (opt ("--preset").isNotEmpty())
        {
            const auto& list = p.presetManager().presets();
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].name.equalsIgnoreCase (opt ("--preset")))
                {
                    juce::String err;
                    p.presetManager().load (i, err);
                }
        }
        const double seconds = opt ("--seconds").isNotEmpty() ? opt ("--seconds").getDoubleValue() : 3.0;
        if (in.getNumSamples() > 0) run (p, in, sr, seconds);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* fe = dynamic_cast<fault::FaultEditor*> (ed.get());
        const float scale = opt ("--scale").isNotEmpty() ? (float) opt ("--scale").getDoubleValue() : 1.0f;
        ed->setSize ((int) (fault::FaultEditor::kBaseW * scale), (int) (fault::FaultEditor::kBaseH * scale));
        if (opt ("--drawer") == "mod") fe->showDrawer (1);
        if (opt ("--drawer") == "settings") fe->showDrawer (2);
        for (int i = 0; i < 30; ++i) fe->refreshNow();   // let scopes and the view fill
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const auto f = cwd.getChildFile (opt ("--snapshot"));
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
        std::cout << "wrote " << f.getFullPathName() << std::endl;
        return 0;
    }
    std::cerr << "usage: fault-presets --check --in in.wav [--out dir] | --snapshot ui.png --in in.wav [--preset name] [--drawer mod|settings]" << std::endl;
    return 1;
}
