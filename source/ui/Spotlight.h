#pragma once
// Spotlight: shows what's being moved from outside the window (Live's Key Map, MIDI Map, automation, a
// controller). When a parameter changes and you didn't touch it in the window, its control glows and a
// tag at the top says "Break Length: 2 bars" for a moment. If the control sits in a closed drawer or
// another page, onReveal lets the editor open it (the "snap to").
//
// Changes made in the window itself are left alone: a slider or button drag/click comes with a change
// gesture (or a mouse press inside the window), and either one keeps that parameter quiet for a moment.
// A preset load (many parameters at once) is taken in silently.
//
// Shared between the Lvrs plug-ins; copy the file as is. Header-only, JUCE only.
//
// Use: Spotlight::tag (component, "paramID") on each control (several may share an ID), make one
// Spotlight over the editor's content (added last, bounds = content), and set onReveal if some controls
// can be hidden.

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace lvrs
{
class Spotlight final : public juce::Component, private juce::Timer
{
public:
    static void tag (juce::Component& c, const juce::String& paramID) { c.getProperties().set (kProp, paramID); }

    Spotlight (juce::AudioProcessor& p, juce::Component& root) : root_ (root)
    {
        setInterceptsMouseClicks (false, false);
        for (auto* q : p.getParameters())
            if (auto* h = dynamic_cast<juce::HostedAudioProcessorParameter*> (q))
            {
                auto w = std::make_unique<Watch>();
                w->param = q;
                w->id = h->getParameterID();
                w->last = q->getValue();
                q->addListener (w.get());
                watches_.push_back (std::move (w));
            }
        startTimerHz (30);
    }
    ~Spotlight() override
    {
        stopTimer();
        for (auto& w : watches_) w->param->removeListener (w.get());
    }

    // The control for `paramID` isn't showing: open what holds it (`c` is the tagged control).
    std::function<void (const juce::String& paramID, juce::Component& c)> onReveal;
    // No control for `paramID` exists right now (the editor builds it on demand, e.g. another module's
    // panel): show it, then the spotlight looks again.
    std::function<void (const juce::String& paramID)> onMissing;
    // Leave some parameters out (meters, hidden ones), by ID.
    std::function<bool (const juce::String& paramID)> ignore;
    bool active = true;

    void paint (juce::Graphics& g) override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (const auto& h : hits_)
        {
            const float a = alpha (h, now);
            if (a <= 0.0f || h.target == nullptr || ! visible (*h.target)) continue;
            const auto r = getLocalArea (h.target.getComponent(), h.target->getLocalBounds()).toFloat().expanded (3.0f);
            g.setColour (kGlow.withAlpha (0.16f * a));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (kGlow.withAlpha (0.9f * a));
            g.drawRoundedRectangle (r, 6.0f, 2.0f);
        }
        // The tags, newest first, along the top centre.
        float y = 6.0f;
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
        {
            const float a = alpha (*it, now);
            if (a <= 0.0f) continue;
            const juce::String text = it->name + ":  " + it->value;
            const juce::Font f (juce::FontOptions (13.0f, juce::Font::bold));
            const float w = juce::GlyphArrangement::getStringWidth (f, text) + 26.0f, h = 22.0f;
            const juce::Rectangle<float> r ((float) getWidth() * 0.5f - w * 0.5f, y, w, h);
            g.setColour (juce::Colour (0xff15171a).withAlpha (0.92f * a));
            g.fillRoundedRectangle (r, h * 0.5f);
            g.setColour (kGlow.withAlpha (a));
            g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, 1.2f);
            g.setFont (f);
            g.drawText (text, r, juce::Justification::centred);
            y += h + 4.0f;
        }
    }

    // For tests / snapshots: how many parameters are lit now, and the newest one's ID.
    int litCount() const { return (int) hits_.size(); }
    juce::String newest() const { return hits_.empty() ? juce::String() : hits_.back().id; }
    void poll() { timerCallback(); }

private:
    static constexpr const char* kProp = "lvrsSpotlight";
    static constexpr double kHoldMs = 900.0, kFadeMs = 600.0, kQuietMs = 400.0;
    static constexpr int kMaxTags = 3, kPresetLoad = 5;
    inline static const juce::Colour kGlow { 0xffffd166 };

    struct Watch final : public juce::AudioProcessorParameter::Listener
    {
        juce::AudioProcessorParameter* param = nullptr;
        juce::String id;
        float last = 0.0f;
        std::atomic<bool> gesture { false };
        std::atomic<double> gestureEnd { 0.0 };
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            gesture.store (starting);
            if (! starting) gestureEnd.store (juce::Time::getMillisecondCounterHiRes());
        }
    };
    struct Hit
    {
        juce::String id, name, value;
        juce::Component::SafePointer<juce::Component> target;
        double at = 0.0;
    };

    static float alpha (const Hit& h, double now)
    {
        const double age = now - h.at;
        if (age < kHoldMs) return 1.0f;
        return (float) juce::jlimit (0.0, 1.0, 1.0 - (age - kHoldMs) / kFadeMs);
    }

    bool touchingWindow() const
    {
        if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) return false;
        auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
        return under != nullptr && (under == &root_ || root_.isParentOf (under) || dynamic_cast<juce::PopupMenu::CustomComponent*> (under) != nullptr
                                    || under->findParentComponentOfClass<juce::ComboBox>() != nullptr);
    }

    // Visible all the way up to the root (isShowing() also needs a window on screen).
    bool visible (const juce::Component& c) const
    {
        for (auto* at = &c; at != nullptr && at != &root_; at = at->getParentComponent())
            if (! at->isVisible()) return false;
        return true;
    }

    juce::Component* find (juce::Component& c, const juce::String& id, bool wantShowing)
    {
        for (auto* child : c.getChildren())
        {
            if (child == this) continue;
            if (child->getProperties()[kProp].toString() == id && (! wantShowing || visible (*child))) return child;
            if (auto* f = find (*child, id, wantShowing)) return f;
        }
        return nullptr;
    }

    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool touching = touchingWindow();
        std::vector<Watch*> moved;
        int changed = 0;   // all of them, the window's own too: a preset load is many at once
        for (auto& w : watches_)
        {
            const float v = w->param->getValue();
            if (std::abs (v - w->last) < 1.0e-5f) continue;
            w->last = v;
            ++changed;
            if (touching || w->gesture.load() || now - w->gestureEnd.load() < kQuietMs) continue;
            if (ignore && ignore (w->id)) continue;
            moved.push_back (w.get());
        }
        if (active && ! moved.empty() && changed < kPresetLoad)
            for (auto* w : moved) light (*w, now);
        // Drop faded ones; repaint only while something is showing.
        hits_.erase (std::remove_if (hits_.begin(), hits_.end(), [&] (const Hit& h) { return alpha (h, now) <= 0.0f; }), hits_.end());
        if (! hits_.empty() || wasShowing_) repaint();
        wasShowing_ = ! hits_.empty();
    }

    void light (Watch& w, double now)
    {
        auto* target = find (root_, w.id, true);
        if (target == nullptr)
            if (auto* hidden = find (root_, w.id, false); hidden != nullptr && onReveal)
            {
                onReveal (w.id, *hidden);
                target = visible (*hidden) ? hidden : nullptr;
            }
            else if (hidden == nullptr && onMissing)
            {
                onMissing (w.id);
                target = find (root_, w.id, true);
            }
        hits_.erase (std::remove_if (hits_.begin(), hits_.end(), [&] (const Hit& h) { return h.id == w.id; }), hits_.end());
        Hit h;
        h.id = w.id;
        h.name = w.param->getName (40);
        h.value = w.param->getCurrentValueAsText();
        const auto unit = w.param->getLabel();
        if (unit.isNotEmpty() && ! h.value.endsWith (unit)) h.value << " " << unit;
        h.target = target;
        h.at = now;
        hits_.push_back (h);
        while ((int) hits_.size() > kMaxTags) hits_.erase (hits_.begin());
        toFront (false);
    }

    juce::Component& root_;
    std::vector<std::unique_ptr<Watch>> watches_;
    std::vector<Hit> hits_;
    bool wasShowing_ = false;
};
} // namespace lvrs
