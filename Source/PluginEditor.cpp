#include "PluginEditor.h"

using namespace theme;

namespace
{
    constexpr int kWidth = 780, kHeight = 712;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float pi = juce::MathConstants<float>::pi;

    juce::Font sans (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font mono (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    float param (VinilProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
    int choice (VinilProcessor& p, const char* id) { return (int) std::round (param (p, id)); }

    void label (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size = 10.5f,
                juce::Justification j = juce::Justification::centred, float alpha = 0.85f)
    {
        g.setFont (sans (size));
        g.setColour (gold.withAlpha (alpha));
        g.drawText (s, r, j);
    }

    // one digit of the seven-segment readout (mask bits a b c d e f g)
    void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int mask)
    {
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight(), t = w * 0.18f;
        const juce::Rectangle<float> seg[7] = {
            { x + t * 0.8f, y, w - t * 1.6f, t },
            { x + w - t, y + t * 0.8f, t, h * 0.5f - t * 1.3f },
            { x + w - t, y + h * 0.5f + t * 0.5f, t, h * 0.5f - t * 1.3f },
            { x + t * 0.8f, y + h - t, w - t * 1.6f, t },
            { x, y + h * 0.5f + t * 0.5f, t, h * 0.5f - t * 1.3f },
            { x, y + t * 0.8f, t, h * 0.5f - t * 1.3f },
            { x + t * 0.8f, y + h * 0.5f - t * 0.5f, w - t * 1.6f, t } };
        for (int k = 0; k < 7; ++k)
        {
            g.setColour ((mask >> k) & 1 ? goldHi : gold.withAlpha (0.09f));
            g.fillRoundedRectangle (seg[k], t * 0.4f);
        }
    }

    constexpr int digitMasks[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

    // the pads, in this order (indices into shapes::make)
    constexpr int padShapes[10] = { 0, 1, 3, 4, 5, 6, 7, 8, 10, 11 };

    void panelOutline (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto f = r.toFloat().reduced (1.0f);
        g.setColour (gold);
        g.drawRoundedRectangle (f, 18.0f, 2.2f);
        g.setColour (gold.withAlpha (0.35f));
        g.drawRoundedRectangle (f.reduced (5.0f), 14.0f, 1.0f);
    }
}

//==============================================================================
VinilLookAndFeel::VinilLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, navyLo);
    setColour (juce::PopupMenu::textColourId, gold);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, gold);
    setColour (juce::PopupMenu::highlightedTextColourId, navy);
    setColour (juce::ComboBox::textColourId, gold);
    setColour (juce::ComboBox::outlineColourId, gold);
    setColour (juce::ComboBox::backgroundColourId, navyLo);
    setColour (juce::TooltipWindow::backgroundColourId, navyLo);
    setColour (juce::TooltipWindow::textColourId, gold);
    setColour (juce::TooltipWindow::outlineColourId, gold);
    setColour (juce::BubbleComponent::backgroundColourId, navyLo);
    setColour (juce::BubbleComponent::outlineColourId, gold);
    setColour (juce::Slider::textBoxTextColourId, goldHi);
    setColour (juce::Slider::textBoxBackgroundColourId, navyLo);
    setColour (juce::Slider::textBoxOutlineColourId, gold);
    setColour (juce::Slider::textBoxHighlightColourId, gold);
    setColour (juce::TextEditor::textColourId, goldHi);
    setColour (juce::TextEditor::backgroundColourId, navyLo);
    setColour (juce::CaretComponent::caretColourId, goldHi);
}

juce::Font VinilLookAndFeel::getLabelFont (juce::Label&) { return sans (13.5f); }
juce::Font VinilLookAndFeel::getPopupMenuFont() { return sans (12.0f); }

// the value box of a slider: same rounded line-art as everything else
void VinilLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    auto r = l.getLocalBounds().toFloat().reduced (0.7f);
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
        r = r.withTrimmedLeft (8.0f);   // a gap between the slider frame and its value box
    g.setColour (navyLo);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (gold);
    g.drawRoundedRectangle (r, 9.0f, 1.4f);
    if (! l.isBeingEdited())
    {
        g.setFont (getLabelFont (l));
        g.setColour (goldHi);
        g.drawText (l.getText(), r.toNearestInt(), juce::Justification::centred, false);
    }
}

// x, y, w, h is the range the ball centre can travel (JUCE already insets it by the ball radius),
// so the track is exactly that range and the ball sits on its ends at minimum and maximum
void VinilLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
    const juce::Point<float> a = vertical ? juce::Point<float> (cx, (float) (y + h)) : juce::Point<float> ((float) x, cy);
    const juce::Point<float> b = vertical ? juce::Point<float> (cx, (float) y) : juce::Point<float> ((float) (x + w), cy);
    const juce::Point<float> thumb = vertical ? juce::Point<float> (cx, pos) : juce::Point<float> (pos, cy);

    // rounded frame around the whole slider
    const float pad = (float) getSliderThumbRadius (s);
    auto frame = vertical ? juce::Rectangle<float> ((float) x, (float) y - pad, (float) w, (float) h + 2.0f * pad).reduced (3.0f, 0.7f)
                          : juce::Rectangle<float> ((float) x - pad, (float) y, (float) w + 2.0f * pad, (float) h).reduced (0.7f);
    g.setColour (gold.withAlpha (0.75f));
    g.drawRoundedRectangle (frame, 12.0f, 1.4f);

    g.setColour (gold.withAlpha (0.3f));
    g.drawLine (juce::Line<float> (a, b), 3.0f);
    g.setColour (goldHi);
    g.drawLine (juce::Line<float> (a, thumb), 3.0f);

    if (s.getProperties().contains ("ticks"))   // a tick every 5 units
    {
        const int n = (int) std::round ((s.getMaximum() - s.getMinimum()) / 5.0);
        for (int i = 0; i <= n; ++i)
        {
            const float tx = a.x + (b.x - a.x) * (float) i / (float) juce::jmax (1, n);
            g.setColour (gold.withAlpha (0.6f));
            g.drawLine (tx, cy + 8.0f, tx, cy + 11.0f, 1.2f);
        }
    }

    g.setColour (navy);
    g.fillEllipse (thumb.x - 8.5f, thumb.y - 8.5f, 17.0f, 17.0f);
    g.setColour (goldHi);
    g.drawEllipse (thumb.x - 8.5f, thumb.y - 8.5f, 17.0f, 17.0f, 2.0f);
}

void VinilLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.7f);
    if (b.getToggleState())
        g.setColour (gold);
    else
        g.setColour (gold.withAlpha (down ? 0.35f : highlighted ? 0.16f : 0.0f));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (gold);
    g.drawRoundedRectangle (r, 9.0f, 1.4f);
}

void VinilLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (sans (10.5f));
    g.setColour (b.getToggleState() ? navy : gold);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void PadButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? gold : gold.withAlpha (highlighted ? 0.18f : 0.06f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (gold);
    g.drawRoundedRectangle (r, 10.0f, 1.5f);
    g.setColour (gold.withAlpha (0.4f));
    g.drawRoundedRectangle (r.reduced (4.0f), 7.0f, 0.8f);
    g.setFont (sans (10.5f));
    g.setColour (down ? navy : gold);
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
CurveView::CurveView (VinilProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

// SNAP: time in steps of half a second, speed in steps of 0.25x
float CurveView::snapX (float x) const
{
    const float step = 0.5f / (float) proc.getCycleSeconds();
    return juce::jlimit (0.0f, 1.0f, std::round (x / step) * step);
}

float CurveView::snapU (float u)
{
    return juce::jlimit (-1.0f, 1.0f, std::round (u * VinilProcessor::speedRange / 0.25f) * 0.25f / VinilProcessor::speedRange);
}

void CurveView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (navyLo);
    g.fillRoundedRectangle (all, 12.0f);

    const float range = VinilProcessor::speedRange;
    const bool smooth = param (proc, "smooth") > 0.5f;
    const auto wave = area();
    const float cy = uToPix (0.0f);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (wave.toNearestInt());

        // ---- speed grid ----
        g.setFont (mono (9.5f));
        for (int s = -(int) range; s <= (int) range; ++s)
        {
            const float y = uToPix ((float) s / range);
            g.setColour (gold.withAlpha (s == 0 ? 0.55f : 0.16f));
            if (s == 0)
            {
                const float dash[] = { 5.0f, 4.0f };
                g.drawDashedLine (juce::Line<float> (wave.getX(), y, wave.getRight(), y), dash, 2, 1.0f);
            }
            else
                g.drawHorizontalLine ((int) y, wave.getX(), wave.getRight());
            g.setColour (gold.withAlpha (0.6f));
            g.drawText ((s > 0 ? "+" : "") + juce::String (s) + "x", (int) wave.getX() + 3, (int) y - 11, 30, 10, juce::Justification::left);
        }

        // ---- time axis in seconds ----
        {
            const double dur = proc.getCycleSeconds();
            const double step = dur <= 10.0 ? 1.0 : dur <= 20.0 ? 2.0 : 5.0;
            g.setFont (mono (9.5f));
            g.setColour (gold.withAlpha (0.65f));
            for (double t = 0.0; t < dur - 0.01; t += step)
            {
                const float x = xToPix ((float) (t / dur));
                g.setColour (gold.withAlpha (0.12f));
                g.drawVerticalLine ((int) x, wave.getY(), wave.getBottom());
                g.setColour (gold.withAlpha (0.65f));
                g.drawText (juce::String ((int) t) + "s", (int) x + 3, (int) wave.getBottom() - 13, 30, 11, juce::Justification::left);
            }
            g.drawText (juce::String (dur, 0) + "s", (int) wave.getRight() - 40, (int) wave.getBottom() - 13, 37, 11, juce::Justification::right);
        }

        // ---- waveform of the incoming audio ----
        g.setColour (gold.withAlpha (0.3f));
        const int cols = (int) wave.getWidth();
        const float hh = wave.getHeight() * 0.42f;
        for (int x = 0; x < cols; x += 2)
        {
            const float a = proc.liveWave[(size_t) juce::jmin (VinilProcessor::waveBins - 1, x * VinilProcessor::waveBins / juce::jmax (1, cols))].load (std::memory_order_relaxed);
            g.fillRect (wave.getX() + (float) x, cy - a * hh, 1.6f, juce::jmax (1.0f, a * hh * 2.0f));
        }

        // ---- the curve ----
        float lut[512];
        proc.curve.bake (lut, 512, smooth);
        juce::Path path;
        for (int i = 0; i < 512; ++i)
        {
            const float x = xToPix ((float) i / 511.0f), y = uToPix (lut[i]);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        juce::Path fillP (path);
        fillP.lineTo (xToPix (1.0f), cy);
        fillP.lineTo (xToPix (0.0f), cy);
        fillP.closeSubPath();
        g.setColour (gold.withAlpha (0.18f));
        g.fillPath (fillP);
        g.setColour (goldHi);
        g.strokePath (path, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (size_t i = 0; i < proc.curve.pts.size(); ++i)
        {
            const auto& pt = proc.curve.pts[i];
            const juce::Point<float> p (xToPix (pt.x), uToPix (pt.y));
            g.setColour ((int) i == dragIndex ? goldHi : navy);
            g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
            g.setColour (goldHi);
            g.drawEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f, 1.6f);
        }

        // ---- playhead ----
        const float ph = proc.cyclePhase.load();
        const float x = xToPix (ph);
        g.setColour (goldHi.withAlpha (0.9f));
        g.drawVerticalLine ((int) x, wave.getY(), wave.getBottom());
        g.fillEllipse (x - 3.5f, uToPix (lut[juce::jlimit (0, 511, (int) (ph * 511.0f))]) - 3.5f, 7.0f, 7.0f);
    }

    if (hover.isNotEmpty())
    {
        g.setFont (mono (10.5f));
        g.setColour (goldHi);
        g.drawText (hover, wave.toNearestInt().reduced (8, 4), juce::Justification::topRight);
    }

    g.setColour (gold.withAlpha (0.55f));
    g.drawRoundedRectangle (all.reduced (0.5f), 12.0f, 1.2f);
}

int CurveView::hitPoint (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 10.0f;
    for (size_t i = 0; i < proc.curve.pts.size(); ++i)
    {
        const float d = p.getDistanceFrom ({ xToPix (proc.curve.pts[i].x), uToPix (proc.curve.pts[i].y) });
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

// returns nothing when the spot is too close to a neighbour; the new point is stored in the curve
void CurveView::addPoint (juce::Point<float> pixel, bool snap)
{
    auto& v = proc.curve.pts;
    float x = pixToX (pixel.x), u = pixToU (pixel.y);
    if (snap) { x = snapX (x); u = snapU (u); }
    auto it = std::upper_bound (v.begin(), v.end(), x, [] (float xv, const CurvePoint& p) { return xv < p.x; });
    const int at = (int) (it - v.begin());
    dragIndex = -1;
    if (at <= 0 || at >= (int) v.size()) return;
    if (x - v[(size_t) at - 1].x < CurveModel::minGap || v[(size_t) at].x - x < CurveModel::minGap) return;
    v.insert (it, { x, u });
    dragIndex = at;
    proc.curveEdited();
}

void CurveView::mouseDown (const juce::MouseEvent& e)
{
    if (! area().contains (e.position)) return;

    auto& v = proc.curve.pts;
    const int idx = hitPoint (e.position);
    const bool snap = (param (proc, "snap") > 0.5f) != e.mods.isShiftDown();

    if (e.mods.isPopupMenu())
    {
        const auto pos = e.position;
        const bool onPoint = idx > 0 && idx < (int) v.size() - 1;
        juce::PopupMenu m;
        if (onPoint)
            m.addItem (1, "Delete point");
        else if (idx < 0)
            m.addItem (2, "Add point here");
        m.addItem (3, "Reset curve to flat");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> ((int) e.getScreenX(), (int) e.getScreenY(), 1, 1)),
                         [this, idx, pos, snap] (int r)
        {
            if (r == 1 && idx > 0 && idx < (int) proc.curve.pts.size() - 1)
                proc.curve.pts.erase (proc.curve.pts.begin() + idx);
            else if (r == 2)
                addPoint (pos, snap);
            else if (r == 3)
                proc.curve.reset (1.0f / VinilProcessor::speedRange);
            else
                return;
            dragIndex = -1;
            proc.curveEdited();
        });
        return;
    }

    if (e.mods.isAltDown())
    {
        if (idx > 0 && idx < (int) v.size() - 1)
        {
            v.erase (v.begin() + idx);
            proc.curveEdited();
        }
        return;
    }

    if (idx >= 0) dragIndex = idx;
    else          addPoint (e.position, snap);
}

void CurveView::mouseDrag (const juce::MouseEvent& e)
{
    auto& v = proc.curve.pts;
    if (dragIndex < 0 || dragIndex >= (int) v.size()) return;

    const bool snap = (param (proc, "snap") > 0.5f) != e.mods.isShiftDown();
    float x = pixToX (e.position.x), u = pixToU (e.position.y);
    if (snap) { x = snapX (x); u = snapU (u); }
    auto& pt = v[(size_t) dragIndex];
    pt.y = u;
    if (dragIndex > 0 && dragIndex < (int) v.size() - 1)
        pt.x = juce::jlimit (v[(size_t) dragIndex - 1].x + CurveModel::minGap, v[(size_t) dragIndex + 1].x - CurveModel::minGap, x);
    proc.curveEdited();
}

void CurveView::mouseUp (const juce::MouseEvent&) { dragIndex = -1; }

void CurveView::mouseMove (const juce::MouseEvent& e)
{
    if (! area().contains (e.position)) { hover = {}; return; }
    const float x = pixToX (e.position.x), u = pixToU (e.position.y);
    const float speed = u * VinilProcessor::speedRange;
    hover = juce::String (x * (float) proc.getCycleSeconds(), 2) + " s   " + (speed >= 0.0f ? "+" : "") + juce::String (speed, 2) + "x";
}

void CurveView::mouseExit (const juce::MouseEvent&) { hover = {}; }

//==============================================================================
TurntableView::TurntableView (VinilProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    startTimerHz (30);
}

bool TurntableView::inside (juce::Point<float> p) const
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f);
    return p.getDistanceFrom (b.getCentre()) <= juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
}

float TurntableView::angleAt (juce::Point<float> p) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return std::atan2 (p.y - c.y, p.x - c.x);
}

void TurntableView::mouseDown (const juce::MouseEvent& e)
{
    if (! inside (e.position)) return;
    held = true;
    lastAngle = angleAt (e.position);
    proc.setPlatterGrabbed (true);
    repaint();
}

void TurntableView::mouseDrag (const juce::MouseEvent& e)
{
    if (! held) return;
    const float a = angleAt (e.position);
    float d = a - lastAngle;
    if (d > pi) d -= twoPi;
    if (d < -pi) d += twoPi;
    lastAngle = a;
    proc.addPlatterAngle (d);
}

void TurntableView::mouseUp (const juce::MouseEvent&)
{
    held = false;
    proc.setPlatterGrabbed (false);
    repaint();
}

void TurntableView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    proc.addPlatterAngle (-w.deltaY * 1.2f);
}

void TurntableView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f);
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float ang = proc.platterAngle.load();

    if (held)
    {
        g.setColour (gold.withAlpha (0.14f));
        g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    }

    g.setColour (gold);
    g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 2.4f);
    g.setColour (gold.withAlpha (0.45f));
    g.drawEllipse (c.x - R + 7.0f, c.y - R + 7.0f, (R - 7.0f) * 2.0f, (R - 7.0f) * 2.0f, 1.0f);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (ang, c.x, c.y));

        // grooves
        int k = 0;
        for (float r = R * 0.40f; r < R * 0.90f; r += 3.6f, ++k)
        {
            g.setColour (gold.withAlpha (k % 4 == 0 ? 0.26f : 0.12f));
            g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.9f);
        }
        for (float r : { R * 0.60f, R * 0.77f })
        {
            g.setColour (gold.withAlpha (0.55f));
            g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.6f);
        }

        // strobe ticks along the rim
        for (int i = 0; i < 72; ++i)
        {
            const float a = twoPi * (float) i / 72.0f;
            const float r0 = R * (i % 6 == 0 ? 0.915f : 0.945f), r1 = R * 0.97f;
            g.setColour (gold.withAlpha (i % 6 == 0 ? 0.9f : 0.5f));
            g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, i % 6 == 0 ? 1.8f : 1.0f);
        }

        // shine wedges: they make the rotation easy to read
        for (int s = 0; s < 2; ++s)
        {
            juce::Path w;
            const float rr = R * 0.84f, a0 = (float) s * pi - 0.28f, a1 = (float) s * pi + 0.28f;
            w.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
            g.setColour (gold.withAlpha (0.3f));
            g.strokePath (w, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // label
        const float rl = R * 0.30f;
        g.setColour (navyHi);
        g.fillEllipse (c.x - rl, c.y - rl, rl * 2.0f, rl * 2.0f);
        g.setColour (gold);
        g.drawEllipse (c.x - rl, c.y - rl, rl * 2.0f, rl * 2.0f, 2.0f);
        g.drawEllipse (c.x - rl * 0.82f, c.y - rl * 0.82f, rl * 1.64f, rl * 1.64f, 0.8f);
        g.setFont (sans (R * 0.105f));
        g.drawText ("VINIL", juce::Rectangle<float> (c.x - rl, c.y - rl * 0.62f, rl * 2.0f, rl * 0.5f), juce::Justification::centred);
        g.setFont (mono (R * 0.062f));
        g.drawText ("33 1/3 RPM", juce::Rectangle<float> (c.x - rl, c.y + rl * 0.26f, rl * 2.0f, rl * 0.4f), juce::Justification::centred);
        juce::Path notch;
        notch.addTriangle (c.x, c.y - rl + 2.0f, c.x - 5.0f, c.y - rl + 12.0f, c.x + 5.0f, c.y - rl + 12.0f);
        g.fillPath (notch);
    }

    // spindle
    g.setColour (goldHi);
    g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
LcdView::LcdView (VinilProcessor& p) : proc (p) { startTimerHz (20); }

void LcdView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (navyLo);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (gold);
    g.drawRoundedRectangle (r.reduced (0.8f), 12.0f, 1.6f);

    const float v = juce::jlimit (-9.99f, 9.99f, proc.speedNow.load());
    const int hundredths = (int) std::round (std::abs (v) * 100.0f);
    const int d0 = hundredths / 100, d1 = (hundredths / 10) % 10, d2 = hundredths % 10;

    const float h = r.getHeight() - 28.0f, y = r.getY() + 10.0f;
    float x = r.getX() + 12.0f;
    drawDigit (g, { x, y, 16.0f, h }, v < -0.004f ? 0x40 : 0);
    x += 22.0f;
    drawDigit (g, { x, y, 24.0f, h }, digitMasks[d0]);
    x += 28.0f;
    g.setColour (goldHi);
    g.fillRect (x, y + h - 4.0f, 4.0f, 4.0f);
    x += 9.0f;
    drawDigit (g, { x, y, 24.0f, h }, digitMasks[d1]);
    x += 30.0f;
    drawDigit (g, { x, y, 24.0f, h }, digitMasks[d2]);
    g.setFont (mono (11.0f, true));
    g.setColour (gold);
    g.drawText ("x", juce::Rectangle<float> (x + 26.0f, y + h - 12.0f, 14.0f, 12.0f), juce::Justification::centred);
    g.setFont (sans (8.5f));
    g.drawText ("PLATTER SPEED", juce::Rectangle<float> (r.getX() + 8.0f, r.getBottom() - 16.0f, 150.0f, 12.0f), juce::Justification::centredLeft);

    // tempo of the project
    const auto bpmArea = r.withTrimmedLeft (200.0f).reduced (8.0f, 6.0f);
    g.setFont (sans (13.0f));
    g.setColour (gold.withAlpha (0.8f));
    g.drawText ("BPM", bpmArea.withHeight (16.0f), juce::Justification::centredRight);
    g.setFont (mono (34.0f, true));
    g.setColour (goldHi);
    g.drawText (juce::String (proc.bpmNow.load(), 1), bpmArea.withTrimmedTop (16.0f), juce::Justification::centredRight);
}

//==============================================================================
VinilEditor::VinilEditor (VinilProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), curveView (p), turntable (p), lcd (p)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (curveView);
    addAndMakeVisible (turntable);
    addAndMakeVisible (lcd);

    loopSlider.getProperties().set ("ticks", true);
    loopSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    loopSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 26);
    loopSlider.setTextValueSuffix (" s");
    loopSlider.setNumDecimalPlacesToDisplay (0);
    addAndMakeVisible (loopSlider);
    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "loop", loopSlider);

    for (auto* b : { &snapButton, &smoothButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    snapAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("snap"), snapButton);
    smoothAtt = std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter ("smooth"), smoothButton);

    const char* ids[] = { "mix", "weight", "drag", "preroll" };
    const char* names[] = { "MIX", "WEIGHT", "DRAG", "PREROLL" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.name = names[i];
        k.slider.setSliderStyle (juce::Slider::LinearVertical);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider.setPopupDisplayEnabled (true, true, this);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids[i], k.slider);
        if (auto* prm = proc.apvts.getParameter (ids[i]))   // double-click = back to the default
            k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        addAndMakeVisible (k.slider);
    }

    for (int i = 0; i < 10; ++i)
    {
        auto* pad = shapePads.add (new PadButton (shapes::names[padShapes[i]]));
        pad->onClick = [this, i] { applyShape (padShapes[i]); };
        addAndMakeVisible (pad);
    }

    setSize (kWidth, kHeight);
}

VinilEditor::~VinilEditor()
{
    setLookAndFeel (nullptr);
}

void VinilEditor::applyShape (int idx)
{
    proc.curve.pts = shapes::make (idx, VinilProcessor::speedRange);
    proc.curveEdited();
}

void VinilEditor::resized()
{
    topPanel  = { 16, 16, 748, 306 };
    turnPanel = { 16, 338, 372, 358 };
    mpcPanel  = { 402, 338, 362, 358 };
    captions.clear();
    captionTexts.clear();
    auto cap = [this] (const juce::String& t, juce::Rectangle<int> r) { captions.add (r); captionTexts.add (t); };

    // header row
    cap ("LOOP", { 30, 28, 40, 26 });     loopSlider.setBounds (74, 28, 676, 26);

    curveView.setBounds (30, 62, 720, 206);

    // footer row
    snapButton.setBounds (30, 278, 56, 26);
    smoothButton.setBounds (90, 278, 70, 26);

    // turntable
    turntable.setBounds (turnPanel.getX() + 14, turnPanel.getY() + 14, 292, 292);
    barArea = { turnPanel.getX() + 16, turnPanel.getY() + 316, turnPanel.getWidth() - 32, 30 };

    // MPC
    lcd.setBounds (mpcPanel.getX() + 16, mpcPanel.getY() + 16, 330, 76);
    // four vertical sliders on the left, the shape pads in two columns on the right
    const int sx = mpcPanel.getX() + 16, sy = mpcPanel.getY() + 112, cell = 48;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i].slider.setBounds (sx + (int) i * cell, sy, cell, 218);
        cap (knobs[i].name, { sx + (int) i * cell - 2, sy + 220, cell + 4, 16 });
    }
    const int px = sx + 4 * cell + 12, padW = 59, padH = 38, gapX = 8, gapY = 7;
    for (int i = 0; i < 10; ++i)
        shapePads[i]->setBounds (px + (i % 2) * (padW + gapX), sy + (i / 2) * (padH + gapY), padW, padH);
}

void VinilEditor::paint (juce::Graphics& g)
{
    g.fillAll (navy);

    panelOutline (g, topPanel);
    panelOutline (g, turnPanel);
    panelOutline (g, mpcPanel);

    for (int i = 0; i < captions.size(); ++i)
        label (g, captionTexts[i], captions[i], 9.5f);

    // name plate
    const juce::Rectangle<float> plate (500.0f, 278.0f, 250.0f, 26.0f);
    g.setColour (gold);
    g.drawRoundedRectangle (plate.reduced (1.0f), 10.0f, 1.4f);
    g.setFont (sans (12.5f));
    g.drawText ("DEVIS LABORATORY VINIL", plate.toNearestInt(), juce::Justification::centred);

    // deck bar of the turntable
    g.setColour (gold);
    g.drawRoundedRectangle (barArea.toFloat(), 14.0f, 1.6f);
    g.setFont (mono (11.5f, true));
    g.setColour (gold.withAlpha (0.8f));
    g.drawText ("33 1/3 RPM", barArea, juce::Justification::centred);
}

void VinilEditor::paintOverChildren (juce::Graphics& g)
{
    // tonearm
    const auto T = turnPanel;
    const juce::Point<float> pivot ((float) T.getRight() - 40.0f, (float) T.getY() + 52.0f);
    const juce::Point<float> elbow (pivot.x - 4.0f, pivot.y + 112.0f);
    const juce::Point<float> head ((float) T.getX() + 268.0f, (float) T.getY() + 238.0f);

    g.setColour (navy);
    g.fillEllipse (pivot.x - 17.0f, pivot.y - 17.0f, 34.0f, 34.0f);
    g.setColour (gold);
    g.drawEllipse (pivot.x - 17.0f, pivot.y - 17.0f, 34.0f, 34.0f, 2.0f);
    g.fillEllipse (pivot.x - 6.0f, pivot.y - 6.0f, 12.0f, 12.0f);
    g.drawRoundedRectangle (pivot.x - 7.0f, pivot.y - 42.0f, 14.0f, 24.0f, 3.0f, 1.6f);

    juce::Path arm;
    arm.startNewSubPath (pivot);
    arm.lineTo (elbow);
    arm.lineTo (head);
    g.setColour (navy);
    g.strokePath (arm, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (gold);
    g.strokePath (arm, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float a = std::atan2 (head.y - elbow.y, head.x - elbow.x);
    juce::Path shell;
    shell.addRoundedRectangle (-4.0f, -6.0f, 24.0f, 12.0f, 2.0f);
    shell.applyTransform (juce::AffineTransform::rotation (a).translated (head.x - std::cos (a) * 2.0f, head.y - std::sin (a) * 2.0f));
    g.setColour (navy);
    g.fillPath (shell);
    g.setColour (gold);
    g.strokePath (shell, juce::PathStrokeType (1.8f));
}
