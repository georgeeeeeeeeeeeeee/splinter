#include "ui/BufferView.h"

namespace fault::ui
{
void BufferView::tick()
{
    const auto& st = engine.status();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastMs > 0.0 ? std::clamp ((now - lastMs) * 0.001, 0.0, 0.2) : 1.0 / 60.0;
    lastMs = now;

    wStart = st.windowStart.load (std::memory_order_relaxed);
    wLen = st.windowLength.load (std::memory_order_relaxed);
    written = (double) st.written.load (std::memory_order_relaxed);
    frozen = st.frozen.load (std::memory_order_relaxed);
    for (size_t ch = 0; ch < 2; ++ch)
    {
        head[ch] = st.head[ch].load (std::memory_order_relaxed);
        speed[ch] = st.headSpeed[ch].load (std::memory_order_relaxed);
        duty[ch] = st.duty[ch].load (std::memory_order_relaxed);
        pieces[ch] = std::max (1, st.pieces[ch].load (std::memory_order_relaxed));
        slice[ch] = st.sliceIndex[ch].load (std::memory_order_relaxed);
        const auto j = st.jumps[ch].load (std::memory_order_relaxed);
        if (j != jumps[ch]) jumpFlash[ch] = 1.0f;
        jumps[ch] = j;
        jumpFlash[ch] *= (float) std::exp (-dt / 0.12);
    }
    const auto t = engine.ticksWritten();
    if (t != ticks) tickFlash = 1.0f;
    ticks = t;
    tickFlash *= (float) std::exp (-dt / 0.09);
    const auto c = st.clips.load (std::memory_order_relaxed);
    if (c != clips) clipFlash = 1.0f;
    clips = c;
    clipFlash = std::max (0.0f, clipFlash - (float) dt * 2.0f);

    // The view grows at once with the window and shrinks slowly, so the older audio stays visible (as a
    // ghost) for a moment when Time shrinks.
    if (wLen >= viewLen || viewLen <= 0.0) viewLen = wLen;
    else viewLen += (wLen - viewLen) * (1.0 - std::exp (-dt / 0.9));
    repaint();
}

void BufferView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (frozen ? colours::freeze : colours::line.interpolatedWith (colours::text, 0.25f * tickFlash));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, frozen ? 1.6f : 1.0f);

    auto inner = r.reduced (10.0f, 22.0f);
    const float laneH = inner.getHeight() * 0.5f;
    if (wLen < 2.0)
    {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Waiting for the first tick", inner, juce::Justification::centred);
    }
    else
    {
        drawLane (g, inner.withHeight (laneH).reduced (0, 2), 0);
        drawLane (g, inner.withTrimmedTop (laneH).reduced (0, 2), 1);
    }

    // Caption: what the window is.
    const double sr = std::max (1.0, engine.sampleRate());
    juce::String info = (clockText ? clockText() + "   " : juce::String()) + juce::String (wLen / sr, wLen / sr < 1.0 ? 3 : 2) + " s window";
    if (pieces[0] > 1 || pieces[1] > 1)
        info << "   " << pieces[0] << (pieces[1] != pieces[0] ? "/" + juce::String (pieces[1]) : juce::String()) << " slices";
    drawCaption (g, info, getLocalBounds().reduced (12, 4).removeFromTop (16), colours::dim);
    const auto fmt = [] (float s) { return (s < 0.0f ? "-" : "") + juce::String (std::abs (s), 2) + "x"; };
    drawCaption (g, "L " + fmt (speed[0]) + "   R " + fmt (speed[1]), getLocalBounds().reduced (12, 4).removeFromBottom (16), colours::dim,
                 juce::Justification::centredRight);
    if (clipFlash > 0.0f)
        drawCaption (g, "Clip", getLocalBounds().reduced (12, 4).removeFromBottom (16), colours::corrupt.withAlpha (std::min (1.0f, 2.0f * clipFlash)),
                     juce::Justification::centredLeft);
    if (frozen)
        drawCaption (g, "Frozen", getLocalBounds().reduced (12, 4).removeFromTop (16), colours::freeze, juce::Justification::centredRight);
    // The tick lamp.
    g.setColour (colours::text.withAlpha (0.15f + 0.85f * tickFlash));
    g.fillEllipse (r.getRight() - 14.0f, r.getBottom() - 30.0f, 6.0f, 6.0f);
}

void BufferView::drawLane (juce::Graphics& g, juce::Rectangle<float> lane, int ch)
{
    const auto c = (size_t) ch;
    const double viewEnd = wStart + wLen, viewStart = viewEnd - std::max (viewLen, wLen);
    const double spp = (viewEnd - viewStart) / std::max (1.0f, lane.getWidth());
    const auto xOf = [&] (double s) { return lane.getX() + (float) ((s - viewStart) / spp); };
    const float mid = lane.getCentreY(), half = lane.getHeight() * 0.48f;

    // The active slice: a lighter band, flashing on a jump; silence hatched inside it.
    const double sliceLen = wLen / pieces[c];
    const double s0 = wStart + sliceLen * std::clamp (slice[c], 0, pieces[c] - 1), s1 = s0 + sliceLen;
    const auto band = juce::Rectangle<float>::leftTopRightBottom (xOf (s0), lane.getY(), std::max (xOf (s0) + 1.0f, xOf (s1)), lane.getBottom());
    g.setColour (colours::raised.interpolatedWith (colours::brk, 0.35f * jumpFlash[c]));
    g.fillRect (band);
    if (tickFlash > 0.01f)
    {
        g.setColour (colours::text.withAlpha (0.07f * tickFlash));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (wStart), lane.getY(), xOf (viewEnd), lane.getBottom()));
    }

    // Waveform: min / max per pixel column from the overview. Older than the window: a ghost.
    const auto& ov = engine.overview();
    const int64_t held0 = ov.oldest(), heldN = ov.count();
    float b4[4];
    const int lo = ch == 0 ? 0 : 2;
    for (int x = 0; x < (int) lane.getWidth(); ++x)
    {
        const double a = viewStart + x * spp, b = a + spp;
        auto k0 = (int64_t) std::floor (a / dsp::Overview::kBucket), k1 = (int64_t) std::floor (b / dsp::Overview::kBucket);
        k0 = std::max (k0, held0);
        k1 = std::min (std::max (k1, k0), heldN - 1);
        if (k0 > k1) continue;
        float mn = 1.0f, mx = -1.0f;
        for (int64_t k = k0; k <= k1; k += std::max<int64_t> (1, (k1 - k0) / 48))   // at most ~48 reads a column
        {
            ov.bucket (k, b4);
            mn = std::min (mn, b4[lo]);
            mx = std::max (mx, b4[lo + 1]);
        }
        if (mx < mn) continue;
        const bool inWindow = a >= wStart - spp;
        g.setColour (inWindow ? colours::buffer.withAlpha (0.9f) : colours::buffer.withAlpha (0.22f));
        const float y0 = mid - std::clamp (mx, -1.0f, 1.0f) * half, y1 = mid - std::clamp (mn, -1.0f, 1.0f) * half;
        g.fillRect (lane.getX() + (float) x, y0, 1.0f, std::max (1.0f, y1 - y0));
    }

    // Silence in the active slice (the end of each repeat going forward, the start in reverse).
    if (duty[c] > 0.001f)
    {
        const bool rev = speed[c] < 0.0f;
        const double q0 = rev ? s0 : s1 - duty[c] * sliceLen, q1 = rev ? s0 + duty[c] * sliceLen : s1;
        auto hatch = juce::Rectangle<float>::leftTopRightBottom (xOf (q0), lane.getY(), xOf (q1), lane.getBottom());
        g.saveState();
        g.reduceClipRegion (hatch.toNearestInt());
        g.setColour (colours::corrupt.withAlpha (0.16f));
        g.fillRect (hatch);
        g.setColour (colours::corrupt.withAlpha (0.55f));
        for (float x = hatch.getX() - lane.getHeight(); x < hatch.getRight(); x += 7.0f)
            g.drawLine (x, lane.getBottom(), x + lane.getHeight(), lane.getY(), 1.0f);
        g.restoreState();
    }

    // Slice divisions; past 64 the slices are audio rate, so just say how many.
    g.setColour (colours::text.withAlpha (0.22f));
    if (pieces[c] <= 64)
        for (int k = 1; k < pieces[c]; ++k)
            g.drawVerticalLine ((int) xOf (wStart + sliceLen * k), lane.getY(), lane.getBottom());
    g.setColour (colours::text.withAlpha (0.45f));
    g.drawVerticalLine ((int) xOf (wStart), lane.getY(), lane.getBottom());   // window start (older audio to its left)

    // Playhead, coloured by direction, with an arrow.
    const float hx = xOf (head[c]);
    const auto pc = speed[c] < 0.0f ? colours::reverse : colours::text;
    g.setColour (pc);
    g.drawLine (hx, lane.getY(), hx, lane.getBottom(), 1.6f);
    juce::Path arrow;
    const float d = speed[c] < 0.0f ? -6.0f : 6.0f;
    arrow.addTriangle (hx, lane.getY(), hx, lane.getY() + 8.0f, hx + d, lane.getY() + 4.0f);
    g.fillPath (arrow);
    drawCaption (g, ch == 0 ? "L" : "R", lane.toNearestInt().removeFromLeft (14).removeFromBottom (14), colours::faint);
}
} // namespace fault::ui
