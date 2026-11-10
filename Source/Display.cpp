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
    // ∞:1 (potentiomètre ≥ 20) : vrai limiteur, la courbe est un plafond horizontal (seuil + make-up), sans knee
    return CompressorCore::curve (di, proc.getParam (params::thr), proc.getParam (params::ratio),
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
        const size_t N = (size_t) std::max (10, getWidth() / 2 + 2);   // un point toutes les 2 px
        while (actIn.size() > N) { actIn.erase (actIn.begin()); actOut.erase (actOut.begin()); actGR.erase (actGR.begin()); }
    }
    repaint();
}

void CompDisplay::paint (Graphics& g)
{
    const auto& p = theme.pal();
    const float W = (float) getWidth(), H = (float) getHeight();
    Path bg; rr (bg, 0.5f, 0.5f, W - 1, H - 1, 11.5f);   // coins 12 (3.0.0)
    g.setColour (p.surface); g.fillPath (bg);   // écran « Surface » (charte V2)
    g.saveState();
    g.reduceClipRegion (bg);
    if (view == transfer) drawTransfer (g, W, H, cur);
    else if (view == activity) drawActivity (g, W, H, cur);
    else drawMeters (g, W, H, cur);
    g.restoreState();
    g.setColour (p.rail); g.strokePath (bg, juce::PathStrokeType (1));
}

// Barre de niveau horizontale (vue Vu-Mètres) : départ arrondi, bout mobile coupé droit ; la barre GR part de la droite
void CompDisplay::hBar (Graphics& g, const juce::String& key, float y, float norm, bool gr,
                        const juce::String& label, const juce::String& value, float thrNorm)
{
    const auto& p = theme.pal();
    const float x = 48, w = 160, h = 8;   // 3.0.0 : étiquette à 16, piste 48 → 208, valeur calée à 256 (bord du cadran)
    norm = juce::jlimit (0.0f, 1.0f, norm);
    g.setColour (p.soft); g.setFont (theme.ui (10, 600));
    g.drawText (label, Rectangle<float> (16, y - 3, 32, 14), juce::Justification::centredLeft, false);
    Path track; rr (track, x, y, w, h, 4);
    g.setColour (p.bg); g.fillPath (track);
    g.saveState();
    g.reduceClipRegion (track);
    // niveaux : teal secondaire → ambre (82 %) → rouge ; réduction de gain : orange
    if (gr) g.setColour (p.orange);
    else
    {
        juce::ColourGradient grad (p.teal2, x, 0, p.redDark, x + w, 0, false);
        grad.addColour (0.6, p.teal2); grad.addColour (0.82, p.orange);
        g.setGradientFill (grad);
    }
    // niveaux : de gauche à droite ; réduction de gain : part de la droite (0 dB) et descend vers la gauche, comme l'aiguille du VU
    if (gr) g.fillRect (x + (1 - norm) * w, y, norm * w, h);
    else g.fillRect (x, y, norm * w, h);
    // maintien de crête (petit trait)
    auto& pk = peaks[key];
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (norm >= pk.v || now - pk.t > 1200) { pk.v = norm; pk.t = now; }
    else pk.v = std::max (norm, pk.v - 0.004f);
    if (pk.v > 0.01f) { g.setColour (gr ? p.orange : (pk.v > 0.85f ? p.redDark : p.text2)); g.fillRect (x + (gr ? 1 - pk.v : pk.v) * w - 1, y, 2.0f, h); }
    g.restoreState();
    g.setColour (p.rail); g.strokePath (track, juce::PathStrokeType (1));
    if (thrNorm >= 0) { g.setColour (p.teal); g.fillRect (x + thrNorm * w - 1, y - 4, 2.0f, h + 8); }
    g.setColour (p.text2);
    drawTabular (g, theme, value, 10, 400, Rectangle<float> (212, y - 3, 44, 14), juce::Justification::right);
}

// ── Vue Transfert : le graphe remplit tout le carré, chiffres à l'intérieur ──
void CompDisplay::drawTransfer (Graphics& g, float W, float H, const M& m)
{
    const auto& p = theme.pal();
    // 3.0.0 : 4 px par dB (272 px = 68 dB), les lignes tous les 12 dB tombent sur la grille de 8 px
    const float cX = 0, cY = 0, cW = W, cH = H, lo = -68;
    auto X = [&] (double d) { return (float) (cX + (d - lo) / (0 - lo) * cW); };
    auto Y = [&] (double d) { return (float) (cY + cH - (d - lo) / (0 - lo) * cH); };
    // en ∞:1 (limiteur), le knee ne s'applique pas : pas de zone de knee
    const float thr = proc.getParam (params::thr), kn = proc.getParam (params::ratio) >= 20 ? 0.0f : proc.getParam (params::knee);

    g.setColour (p.rail);
    for (int db = -60; db <= -12; db += 12)
    {
        g.drawLine (X (db), cY, X (db), cY + cH, 1); g.drawLine (cX, Y (db), cX + cW, Y (db), 1);
    }
    // knee + seuil
    const float hk = kn / 2;
    g.setColour (A (p.teal, 0.07f)); g.fillRect (X (thr - hk), cY, X (thr + hk) - X (thr - hk), cH);
    g.setColour (p.teal);   // seuil en pointillés, opacité 100 % (charte : à 50 % il ne se voit plus)
    { Path l; l.startNewSubPath (X (thr), cY); l.lineTo (X (thr), cY + cH); dashedLine (g, l, 1, 4, 4); }
    // diagonale 1:1 (repère secondaire)
    g.setColour (p.mark2);
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
    g.setGradientFill (juce::ColourGradient (A (p.teal, 0.25f), 0, cY, A (p.teal, 0.02f), 0, cY + cH, false));
    g.fillPath (fill);
    g.setColour (p.teal);
    g.strokePath (c, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // point en direct + repères
    const double di = std::max ((double) lo, std::min (0.0, (double) m.in));
    const double dout = std::max ((double) lo, std::min (6.0, curve (di)));
    const float dx = X (di), dyy = Y (dout);
    g.setColour (A (p.orange, 0.5f));
    { Path l; l.startNewSubPath (dx, cY + cH); l.lineTo (dx, dyy); l.lineTo (cX, dyy); dashedLine (g, l, 1, 3, 3); }
    g.setColour (A (p.orange, 0.2f)); g.fillEllipse (dx - 8, dyy - 8, 16, 16);
    g.setColour (p.orange); g.fillEllipse (dx - 5, dyy - 5, 10, 10);
    g.setColour (p.bg); g.drawEllipse (dx - 5, dyy - 5, 10, 10, 2);
    // chiffres à l'intérieur, en petit : sortie le long du bord gauche, entrée le long du bord bas
    g.setColour (p.soft); g.setFont (theme.ui (10));
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    for (int db = -12; db >= -60; db -= 12)
    {
        g.drawText (minus + juce::String (-db), Rectangle<float> (6, Y (db) - 15, 40, 12), juce::Justification::bottomLeft, false);
        if (db > -60) g.drawText (minus + juce::String (-db), Rectangle<float> (X (db) - 44, H - 18, 40, 12), juce::Justification::bottomRight, false);
    }
}

// ── Vue Activité (3.0.0) : niveaux posés en bas, 4 px par dB comme la vue Transfer ; entrée en gris, sortie en teal,
// seuil en pointillés, et la réduction de gain en orange suspendue en haut, à la même échelle : ce qui pend en haut
// est ce que le compresseur retire. Un point toutes les 2 px, le plus récent à droite. Chiffres d'échelle à droite.
void CompDisplay::drawActivity (Graphics& g, float W, float H, const M&)
{
    const auto& p = theme.pal();
    const int n = (int) actIn.size();
    const float k = H / 68.0f;
    auto xAt = [&] (int i) { return W - 2.0f * (float) (n - 1 - i); };
    auto Y = [&] (float db) { return juce::jlimit (0.0f, H, -db * k); };
    g.setColour (p.rail);
    for (int db = -12; db >= -60; db -= 12) g.drawLine (0, Y ((float) db), W, Y ((float) db), 1);
    auto area = [&] (const std::vector<float>& arr, Colour fillC, Colour strokeC, float sw)
    {
        if (arr.size() < 2) return;
        Path line, fill;
        for (int i = 0; i < n; ++i) { const float y = Y (arr[(size_t) i]); if (i == 0) line.startNewSubPath (xAt (i), y); else line.lineTo (xAt (i), y); }
        fill = line; fill.lineTo (xAt (n - 1), H); fill.lineTo (xAt (0), H); fill.closeSubPath();
        g.setColour (fillC); g.fillPath (fill);
        g.setColour (strokeC); g.strokePath (line, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    area (actIn, A (p.soft, 0.16f), A (p.soft, 0.7f), 1.0f);
    area (actOut, A (p.teal, 0.20f), p.teal2, 1.5f);
    g.setColour (p.teal);
    { const float ty = Y (proc.getParam (params::thr)); Path l; l.startNewSubPath (0, ty); l.lineTo (W, ty); dashedLine (g, l, 1, 4, 4); }
    if (actGR.size() >= 2)
    {
        Path line, fill;
        for (int i = 0; i < n; ++i) { const float y = std::min (H, actGR[(size_t) i] * k); if (i == 0) line.startNewSubPath (xAt (i), y); else line.lineTo (xAt (i), y); }
        fill = line; fill.lineTo (xAt (n - 1), 0); fill.lineTo (xAt (0), 0); fill.closeSubPath();
        g.setColour (A (p.orange, 0.22f)); g.fillPath (fill);
        g.setColour (p.orange); g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (p.soft); g.setFont (theme.ui (10));
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    for (int db = -12; db >= -60; db -= 12)
        g.drawText (minus + juce::String (-db), Rectangle<float> (W - 46, Y ((float) db) - 15, 40, 12), juce::Justification::bottomRight, false);
}
// ── Vue Vu-Mètres : cadran VU clair rétroéclairé (charte V2) + barres In / GR / Out ──
void CompDisplay::drawMeters (Graphics& g, float W, float, const M& m)
{
    // 3.0.0 : cadran 240 × 160 (3:2) à 16 px des bords, barres au pas de 24 (centres y 200 / 224 / 248)
    drawVuDial (g, theme, Rectangle<float> (16, 16, W - 32, 160), vuPos, "GAIN REDUCTION", {}, lampOff);
    const float thr = proc.getParam (params::thr);
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    auto val = [&] (float v) { return (v < -0.05f ? minus : juce::String()) + juce::String (std::abs (v), 1); };
    hBar (g, "in", 196, (m.in + 60) / 60, false, "In", val (m.in), (thr + 60) / 60);
    hBar (g, "gr", 220, m.gr / 24, true, "GR", val (-m.gr));
    hBar (g, "out", 244, (m.out + 60) / 60, false, "Out", val (m.out));
}
} // namespace audiolab
