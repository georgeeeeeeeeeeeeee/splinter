#include "plugin/PluginEditor.h"

#include "dsp/Corrupt/Corrupt.h"
#include "dsp/Params.h"

namespace fault
{
namespace
{
using namespace ui;
const char* kBendZones[] = { "Off", "Reverse", "Octave", "Two octaves", "Slew", "Tape stop", "Everything" };
const char* kBreakZones[] = { "Off", "Halve", "Jump", "Subdivide", "Audio rate", "Silence", "Everything" };
// Host / note steps as note lengths (ticks per beat 1/16 .. 8).
const char* kStepNotes[] = { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/32" };

juce::String hzOrSeconds (double s)
{
    if (s >= 1.0) return juce::String (s, 2) + " s";
    if (s >= 0.1) return juce::String (juce::roundToInt (s * 1000.0)) + " ms";
    return juce::String (1.0 / s, 1) + " Hz";
}
} // namespace

FaultEditor::FaultEditor (FaultProcessor& p)
    : AudioProcessorEditor (p), proc (p), view (p.engine()),
      bendOn (p.getApvts(), "bendOnMacro", "Bend", colours::bend), breakOn (p.getApvts(), "breakOnMacro", "Break", colours::brk),
      freeze (p.getApvts(), "freeze", "Freeze", colours::freeze), modDrawer (p), settingsDrawer (p)
{
    setLookAndFeel (&look);
    auto& s = p.getApvts();
    const auto& st = p.engine().status();
    const auto knob = [&] (const char* id, const char* name, juce::Colour c, dsp::ModDest d, bool bipolar = false) {
        auto k = std::make_unique<Knob> (param (s, id), name, c, (int) d, bipolar);
        k->modulated = [&st, d] { return st.modulated[(size_t) d].load (std::memory_order_relaxed); };
        k->onAssign = [this] (int src, int dest) {
            if (proc.assignRoute (src, dest) < 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Routes full", "All 8 routes are in use: clear one in the mod drawer.");
        };
        content.addAndMakeVisible (*k);
        return k;
    };
    time = knob ("time", "Time", colours::buffer, dsp::ModDest::time);
    repeats = knob ("repeats", "Repeats", colours::buffer, dsp::ModDest::repeats);
    mix = knob ("mix", "Mix", colours::buffer, dsp::ModDest::mix);
    bend = knob ("bend", "Bend", colours::bend, dsp::ModDest::bend);
    brk = knob ("break", "Break", colours::brk, dsp::ModDest::brk);
    corrupt = knob ("corrupt", "Corrupt", colours::corrupt, dsp::ModDest::corrupt);
    time->valueText = [this] { return timeText(); };
    bend->valueText = [this] { return bendText(); };
    brk->valueText = [this] { return breakText(); };
    corrupt->valueText = [this] { return corruptText(); };
    view.clockText = [this] { return clockText(); };

    corruptType = std::make_unique<ParamCombo> (s, "corruptType");
    for (juce::Component* c : { (juce::Component*) &view, (juce::Component*) &bendOn, (juce::Component*) &breakOn, (juce::Component*) &freeze,
                                (juce::Component*) corruptType.get(), (juce::Component*) &presetBox })
        content.addAndMakeVisible (c);
    bendOn.setTooltip ("Macro: let the Bend dice play. Micro: reverse");
    breakOn.setTooltip ("Macro: let the Break dice play. Micro: Traverse (off) or Silence (on)");
    freeze.setTooltip ("Stop recording: the window keeps playing what's in the buffer");

    const auto setChoice = [&s] (const char* id, int index) {
        if (auto* q = s.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 ((float) index));
    };
    const auto segment = [&] (juce::TextButton& b, juce::Colour c, std::function<void()> f, const char* tip) {
        b.setColour (juce::TextButton::buttonOnColourId, c);
        b.onClick = std::move (f);
        b.setTooltip (tip);
        content.addAndMakeVisible (b);
    };
    segment (macroBtn, colours::text, [=] { setChoice ("mode", 0); }, "Macro: Bend and Break roll dice on every tick");
    segment (microBtn, colours::text, [=] { setChoice ("mode", 1); }, "Micro: Bend is speed, Break picks the slice or the silence");
    segment (intBtn, colours::buffer, [=] { setChoice ("clockSource", 0); }, "Internal clock: Time is the period, 16 s to 80 Hz");
    segment (hostBtn, colours::buffer, [=] { setChoice ("clockSource", 1); }, "Host tempo: Time picks a note length, locked to the bar");
    segment (noteBtn, colours::buffer, [=] { setChoice ("clockSource", 2); }, "Note clock: each Note-clock gate note is a pulse");
    resetBtn.setTooltip ("Re-sync the clock (and capture a new window)");
    resetBtn.onStateChange = [this, &s] {
        if (auto* q = s.getParameter ("reset")) q->setValueNotifyingHost (resetBtn.isDown() ? 1.0f : 0.0f);
    };
    content.addAndMakeVisible (resetBtn);

    // Break this bar.
    fillBtn.setTooltip ("Break this bar: waits for its place before the next bar line, glitches it, and lets go on the downbeat. "
                        "Also on a MIDI note (the Break bar gate) and as a parameter you can map to a key");
    fillBtn.setColour (juce::TextButton::buttonOnColourId, colours::brk);
    fillBtn.onStateChange = [this, &s] {
        if (auto* q = s.getParameter ("fillTrigger"))
            if ((q->getValue() > 0.5f) != fillBtn.isDown()) q->setValueNotifyingHost (fillBtn.isDown() ? 1.0f : 0.0f);
    };
    content.addAndMakeVisible (fillBtn);
    fillLength = std::make_unique<ParamCombo> (s, "fillLength");
    fillEvery = std::make_unique<ParamCombo> (s, "fillEvery");
    fillLength->box.setTooltip ("How much breaks: the last beat, the last two beats, the whole bar or two bars");
    fillEvery->box.setTooltip ("By hand, or on its own: the end of every 2, 4, 8 or 16 bars, with the Chance below");
    fillAmount = std::make_unique<ParamSlider> (s, "fillAmount", "Amount", colours::brk);
    fillChance = std::make_unique<ParamSlider> (s, "fillChance", "Chance", colours::brk);
    for (auto* c : { (juce::Component*) fillLength.get(), (juce::Component*) fillEvery.get(), (juce::Component*) fillAmount.get(),
                     (juce::Component*) fillChance.get() })
        content.addAndMakeVisible (c);

    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &modBtn, &setBtn })
        content.addAndMakeVisible (b);
    modBtn.setColour (juce::TextButton::buttonOnColourId, colours::mod);
    setBtn.setColour (juce::TextButton::buttonOnColourId, colours::text);
    modBtn.onClick = [this] { showDrawer (modDrawer.isVisible() ? 0 : 1); };
    setBtn.onClick = [this] { showDrawer (settingsDrawer.isVisible() ? 0 : 2); };
    const auto step = [this] (int d) {
        const int n = (int) proc.presetManager().presets().size();
        if (n == 0) return;
        const int cur = proc.presetManager().current();
        juce::String err;
        proc.presetManager().load (((cur < 0 ? (d > 0 ? -1 : 0) : cur) + d + n) % n, err);
    };
    prevBtn.onClick = [step] { step (-1); };
    nextBtn.onClick = [step] { step (1); };
    saveBtn.onClick = [this] { savePreset(); };
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id > 0 && id - 1 != proc.presetManager().current())
        {
            juce::String err;
            proc.presetManager().load (id - 1, err);
        }
    };
    presetBox.setTextWhenNothingSelected ("Init");

    content.addChildComponent (modDrawer);
    content.addChildComponent (settingsDrawer);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kBaseW, kBaseH);
    layout();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kBaseW / kBaseH);
    setResizeLimits (kBaseW * 8 / 10, kBaseH * 8 / 10, kBaseW * 2, kBaseH * 2);
    setSize (kBaseW, kBaseH);
    startTimerHz (60);
}

FaultEditor::~FaultEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FaultEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
}

void FaultEditor::layout()
{
    // Header.
    presetBox.setBounds (330, 12, 240, 24);
    prevBtn.setBounds (300, 12, 26, 24);
    nextBtn.setBounds (574, 12, 26, 24);
    saveBtn.setBounds (606, 12, 50, 24);
    modBtn.setBounds (744, 12, 60, 24);
    setBtn.setBounds (810, 12, 74, 24);

    // Knob columns.
    const int kw = 150, kh = 128, top = 56;
    time->setBounds (18, top, kw, kh);
    repeats->setBounds (18, top + kh + 10, kw, kh);
    mix->setBounds (18, top + 2 * (kh + 10), kw, kh);
    bend->setBounds (732, top, kw, kh - 6);
    bendOn.setBounds (777, top + kh - 4, 60, 20);
    brk->setBounds (732, top + kh + 18, kw, kh - 6);
    breakOn.setBounds (777, top + 2 * kh + 14, 60, 20);
    corrupt->setBounds (732, top + 2 * (kh + 18), kw, kh - 6);
    corruptType->setBounds (757, top + 3 * kh + 32, 110, 22);

    // Centre.
    const juce::Rectangle<int> centre (184, 52, 532, 410);
    view.setBounds (centre);
    // Transport row under the view.
    int x = 184;
    const int y = 476, h = 28;
    for (auto* b : { &macroBtn, &microBtn }) { b->setBounds (x, y, 62, h); x += 64; }
    x += 14;
    freeze.setBounds (x, y, 78, h);
    x += 92;
    for (auto* b : { &intBtn, &hostBtn, &noteBtn }) { b->setBounds (x, y, 52, h); x += 54; }
    x += 14;
    resetBtn.setBounds (x, y, 66, h);

    // Break this bar, along the bottom.
    {
        const int fy = 552, fh = 28;
        fillBtn.setBounds (18, fy, 150, fh);
        fillLength->setBounds (184, fy + 2, 96, fh - 4);
        fillAmount->setBounds (296, fy, 210, fh);
        fillEvery->setBounds (528, fy + 2, 130, fh - 4);
        fillChance->setBounds (674, fy, 210, fh);
    }

    const auto drawerArea = juce::Rectangle<int> (184, 52, 532, 496);
    modDrawer.setBounds (drawerArea);
    settingsDrawer.setBounds (drawerArea);
}

void FaultEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    // The wordmark: drawn, then a band of it re-drawn sideways, like a buffer skipping.
    const juce::Rectangle<int> mark (20, 8, 122, 32);
    const auto word = [&] (juce::Colour c, int dx) {
        g.setColour (c);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold).withKerningFactor (0.12f));
        g.drawText ("SPLINTER", mark.translated (dx, 0), juce::Justification::centredLeft, false);
    };
    word (colours::corrupt.withAlpha (0.55f), -2);
    word (colours::buffer.withAlpha (0.55f), 2);
    word (colours::text, 0);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (mark.getX(), mark.getY() + 15, mark.getWidth(), 4);
        g.setColour (colours::bg);
        g.fillRect (mark);
        word (colours::text, 6);
    }
    drawCaption (g, "glitch buffer", { 146, 16, 96, 16 }, colours::faint);
    g.setColour (colours::line);
    g.drawHorizontalLine (46, 16.0f, 884.0f);
    drawCaption (g, "Preset", { 248, 16, 50, 16 }, colours::faint);
    // Column captions.
    drawCaption (g, "Buffer", { 18, 512, 150, 14 }, colours::faint, juce::Justification::centred);
    drawCaption (g, "Damage", { 732, 512, 150, 14 }, colours::faint, juce::Justification::centred);
    drawCaption (g, "Mode", { 184, 506, 126, 14 }, colours::faint, juce::Justification::centred);
    drawCaption (g, "Clock", { 460, 506, 160, 14 }, colours::faint, juce::Justification::centred);
    g.setColour (colours::line);
    g.drawHorizontalLine (536, 16.0f, 884.0f);
}

void FaultEditor::showDrawer (int which)
{
    modDrawer.setVisible (which == 1);
    settingsDrawer.setVisible (which == 2);
    modBtn.setToggleState (which == 1, juce::dontSendNotification);
    setBtn.setToggleState (which == 2, juce::dontSendNotification);
    view.setVisible (which == 0);
}

float FaultEditor::param01 (const char* id) const
{
    return proc.getApvts().getRawParameterValue (id)->load();
}

juce::String FaultEditor::clockText() const
{
    const int clock = (int) std::lround (param01 ("clockSource"));
    const int st = proc.engine().status().clockStep.load();
    if (clock == 0)
        return "Internal";
    return juce::String (clock == 1 ? "Host " : "Note ") + dsp::Clock::kRateNames[(size_t) std::clamp (st, 0, 8)];
}

juce::String FaultEditor::timeText() const
{
    const int clock = (int) std::lround (param01 ("clockSource"));
    const float t = param01 ("time");
    if (clock == 0)
        return hzOrSeconds (dsp::Clock::internalPeriodSeconds (t));
    const int stp = dsp::Clock::hostStep (t);
    return clock == 1 ? juce::String (kStepNotes[stp]) : juce::String (dsp::Clock::kRateNames[(size_t) stp]) + " per pulse";
}

juce::String FaultEditor::bendText() const
{
    const float v = param01 ("bend");
    if (param01 ("mode") > 0.5f)
    {
        const float oct = -3.0f + 6.0f * v;
        return (oct >= 0.0f ? "+" : "") + juce::String (oct, 2) + " oct";
    }
    float pos = 0.0f;
    const int z = dsp::MacroTables::zoneOf (proc.macroTables().bendZoneStart, v, pos);
    return z == 0 ? juce::String ("Off")
                  : juce::String (kBendZones[z]) + " " + juce::String ((int) std::lround (pos * 100.0f)) + "%" + (param01 ("bendOnMacro") > 0.5f ? "" : " (off)");
}

juce::String FaultEditor::breakText() const
{
    const float v = param01 ("break");
    if (param01 ("mode") > 0.5f)
    {
        if (param01 ("breakOnMicro") > 0.5f)
            return juce::String ((int) std::lround (v * 90.0f)) + "% silent";
        const int n = std::max (1, (int) std::lround (param01 ("repeats")));
        return n <= 1 ? juce::String ("Traverse (Repeats 1)") : "Slice " + juce::String (std::min (n - 1, (int) std::floor (v * (float) n)) + 1) + " of " + juce::String (n);
    }
    float pos = 0.0f;
    const int z = dsp::MacroTables::zoneOf (proc.macroTables().breakZoneStart, v, pos);
    return z == 0 ? juce::String ("Off")
                  : juce::String (kBreakZones[z]) + " " + juce::String ((int) std::lround (pos * 100.0f)) + "%" + (param01 ("breakOnMacro") > 0.5f ? "" : " (off)");
}

juce::String FaultEditor::corruptText() const
{
    const float v = param01 ("corrupt");
    const auto type = dsp::Corrupt::effectiveType ((dsp::CorruptType) std::lround (param01 ("corruptType")),
                                                   param01 ("corruptSet") > 0.5f ? dsp::CorruptSet::original3 : dsp::CorruptSet::all);
    if (dsp::Corrupt::isBypass (type, v))
        return "Off";
    switch (type)
    {
        case dsp::CorruptType::decimate: return dsp::kDecimatePresets[(size_t) dsp::Corrupt::decimatePreset (v)].name;
        case dsp::CorruptType::dropout: return juce::String (v < 0.5f ? "Few, long " : "Many, short ") + juce::String ((int) std::lround (v * 100.0f)) + "%";
        case dsp::CorruptType::destroy:
            return v <= 0.5f ? "Drive +" + juce::String ((int) std::lround (60.0f * v)) + " dB" : "Clip + fold " + juce::String ((int) std::lround ((v - 0.5f) * 200.0f)) + "%";
        case dsp::CorruptType::djFilter:
        {
            const double t = std::clamp ((std::abs (v - 0.5) - dsp::DjFilter::kDeadZone) / (0.5 - dsp::DjFilter::kDeadZone), 0.0, 1.0);
            const double hz = v < 0.5f ? 20000.0 * std::pow (50.0 / 20000.0, t) : 20.0 * std::pow (8000.0 / 20.0, t);
            return juce::String (v < 0.5f ? "LP " : "HP ") + (hz >= 1000.0 ? juce::String (hz / 1000.0, 1) + " kHz" : juce::String ((int) hz) + " Hz");
        }
        case dsp::CorruptType::vinyl: return "Age " + juce::String ((int) std::lround (v * 100.0f)) + "%";
    }
    return {};
}

void FaultEditor::refreshPresetBox()
{
    const auto& pm = proc.presetManager();
    if ((int) pm.presets().size() != presetCount)
    {
        presetCount = (int) pm.presets().size();
        presetBox.clear (juce::dontSendNotification);
        juce::String cat;
        for (int i = 0; i < presetCount; ++i)
        {
            const auto& pr = pm.presets()[(size_t) i];
            if (pr.category != cat)
            {
                presetBox.addSectionHeading (pr.category);
                cat = pr.category;
            }
            presetBox.addItem (pr.name, i + 1);
        }
        shownPreset = {};
    }
    if (pm.currentName() != shownPreset || presetBox.getSelectedId() != pm.current() + 1)
    {
        shownPreset = pm.currentName();
        if (pm.current() >= 0)
            presetBox.setSelectedId (pm.current() + 1, juce::dontSendNotification);
        else
            presetBox.setText (shownPreset, juce::dontSendNotification);
        if (pm.current() >= 0)
            presetBox.setTooltip (pm.presets()[(size_t) pm.current()].about);
    }
}

void FaultEditor::savePreset()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Saved to " + PresetManager::userFolder().getFullPathName(), juce::MessageBoxIconType::NoIcon, this);
    saveWindow->addTextEditor ("name", proc.presetManager().currentName(), "Name");
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r) {
        if (r == 1 && saveWindow != nullptr)
        {
            juce::String err;
            if (! proc.presetManager().save (saveWindow->getTextEditorContents ("name"), err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", err);
            presetCount = -1;
        }
        saveWindow.reset();
    }), false);
}

void FaultEditor::timerCallback()
{
    auto& s = proc.getApvts();
    const bool micro = param01 ("mode") > 0.5f;
    // The Bend / Break buttons act on the current mode's toggle (DECISIONS D9).
    bendOn.attach (s, micro ? "bendOnMicro" : "bendOnMacro");
    breakOn.attach (s, micro ? "breakOnMicro" : "breakOnMacro");
    bendOn.setButtonText (micro ? "Reverse" : "Bend");
    breakOn.setButtonText (micro ? "Silence" : "Break");
    macroBtn.setToggleState (! micro, juce::dontSendNotification);
    microBtn.setToggleState (micro, juce::dontSendNotification);
    const int clock = (int) std::lround (param01 ("clockSource"));
    intBtn.setToggleState (clock == 0, juce::dontSendNotification);
    hostBtn.setToggleState (clock == 1, juce::dontSendNotification);
    noteBtn.setToggleState (clock == 2, juce::dontSendNotification);
    // A gate can freeze without the parameter: the button shows what the buffer is doing.
    freeze.setColour (juce::TextButton::textColourOffId, proc.engine().status().frozen.load() ? colours::freeze : colours::dim);
    // Break this bar: lit while it plays, blinking while it waits for its place.
    {
        const int f = proc.engine().status().fill.load (std::memory_order_relaxed);
        const bool blink = (juce::Time::getMillisecondCounter() / 150) % 2 == 0;
        fillBtn.setToggleState (f == 2 || (f == 1 && blink), juce::dontSendNotification);
        fillBtn.setButtonText (f == 1 ? "Break this bar (armed)" : "Break this bar");
        fillChance->setEnabled (param01 ("fillEvery") > 0.5f);
    }

    for (auto* k : { time.get(), repeats.get(), mix.get(), bend.get(), brk.get(), corrupt.get() })
        k->refresh();
    if (view.isVisible()) view.tick();
    if (modDrawer.isVisible()) modDrawer.tick();
    if (settingsDrawer.isVisible()) settingsDrawer.tick();
    refreshPresetBox();
}
} // namespace fault
