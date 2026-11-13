#include "ui/ModDrawer.h"

namespace fault::ui
{
ModDrawer::ModDrawer (FaultProcessor& p) : proc (p)
{
    auto& s = p.getApvts();
    for (int m = 0; m < dsp::kNumModulators; ++m)
    {
        auto& c = mods[(size_t) m];
        const juce::String id = "mod" + juce::String (m + 1);
        c.chip = std::make_unique<SourceChip> ((int) dsp::ModSource::mod1 + m, "MOD " + juce::String (m + 1));
        c.shape = std::make_unique<ParamCombo> (s, id + "Shape");
        c.sync = std::make_unique<ParamCombo> (s, id + "Sync");
        c.polarity = std::make_unique<ParamCombo> (s, id + "Polarity");
        c.rate = std::make_unique<ParamSlider> (s, id + "Rate", "Rate", colours::mod);
        c.phase = std::make_unique<ParamSlider> (s, id + "Phase", "Phase", colours::mod);
        c.rate->labelWidth = c.phase->labelWidth = 34;
        c.rate->valueWidth = c.phase->valueWidth = 46;
        for (juce::Component* x : { (juce::Component*) c.chip.get(), (juce::Component*) &c.scope, (juce::Component*) c.shape.get(), (juce::Component*) c.sync.get(),
                                    (juce::Component*) c.polarity.get(), (juce::Component*) c.rate.get(), (juce::Component*) c.phase.get() })
            addAndMakeVisible (x);
    }
    envChip = std::make_unique<SourceChip> ((int) dsp::ModSource::env, "ENV");
    envAttack = std::make_unique<ParamSlider> (s, "envAttack", "Att", colours::mod);
    envRelease = std::make_unique<ParamSlider> (s, "envRelease", "Rel", colours::mod);
    envGain = std::make_unique<ParamSlider> (s, "envGain", "Gain", colours::mod);
    for (auto* x : { envAttack.get(), envRelease.get(), envGain.get() }) { x->labelWidth = 28; x->valueWidth = 50; addAndMakeVisible (x); }
    addAndMakeVisible (*envChip);
    addAndMakeVisible (envScope);
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
    {
        auto& cc = ccs[(size_t) c];
        cc.chip = std::make_unique<SourceChip> ((int) dsp::ModSource::cc1 + c, "CC " + juce::String (c + 1));
        cc.polarity = std::make_unique<ParamCombo> (s, "cc" + juce::String (c + 1) + "Polarity");
        cc.learn.onClick = [this, c] { proc.learnCc.store (proc.learnCc.load() == c ? -1 : c); };
        cc.learn.setTooltip ("Click, then move a controller to assign it to this lane");
        addAndMakeVisible (*cc.chip);
        addAndMakeVisible (cc.scope);
        addAndMakeVisible (cc.learn);
        addAndMakeVisible (*cc.polarity);
    }
    for (int r = 0; r < dsp::kNumRoutes; ++r)
    {
        auto& row = routes[(size_t) r];
        const juce::String id = "route" + juce::String (r + 1);
        row.source = std::make_unique<ParamCombo> (s, id + "Source");
        row.dest = std::make_unique<ParamCombo> (s, id + "Dest");
        row.depth = std::make_unique<ParamSlider> (s, id + "Depth", juce::String (r + 1), colours::mod);
        row.depth->labelWidth = 0;
        row.depth->valueWidth = 40;
        row.clear.onClick = [&s, id] {
            for (const char* w : { "Source", "Dest", "Depth" })
                if (auto* q = s.getParameter (id + w)) q->setValueNotifyingHost (q->getDefaultValue());
        };
        row.clear.setTooltip ("Clear this route");
        addAndMakeVisible (*row.source);
        addAndMakeVisible (*row.dest);
        addAndMakeVisible (*row.depth);
        addAndMakeVisible (row.clear);
    }
}

void ModDrawer::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel.withAlpha (0.98f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    drawCaption (g, "Modulators", modArea.withHeight (14).translated (0, -16));
    drawCaption (g, "drag a chip onto a knob to route it", modArea.withHeight (14).translated (0, -16), colours::faint, juce::Justification::centredRight);
    drawCaption (g, "Envelope + MIDI CC", srcArea.withHeight (14).translated (0, -16));
    drawCaption (g, "Routes", routeArea.withHeight (14).translated (0, -16));
    // Card backgrounds.
    g.setColour (colours::raised.withAlpha (0.55f));
    for (const auto& c : mods) g.fillRoundedRectangle (c.scope.getBounds().getUnion (c.phase->getBounds()).expanded (5).toFloat(), 4.0f);
    g.fillRoundedRectangle (envScope.getBounds().getUnion (envGain->getBounds()).expanded (5).toFloat(), 4.0f);
    for (const auto& c : ccs) g.fillRoundedRectangle (c.scope.getBounds().getUnion (c.polarity->getBounds()).expanded (5).toFloat(), 4.0f);
}

void ModDrawer::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (16);
    modArea = r.removeFromTop (178);
    r.removeFromTop (24);
    srcArea = r.removeFromTop (104);
    r.removeFromTop (24);
    routeArea = r;

    const int gap = 10, mw = (modArea.getWidth() - gap * 3) / 4;
    for (int m = 0; m < dsp::kNumModulators; ++m)
    {
        auto& c = mods[(size_t) m];
        auto col = juce::Rectangle<int> (modArea.getX() + m * (mw + gap), modArea.getY(), mw, modArea.getHeight()).reduced (5);
        c.chip->setBounds (col.removeFromTop (18).withSizeKeepingCentre (64, 18));
        col.removeFromTop (4);
        c.scope.setBounds (col.removeFromTop (38));
        col.removeFromTop (4);
        c.shape->setBounds (col.removeFromTop (20));
        col.removeFromTop (3);
        c.sync->setBounds (col.removeFromTop (20));
        col.removeFromTop (3);
        c.rate->setBounds (col.removeFromTop (18));
        c.phase->setBounds (col.removeFromTop (18));
        col.removeFromTop (3);
        c.polarity->setBounds (col.removeFromTop (20));
    }
    const int sw = (srcArea.getWidth() - gap * 4) / 5;
    {
        auto col = juce::Rectangle<int> (srcArea.getX(), srcArea.getY(), sw, srcArea.getHeight()).reduced (5);
        envChip->setBounds (col.removeFromTop (18).withSizeKeepingCentre (54, 18));
        col.removeFromTop (4);
        envScope.setBounds (col.removeFromTop (22));
        col.removeFromTop (2);
        envAttack->setBounds (col.removeFromTop (16));
        envRelease->setBounds (col.removeFromTop (16));
        envGain->setBounds (col.removeFromTop (16));
    }
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
    {
        auto& cc = ccs[(size_t) c];
        auto col = juce::Rectangle<int> (srcArea.getX() + (c + 1) * (sw + gap), srcArea.getY(), sw, srcArea.getHeight()).reduced (5);
        cc.chip->setBounds (col.removeFromTop (18).withSizeKeepingCentre (54, 18));
        col.removeFromTop (4);
        cc.scope.setBounds (col.removeFromTop (22));
        col.removeFromTop (4);
        cc.learn.setBounds (col.removeFromTop (20));
        col.removeFromTop (3);
        cc.polarity->setBounds (col.removeFromTop (20));
    }
    const int rw = (routeArea.getWidth() - 16) / 2, rh = 24;
    for (int k = 0; k < dsp::kNumRoutes; ++k)
    {
        auto& row = routes[(size_t) k];
        auto line = juce::Rectangle<int> (routeArea.getX() + (k / 4) * (rw + 16), routeArea.getY() + (k % 4) * (rh + 4), rw, rh);
        row.clear.setBounds (line.removeFromRight (20).reduced (0, 2));
        line.removeFromRight (4);
        row.source->setBounds (line.removeFromLeft (rw * 30 / 100).reduced (0, 1));
        line.removeFromLeft (4);
        row.dest->setBounds (line.removeFromLeft (rw * 28 / 100).reduced (0, 1));
        line.removeFromLeft (6);
        row.depth->setBounds (line);
    }
}

void ModDrawer::tick()
{
    const auto& st = proc.engine().status();
    auto& s = proc.getApvts();
    for (int m = 0; m < dsp::kNumModulators; ++m)
    {
        auto& c = mods[(size_t) m];
        const juce::String id = "mod" + juce::String (m + 1);
        const bool bi = s.getRawParameterValue (id + "Polarity")->load() > 0.5f;
        c.scope.push (st.sources[(size_t) dsp::ModSource::mod1 + (size_t) m].load(), bi);
        c.rate->setEnabled (s.getRawParameterValue (id + "Sync")->load() < 0.5f);   // synced: the length sets it
    }
    envScope.push (st.sources[(size_t) dsp::ModSource::env].load(), false);
    for (int c = 0; c < dsp::kNumCcLanes; ++c)
    {
        auto& cc = ccs[(size_t) c];
        const bool bi = s.getRawParameterValue ("cc" + juce::String (c + 1) + "Polarity")->load() > 0.5f;
        cc.scope.push (st.sources[(size_t) dsp::ModSource::cc1 + (size_t) c].load(), bi);
        const bool learning = proc.learnCc.load() == c;
        cc.learn.setButtonText (learning ? "move a CC..." : "CC# " + juce::String (proc.ccNumbers[(size_t) c].load()));
        cc.learn.setToggleState (learning, juce::dontSendNotification);
        cc.learn.setColour (juce::TextButton::buttonOnColourId, colours::mod);
    }
}
} // namespace fault::ui
