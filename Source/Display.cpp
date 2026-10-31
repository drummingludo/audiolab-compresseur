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
        const size_t N = (size_t) std::max (10, getWidth());
        while (actIn.size() > N) { actIn.erase (actIn.begin()); actOut.erase (actOut.begin()); actGR.erase (actGR.begin()); }
    }
    repaint();
}

void CompDisplay::paint (Graphics& g)
{
    const auto& p = theme.pal();
    const float W = (float) getWidth(), H = (float) getHeight();
    Path bg; rr (bg, 0.5f, 0.5f, W - 1, H - 1, 10);
    g.setColour (view == meters ? p.cv : p.cv2); g.fillPath (bg);
    g.saveState();
    g.reduceClipRegion (bg);
    if (view == transfer) drawTransfer (g, W, H, cur);
    else if (view == activity) drawActivity (g, W, H, cur);
    else drawMeters (g, W, H, cur);
    g.restoreState();
    g.setColour (p.grid); g.strokePath (bg, juce::PathStrokeType (1));
}

// Barre de niveau horizontale (vue Vu-Mètres) : départ arrondi, bout mobile coupé droit
void CompDisplay::hBar (Graphics& g, const juce::String& key, float y, float norm, bool gr,
                        const juce::String& label, const juce::String& value, float thrNorm)
{
    const auto& p = theme.pal();
    const float x = 46, w = 170, h = 8;
    norm = juce::jlimit (0.0f, 1.0f, norm);
    g.setColour (p.txt); g.setFont (theme.ui (10, 600));
    g.drawText (label, Rectangle<float> (14, y - 3, 32, 14), juce::Justification::centredLeft, false);
    Path track; rr (track, x, y, w, h, 4);
    g.setColour (p.cv2); g.fillPath (track);
    g.saveState();
    g.reduceClipRegion (track);
    if (gr) g.setColour (p.orange);
    else
    {
        juce::ColourGradient grad (p.green, x, 0, p.red, x + w, 0, false);
        grad.addColour (0.6, p.green); grad.addColour (0.82, p.yellow);
        g.setGradientFill (grad);
    }
    g.fillRect (x, y, norm * w, h);
    // maintien de crête (petit trait)
    auto& pk = peaks[key];
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (norm >= pk.v || now - pk.t > 1200) { pk.v = norm; pk.t = now; }
    else pk.v = std::max (norm, pk.v - 0.004f);
    if (pk.v > 0.01f) { g.setColour (gr ? p.orange : (pk.v > 0.85f ? p.red : p.txt2)); g.fillRect (x + pk.v * w - 1, y, 2.0f, h); }
    g.restoreState();
    g.setColour (p.grid); g.strokePath (track, juce::PathStrokeType (1));
    if (thrNorm >= 0) { g.setColour (p.accent); g.fillRect (x + thrNorm * w - 1, y - 4, 2.0f, h + 8); }
    g.setColour (p.txt2); g.setFont (theme.mono (9));
    g.drawText (value, Rectangle<float> (216, y - 3, 40, 14), juce::Justification::centredRight, false);
}

// ── Vue Transfert : le graphe remplit tout le carré, chiffres à l'intérieur ──
void CompDisplay::drawTransfer (Graphics& g, float W, float H, const M& m)
{
    const auto& p = theme.pal();
    const float cX = 1, cY = 1, cW = W - 2, cH = H - 2, lo = -60;
    auto X = [&] (double d) { return (float) (cX + (d - lo) / (0 - lo) * cW); };
    auto Y = [&] (double d) { return (float) (cY + cH - (d - lo) / (0 - lo) * cH); };
    const float thr = proc.getParam (params::thr), kn = proc.getParam (params::knee);

    g.setColour (p.grid);
    for (int db = -48; db <= -12; db += 12)
    {
        g.drawLine (X (db), cY, X (db), cY + cH, 1); g.drawLine (cX, Y (db), cX + cW, Y (db), 1);
    }
    // knee + seuil
    const float hk = kn / 2;
    g.setColour (A (p.accent, 0.07f)); g.fillRect (X (thr - hk), cY, X (thr + hk) - X (thr - hk), cH);
    g.setColour (A (p.accent, 0.5f));
    { Path l; l.startNewSubPath (X (thr), cY); l.lineTo (X (thr), cY + cH); dashedLine (g, l, 1, 4, 4); }
    // diagonale 1:1
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
    g.setColour (p.accent);
    g.strokePath (c, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // point en direct + repères
    const double di = std::max ((double) lo, std::min (0.0, (double) m.in));
    const double dout = std::max ((double) lo, std::min (6.0, curve (di)));
    const float dx = X (di), dyy = Y (dout);
    g.setColour (A (p.orange, 0.5f));
    { Path l; l.startNewSubPath (dx, cY + cH); l.lineTo (dx, dyy); l.lineTo (cX, dyy); dashedLine (g, l, 1, 3, 3); }
    g.setColour (A (p.orange, 0.25f)); g.fillEllipse (dx - 9, dyy - 9, 18, 18);
    g.setColour (p.orange); g.fillEllipse (dx - 5, dyy - 5, 10, 10);
    g.setColour (juce::Colours::white); g.drawEllipse (dx - 5, dyy - 5, 10, 10, 1.5f);
    // chiffres à l'intérieur, en petit : sortie le long du bord gauche, entrée le long du bord bas
    g.setColour (p.txt); g.setFont (theme.mono (8));
    for (int db = 0; db >= -48; db -= 12)
    {
        const float base = db == 0 ? 12.0f : Y (db) - 3;
        g.drawText (juce::String (db), Rectangle<float> (6, base - 9, 40, 10), juce::Justification::bottomLeft, false);
    }
    for (int db = -48; db <= 0; db += 12)
    {
        const float right = db == 0 ? W - 6 : X (db) - 3;
        g.drawText (juce::String (db), Rectangle<float> (right - 40, H - 7 - 9, 40, 10), juce::Justification::bottomRight, false);
    }
}

// ── Vue Activité : remplit tout le carré, aucun chiffre ──
void CompDisplay::drawActivity (Graphics& g, float W, float H, const M&)
{
    const auto& p = theme.pal();
    const float aX = 0, aW = W;
    const int n = (int) actIn.size();
    auto xAt = [&] (int i) { return aX + aW - (float) (n - 1 - i); };
    const float mid = H / 2, amp = H * 0.47f;
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
}

// ── Vue Vu-Mètres : VU à aiguille sur une face encastrée + barres In / GR / Out ──
void CompDisplay::drawMeters (Graphics& g, float W, float, const M& m)
{
    const auto& p = theme.pal();
    const Rectangle<float> face (14, 14, W - 28, 172);
    Path panel; panel.addRoundedRectangle (face, 8);
    g.setColour (p.cv2); g.fillPath (panel);
    g.saveState();
    g.reduceClipRegion (panel);
    {
        // ombre intérieure : décalée de 4 px vers le bas, flou ≈ 5,5 px, opacité 50 %
        Path frame; frame.addRectangle (face.expanded (40)); frame.addRoundedRectangle (face, 8);
        frame.setUsingNonZeroWinding (false);
        juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 11, { 0, 4 }).drawForPath (g, frame);
    }
    needleVU (g, face.getX(), face.getY(), face.getWidth(), 147);
    g.restoreState();
    const float thr = proc.getParam (params::thr);
    hBar (g, "in", 200, (m.in + 60) / 60, false, "In", juce::String (m.in, 1), (thr + 60) / 60);
    hBar (g, "gr", 222, m.gr / 24, true, "GR", "-" + juce::String (m.gr, 1));
    hBar (g, "out", 244, (m.out + 60) / 60, false, "Out", juce::String (m.out, 1));
}

void CompDisplay::needleVU (Graphics& g, float x, float y, float w, float h)
{
    const auto& p = theme.pal();
    const float s = std::min (w / 330.0f, h / 200.0f), ox = x + (w - 330 * s) / 2, oy = y + (h - 200 * s) / 2;
    g.saveState();
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
    const float a = ang (pos (vuPos));
    g.setColour (p.accent);
    Path needle; needle.startNewSubPath (cx, cy); needle.lineTo (cx + std::sin (a) * (R + 4), cy - std::cos (a) * (R + 4));
    g.strokePath (needle, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (p.txt2); g.fillEllipse (cx - 14, cy - 14, 28, 28);
    g.restoreState();
}
} // namespace audiolab
