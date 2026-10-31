#include "Display.h"

namespace audiolab
{
using juce::Colour;
using juce::Graphics;
using juce::Path;
using juce::Rectangle;

static inline Colour A (Colour c, float a) { return c.withMultipliedAlpha (a); }

static void rr (Path& p, float x, float y, float w, float h, float r)
{
    r = std::min ({ r, w / 2, h / 2 });
    p.addRoundedRectangle (x, y, w, h, r);
}

static void dashedLine (Graphics& g, Path line, float width, float on, float off)
{
    Path d; const float dl[] = { on, off };
    juce::PathStrokeType (width).createDashedStroke (d, line, dl, 2);
    g.fillPath (d);
}

CompDisplay::CompDisplay (Theme& t, AudioLabCompProcessor& p) : theme (t), proc (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (60);
}

void CompDisplay::setView (int v)
{
    view = v;
    actIn.clear(); actOut.clear(); actGR.clear();
    repaint();
}

double CompDisplay::curve (double di) const
{
    const double r = proc.getParam (params::ratio);
    return CompressorCore::curve (di, proc.getParam (params::thr), r >= 20 ? 100 : r,   // ∞:1 = 100:1, comme le moteur
                                  proc.getParam (params::knee), proc.getParam (params::makeup));
}

void CompDisplay::timerCallback()
{
    // Même lissage que cMeters() dans Audio Lab
    const auto m = proc.readMeters();
    if (m.active)
    {
        smIn += (m.inDb - smIn) * 0.25f;
        smGR += (m.grDb - smGR) * 0.3f;
        smOut += (m.outDb - smOut) * 0.25f;
    }
    else
    {
        smIn += (-80 - smIn) * 0.06f; smGR += (0 - smGR) * 0.1f; smOut += (-80 - smOut) * 0.06f;
    }
    cur = { smIn, std::max (0.0f, smGR), smOut };

    // VU à aiguille : le texte suit la GR (60 ms) ; l'aiguille suit une réponse VU normalisée
    // du 2e ordre (99 % de la déviation en 300 ms, dépassement ≈ 1,3 %).
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double dt = std::min (0.1, lastT > 0 ? now - lastT : 1.0 / 60);
    lastT = now;
    vuGr += (cur.gr - vuGr) * (1 - std::exp (-dt / 0.06));
    const double wn = 13.42, z = 0.81, target = -cur.gr;
    for (double t = 0; t < dt; t += 0.001)
    {
        const double h1 = std::min (0.001, dt - t);
        const double acc = wn * wn * (target - vuPos) - 2 * z * wn * vuVel;
        vuVel += acc * h1; vuPos += vuVel * h1;
    }
    if (view == activity)
    {
        actIn.push_back (cur.in); actOut.push_back (cur.out); actGR.push_back (cur.gr);
        const size_t N = (size_t) std::max (10, getWidth() - 40);
        while (actIn.size() > N) { actIn.erase (actIn.begin()); actOut.erase (actOut.begin()); actGR.erase (actGR.begin()); }
    }
    repaint();
}

void CompDisplay::paint (Graphics& g)
{
    const auto& p = theme.pal();
    const float W = (float) getWidth(), H = (float) getHeight();
    Path bg; rr (bg, 0.5f, 0.5f, W - 1, H - 1, 10);
    g.setColour (p.cv); g.fillPath (bg);
    g.setColour (p.border); g.strokePath (bg, juce::PathStrokeType (1));
    g.saveState();
    g.reduceClipRegion (bg);
    if (view == transfer) drawTransfer (g, W, H, cur);
    else if (view == activity) drawActivity (g, W, H, cur);
    else drawMeters (g, W, H, cur);
    g.restoreState();
}

// Barre de mesure : piste arrondie, dégradé, maintien de crête, repère de seuil
void CompDisplay::meterBar (Graphics& g, const juce::String& key, float x, float y, float w, float h, float norm,
                            bool gr, const juce::String& label, const juce::String& value, float thrNorm)
{
    const auto& p = theme.pal();
    norm = juce::jlimit (0.0f, 1.0f, norm);
    Path track; rr (track, x, y, w, h, w / 2);
    g.setColour (p.cv2); g.fillPath (track);
    g.setColour (p.grid); g.strokePath (track, juce::PathStrokeType (1));
    g.saveState();
    g.reduceClipRegion (track);
    if (gr) { g.setColour (p.orange); g.fillRect (x, y, w, norm * h); }
    else
    {
        juce::ColourGradient grad (p.red, 0, y, p.green, 0, y + h, false);
        grad.addColour (0.18, p.yellow); grad.addColour (0.4, p.green);
        g.setGradientFill (grad); g.fillRect (x, y + h * (1 - norm), w, h * norm);
    }
    // maintien de crête
    auto& pk = peaks[key];
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (norm >= pk.v || now - pk.t > 1200) { pk.v = norm; pk.t = now; }
    else pk.v = std::max (norm, pk.v - 0.004f);
    const float py = gr ? y + pk.v * h : y + h * (1 - pk.v);
    if (pk.v > 0.01f) { g.setColour (gr ? p.orange : (pk.v > 0.85f ? p.red : p.txt2)); g.fillRect (x, py - 1, w, 2.0f); }
    if (thrNorm >= 0) { const float ty = y + h * (1 - thrNorm); g.setColour (p.accent); g.fillRect (x - 2, ty - 1, w + 4, 2.0f); }
    g.restoreState();
    g.setColour (p.txt); g.setFont (theme.ui (10, 600));
    g.drawText (label, Rectangle<float> (x - 20, y - 18, w + 40, 12), juce::Justification::centredBottom, false);
    g.setColour (p.txt2); g.setFont (theme.mono (9));
    g.drawText (value, Rectangle<float> (x - 24, y + h + 4, w + 48, 12), juce::Justification::centredTop, false);
}

// ── Vue Transfert ──
void CompDisplay::drawTransfer (Graphics& g, float W, float H, const M& m)
{
    const auto& p = theme.pal();
    const float mW = 14, gap = 22, pT = 22, pB = 24, pL = 40;
    const float metersW = 3 * mW + 2 * gap;
    // Courbe de transfert dans un carré (1 dB en entrée = 1 dB en sortie à l'écran), centré verticalement
    const float availW = W - pL - metersW - 36, availH = H - pT - pB;
    const float side = std::min (availW, availH);
    const float cX = pL, cY = pT + (availH - side) / 2, cW = side, cH = side;
    const float lo = -60;
    auto X = [&] (double d) { return (float) (cX + (d - lo) / (0 - lo) * cW); };
    auto Y = [&] (double d) { return (float) (cY + cH - (d - lo) / (0 - lo) * cH); };
    const float thr = proc.getParam (params::thr), kn = proc.getParam (params::knee);

    Path zone; rr (zone, cX, cY, cW, cH, 8);
    g.setColour (p.cv2); g.fillPath (zone);
    g.saveState(); g.reduceClipRegion (zone);
    for (int db = -60; db <= 0; db += 12)
    {
        g.setColour (db == 0 ? p.grid2 : p.grid);
        g.drawLine (X (db), cY, X (db), cY + cH, 1); g.drawLine (cX, Y (db), cX + cW, Y (db), 1);
    }
    // knee
    const float hk = kn / 2;
    g.setColour (A (p.accent, 0.07f)); g.fillRect (X (thr - hk), cY, X (thr + hk) - X (thr - hk), cH);
    g.setColour (A (p.accent, 0.5f));
    { Path l; l.startNewSubPath (X (thr), cY); l.lineTo (X (thr), cY + cH); dashedLine (g, l, 1, 4, 4); }
    // unité
    g.setColour (p.grid2);
    { Path l; l.startNewSubPath (X (lo), Y (lo)); l.lineTo (X (0), Y (0)); dashedLine (g, l, 1, 4, 4); }
    // courbe + remplissage
    Path c, fill;
    for (int xi = 0; xi <= (int) cW; ++xi)
    {
        const double di = lo + xi / cW * (0 - lo);
        const float y = Y (std::max ((double) lo, std::min (6.0, curve (di))));
        if (xi == 0) { c.startNewSubPath (cX, y); fill.startNewSubPath (cX, Y (lo)); fill.lineTo (cX, y); }
        else { c.lineTo (cX + xi, y); fill.lineTo (cX + xi, y); }
    }
    fill.lineTo (cX + cW, cY + cH); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (A (p.accent, 0.25f), 0, cY, A (p.accent, 0.02f), 0, cY + cH, false));
    g.fillPath (fill);
    const juce::PathStrokeType st (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (A (p.accent, 0.12f)); g.strokePath (c, juce::PathStrokeType (9, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (A (p.accent, 0.2f)); g.strokePath (c, juce::PathStrokeType (5, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (p.accent); g.strokePath (c, st);
    // point en direct + repères
    const double di = std::max ((double) lo, std::min (0.0, (double) m.in));
    const double dout = std::max ((double) lo, std::min (6.0, curve (di)));
    const float dx = X (di), dyy = Y (dout);
    g.setColour (A (p.orange, 0.5f));
    { Path l; l.startNewSubPath (dx, cY + cH); l.lineTo (dx, dyy); l.lineTo (cX, dyy); dashedLine (g, l, 1, 3, 3); }
    g.setColour (A (p.orange, 0.25f)); g.fillEllipse (dx - 9, dyy - 9, 18, 18);
    g.setColour (p.orange); g.fillEllipse (dx - 5, dyy - 5, 10, 10);
    g.setColour (juce::Colours::white); g.drawEllipse (dx - 5, dyy - 5, 10, 10, 1.5f);
    g.restoreState();
    // axes
    g.setColour (p.txt); g.setFont (theme.mono (10));
    for (int db = -60; db <= 0; db += 12)
    {
        g.drawText (juce::String (db), Rectangle<float> (X (db) - 20, cY + cH + 5, 40, 12), juce::Justification::centredTop, false);
        g.drawText (juce::String (db), Rectangle<float> (cX - 46, Y (db) - 6, 40, 12), juce::Justification::centredRight, false);
    }
    // mètres
    const float mx = cX + cW + 28, mY = cY, mH = cH;
    meterBar (g, "c-in", mx, mY, mW, mH, (m.in + 60) / 60, false, "In", juce::String (m.in, 1), (thr + 60) / 60);
    meterBar (g, "c-gr", mx + mW + gap, mY, mW, mH, m.gr / 24, true, "GR", "-" + juce::String (m.gr, 1));
    meterBar (g, "c-out", mx + 2 * (mW + gap), mY, mW, mH, (m.out + 60) / 60, false, "Out", juce::String (m.out, 1));
}

// ── Vue Activité ──
void CompDisplay::drawActivity (Graphics& g, float W, float H, const M& m)
{
    const auto& p = theme.pal();
    const float pL = 36, pR = 8, pT = 22, pB = 8;
    const float aX = pL, aY = pT, aW = W - pL - pR, aH = H - pT - pB;
    Path zone; rr (zone, aX, aY, aW, aH, 8);
    g.setColour (p.cv2); g.fillPath (zone);
    const int n = (int) actIn.size();
    auto xAt = [&] (int i) { return aX + aW - (float) (n - 1 - i); };
    g.saveState(); g.reduceClipRegion (zone);
    const float mid = aY + aH / 2, amp = aH * 0.47f;
    auto dbToV = [] (float db) { return juce::jlimit (0.0f, 1.0f, (std::max (-60.0f, db) + 60) / 60); };
    g.setColour (p.grid);
    for (float d : { -12.0f, -24.0f, -36.0f })
    {
        const float v = dbToV (d);
        g.drawLine (aX, mid - v * amp, aX + aW, mid - v * amp, 1); g.drawLine (aX, mid + v * amp, aX + aW, mid + v * amp, 1);
    }
    auto wave = [&] (const std::vector<float>& arr, Colour fillC, Colour strokeC)
    {
        if (arr.size() < 2) return;
        Path w;
        for (int i = 0; i < n; ++i) { const float v = dbToV (arr[(size_t) i]); if (i == 0) w.startNewSubPath (xAt (i), mid - v * amp); else w.lineTo (xAt (i), mid - v * amp); }
        for (int i = n - 1; i >= 0; --i) w.lineTo (xAt (i), mid + dbToV (arr[(size_t) i]) * amp);
        w.closeSubPath();
        g.setColour (fillC); g.fillPath (w);
        g.setColour (strokeC); g.strokePath (w, juce::PathStrokeType (1));
    };
    wave (actIn, A (p.txt, 0.18f), A (p.txt, 0.5f));
    wave (actOut, A (p.accent, 0.35f), A (p.accent, 0.9f));
    const float tv = dbToV (proc.getParam (params::thr));
    g.setColour (p.accent);
    { Path l; l.startNewSubPath (aX, mid - tv * amp); l.lineTo (aX + aW, mid - tv * amp); dashedLine (g, l, 1.2f, 6, 4); }
    { Path l; l.startNewSubPath (aX, mid + tv * amp); l.lineTo (aX + aW, mid + tv * amp); dashedLine (g, l, 1.2f, 6, 4); }
    g.restoreState();
    g.setColour (p.txt); g.setFont (theme.mono (10));
    g.drawText ("0", Rectangle<float> (aX - 40, mid - amp - 6, 34, 12), juce::Justification::centredRight, false);
    g.drawText ("-24", Rectangle<float> (aX - 40, mid - dbToV (-24) * amp - 6, 34, 12), juce::Justification::centredRight, false);
    g.setColour (p.orange);
    g.drawText ("GR -" + juce::String (m.gr, 1) + " dB", Rectangle<float> (aX, aY - 18, aW, 12), juce::Justification::centredRight, false);
}

// ── Vue Mètres : VU à aiguille (réduction de gain) + barres In / GR / Out ──
void CompDisplay::drawMeters (Graphics& g, float W, float H, const M& m)
{
    const auto& p = theme.pal();
    const float mW = 16, pT = 24, pB = 22, mH = H - pT - pB, gap = 30;
    const float bx0 = W - (3 * mW + 2 * gap) - 12;
    const float xs[] = { bx0, bx0 + mW + gap, bx0 + 2 * (mW + gap) };
    const float vw = bx0 - 40, vh = std::min (H - pT - pB + 28, vw * 0.8f);   // cadran aux proportions d'un VU
    needleVU (g, 8, pT - 14 + (H - pT - pB + 28 - vh) / 2, vw, vh);
    const float thr = proc.getParam (params::thr);
    meterBar (g, "cc-in", xs[0], pT, mW, mH, (m.in + 60) / 60, false, "In", juce::String (m.in, 1), (thr + 60) / 60);
    meterBar (g, "cc-gr", xs[1], pT, mW, mH, m.gr / 24, true, "GR", "-" + juce::String (m.gr, 1));
    meterBar (g, "cc-out", xs[2], pT, mW, mH, (m.out + 60) / 60, false, "Out", juce::String (m.out, 1));
    g.setColour (p.txt); g.setFont (theme.mono (9));
    for (int d : { 0, -12, -24, -36, -48 })
    {
        const float y = pT + mH * (1 - (d + 60) / 60.0f);
        g.drawText (juce::String (d), Rectangle<float> (xs[0] - 37, y - 6, 32, 12), juce::Justification::centredRight, false);
        g.drawText (juce::String (d), Rectangle<float> (xs[2] - 37, y - 6, 32, 12), juce::Justification::centredRight, false);
    }
    for (int d : { 0, 6, 12, 18, 24 })
    {
        const float y = pT + d / 24.0f * mH;
        g.drawText ("-" + juce::String (d), Rectangle<float> (xs[1] - 37, y - 6, 32, 12), juce::Justification::centredRight, false);
    }
}

void CompDisplay::needleVU (Graphics& g, float x, float y, float w, float h)
{
    const auto& p = theme.pal();
    Path box; rr (box, x, y, w, h, 12);
    g.setColour (p.cv2); g.fillPath (box);
    g.setColour (p.grid); g.strokePath (box, juce::PathStrokeType (1));
    const float s = std::min (w / 330.0f, h / 200.0f), ox = x + (w - 330 * s) / 2, oy = y + (h - 200 * s) / 2;
    g.saveState(); g.reduceClipRegion (box);
    g.addTransform (juce::AffineTransform::scale (s).translated (ox, oy));
    const float cx = 165, cy = 215, R = 150;
    static const float pts[][2] = { { -20, 0 }, { -10, .2f }, { -7, .32f }, { -5, .44f }, { -3, .57f }, { -2, .64f }, { -1, .71f }, { 0, .78f }, { 1, .855f }, { 2, .93f }, { 3, 1 } };
    auto pos = [&] (double vu) -> float
    {
        if (vu <= -20) return 0;
        if (vu >= 3) return 1;
        for (int i = 1; i < 11; ++i)
            if (vu <= pts[i][0]) { const float a = pts[i - 1][0], pa = pts[i - 1][1], b = pts[i][0], pb = pts[i][1]; return (float) (pa + (pb - pa) * (vu - a) / (b - a)); }
        return 1;
    };
    auto ang = [] (float t) { return (-48 + 96 * t) * juce::MathConstants<float>::pi / 180; };
    auto P = [&] (float t, float r) { return juce::Point<float> (cx + std::sin (ang (t)) * r, cy - std::cos (ang (t)) * r); };
    { Path a; a.addCentredArc (cx, cy, R, R, 0, ang (0), ang (pos (0)), true); g.setColour (p.txt2); g.strokePath (a, juce::PathStrokeType (2)); }
    { Path a; a.addCentredArc (cx, cy, R, R, 0, ang (pos (0)), ang (1), true); g.setColour (p.red); g.strokePath (a, juce::PathStrokeType (4)); }
    g.setFont (theme.ui (11, 600));
    for (auto& q : pts)
    {
        const float vu = q[0], t = pos (vu);
        const auto p1 = P (t, R - 1), p2 = P (t, R - 10), tp = P (t, R + 13);
        g.setColour (vu > 0 ? p.red : p.txt2); g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.5f);
        g.setColour (vu > 0 ? p.red : p.txt);
        const juce::String lab = (int) vu == 0 ? "0" : (vu > 0 ? "+" + juce::String ((int) vu) : juce::String ((int) vu));
        g.drawText (lab, Rectangle<float> (tp.x - 15, tp.y - 7, 30, 14), juce::Justification::centred, false);
    }
    for (int vu = -20; vu <= 3; ++vu)
    {
        bool major = false; for (auto& q : pts) if ((int) q[0] == vu) major = true;
        if (major) continue;
        const float t = pos (vu); const auto p1 = P (t, R - 1), p2 = P (t, R - 6);
        g.setColour (p.txt); g.drawLine (p1.x, p1.y, p2.x, p2.y, 1);
    }
    g.setColour (p.txt2); g.setFont (theme.ui (20, 700));
    g.drawText ("VU", Rectangle<float> (cx - 40, 120, 80, 24), juce::Justification::centred, false);
    g.setColour (p.txt); g.setFont (theme.ui (10, 600));
    g.drawText ("GAIN REDUCTION", Rectangle<float> (cx - 80, 145, 160, 14), juce::Justification::centred, false);
    g.setColour (p.orange); g.setFont (theme.mono (12, true));
    g.drawText ("-" + juce::String (std::max (0.0, vuGr), 1) + " dB", Rectangle<float> (198, 178, 120, 16), juce::Justification::centredRight, false);
    const float a = ang (pos (vuPos));
    g.setColour (p.accent);
    Path needle; needle.startNewSubPath (cx, cy); needle.lineTo (cx + std::sin (a) * (R + 4), cy - std::cos (a) * (R + 4));
    g.strokePath (needle, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (p.txt2); g.fillEllipse (cx - 14, cy - 14, 28, 28);
    g.restoreState();
}
} // namespace audiolab
