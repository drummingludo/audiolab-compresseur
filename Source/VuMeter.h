// VU-mètre à aiguille des plugins Audio Lab (charte V2, fiche 02 § 7) :
// écran « Surface » ; cadran clair rétroéclairé encastré (ombre intérieure, halo de lampe ambre éteint en bypass,
// bords assombris, reflet de verre qui ne masque pas les chiffres), aiguille sombre avec ombre portée, pivot en relief.
// Mode 0 : réduction de gain. Modes suivants : niveau de sortie référencé (+4, +8, +10…). Dernier mode facultatif : éteint.
// Aiguille : réponse VU normalisée du 2e ordre (99 % de la déviation en 300 ms, dépassement ≈ 1,3 %).
#pragma once
#include "Theme.h"
#include <functional>
#include <vector>

namespace audiolab
{
// Position (0 → 1) d'une valeur VU sur l'échelle −20 … +3 du SVG d'Audio Lab
inline float vuScalePos (float vu)
{
    static constexpr float pts[11][2] = { { -20, 0 }, { -10, .2f }, { -7, .32f }, { -5, .44f }, { -3, .57f }, { -2, .64f },
                                          { -1, .71f }, { 0, .78f }, { 1, .855f }, { 2, .93f }, { 3, 1 } };
    if (vu <= -20) return 0;
    if (vu >= 3) return 1;
    for (int i = 1; i < 11; ++i)
        if (vu <= pts[i][0])
        {
            const float a = pts[i - 1][0], pa = pts[i - 1][1], b = pts[i][0], pb = pts[i][1];
            return pa + (pb - pa) * (vu - a) / (b - a);
        }
    return 1;
}

// Cadran VU clair rétroéclairé (charte 02 § 7), dessiné dans « face » : face encastrée, lampe ambre (éteinte en bypass),
// bords assombris, échelle, aiguille avec ombre, pivot, valeur facultative en bas à droite, reflet de verre.
inline void drawVuDial (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> face, double needle,
                        const juce::String& modeText, const juce::String& valueText, bool lampOff)
{
    static constexpr float major[11] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    const auto& p = theme.pal();
    juce::Path facePath; facePath.addRoundedRectangle (face, 8);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (facePath);
    g.setColour (p.vuFace); g.fillPath (facePath);
    // ombre intérieure : décalée de 4 px vers le bas, flou ≈ 5,5 px, opacité 50 %
    juce::Path frame; frame.addRectangle (face.expanded (40)); frame.addRoundedRectangle (face, 8);
    frame.setUsingNonZeroWinding (false);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 11, { 0, 4 }).drawForPath (g, frame);

    // cadran 330 × 200 (repère du SVG d'Audio Lab), en haut de la face, pivot visible en bas
    const float s = std::min (face.getWidth() / 330.0f, face.getHeight() / 234.6f);
    const float ox = face.getX() + (face.getWidth() - 330 * s) / 2, oy = face.getY();
    const float pivotY = oy + 215 * s;

    // halo de lampe ambre sous le pivot (34 % → 12 % → 0), éteint en bypass
    if (! lampOff)
    {
        juce::ColourGradient halo (p.orange.withAlpha (0.34f), face.getCentreX(), pivotY + 10 * s,
                                   p.orange.withAlpha (0.0f), face.getCentreX() + face.getWidth() * 0.70f, pivotY + 10 * s, true);
        halo.addColour (0.5, p.orange.withAlpha (0.12f));
        g.setGradientFill (halo); g.fillRect (face);
    }
    // bords légèrement assombris (0 → 9 %)
    {
        const juce::Point<float> c (face.getCentreX(), face.getY() + face.getHeight() * 0.477f);
        juce::ColourGradient vg (juce::Colours::black.withAlpha (0.0f), c, juce::Colours::black.withAlpha (0.09f),
                                 c.translated (face.getWidth() * 0.62f, 0), true);
        vg.addColour (0.55, juce::Colours::black.withAlpha (0.0f));
        g.setGradientFill (vg); g.fillRect (face);
    }

    {
        juce::Graphics::ScopedSaveState sd (g);
        g.addTransform (juce::AffineTransform::scale (s).translated (ox, oy));
        const float cx = 165, cy = 215, R = 150;
        auto ang = [] (float t) { return (-48 + 96 * t) * juce::MathConstants<float>::pi / 180; };
        auto P = [&] (float t, float r) { return juce::Point<float> (cx + std::sin (ang (t)) * r, cy - std::cos (ang (t)) * r); };
        { juce::Path a; a.addCentredArc (cx, cy, R, R, 0, ang (0), ang (vuScalePos (0)), true); g.setColour (p.vuInk); g.strokePath (a, juce::PathStrokeType (2)); }
        { juce::Path a; a.addCentredArc (cx, cy, R, R, 0, ang (vuScalePos (0)), ang (1), true); g.setColour (p.redLight); g.strokePath (a, juce::PathStrokeType (4)); }
        for (int vu = -20; vu <= 3; ++vu)
        {
            bool isMajor = false; for (float q : major) if ((int) q == vu) isMajor = true;
            if (isMajor) continue;
            const float t = vuScalePos ((float) vu); const auto p1 = P (t, R - 1), p2 = P (t, R - 6);
            g.setColour (p.vuMinor); g.drawLine (p1.x, p1.y, p2.x, p2.y, 1);
        }
        g.setFont (theme.ui (11, 600));
        for (float vu : major)
        {
            const float t = vuScalePos (vu);
            const auto p1 = P (t, R - 1), p2 = P (t, R - 10), tp = P (t, R + 13);
            g.setColour (vu > 0 ? p.redLight : p.vuInk); g.drawLine (p1.x, p1.y, p2.x, p2.y, 1.5f);
            g.setColour (vu > 0 ? p.redLight : p.vuText);
            const juce::String lab = (int) vu == 0 ? "0" : (vu > 0 ? "+" + juce::String ((int) vu) : juce::String ((int) vu));
            g.drawText (lab, juce::Rectangle<float> (tp.x - 16, tp.y - 8, 32, 16), juce::Justification::centred, false);
        }
        g.setColour (p.vuInk); g.setFont (theme.ui (20, 700));
        g.drawText ("VU", juce::Rectangle<float> (cx - 50, 121, 100, 24), juce::Justification::centred, false);
        g.setColour (p.vuText); g.setFont (theme.ui (10, 600, 0.1f));
        g.drawText (modeText, juce::Rectangle<float> (cx - 110, 148, 220, 16), juce::Justification::centred, false);

        // aiguille (1,6 px à l'écran) avec ombre portée, puis pivot en relief
        const float a = ang (vuScalePos ((float) needle)), w = 1.6f / s;
        juce::Path nd; nd.startNewSubPath (cx, cy); nd.lineTo (cx + std::sin (a) * (R + 4), cy - std::cos (a) * (R + 4));
        {
            juce::Graphics::ScopedSaveState sh (g);
            g.addTransform (juce::AffineTransform::translation (3, 4));
            const float ew[] = { 2.4f, 1.2f, 0.2f }, ea[] = { 0.07f, 0.09f, 0.12f };
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (juce::Colours::black.withAlpha (ea[i]));
                g.strokePath (nd, juce::PathStrokeType (w + ew[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        g.setColour (p.vuInk);
        g.strokePath (nd, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::ColourGradient pg (juce::Colour (0xFF4A4B49), cx - 14 + 0.38f * 28, cy - 14 + 0.32f * 28,
                                 p.vuInk, cx - 14 + 0.38f * 28 + 21, cy - 14 + 0.32f * 28, true);
        g.setGradientFill (pg); g.fillEllipse (cx - 14, cy - 14, 28, 28);
    }

    // valeur lue, en bas à droite du cadran
    if (valueText.isNotEmpty())
    {
        g.setColour (p.vuText);
        drawTabular (g, theme, valueText, 10, 600, juce::Rectangle<float> (face.getRight() - 150, face.getBottom() - 22, 140, 14),
                     juce::Justification::right);
    }
    // reflet de verre diagonal (blanc 85 % → 18 %, coupé net), en dehors des chiffres
    juce::ColourGradient gl (juce::Colours::white.withAlpha (0.85f), face.getX(), face.getY(),
                             juce::Colours::white.withAlpha (0.0f), face.getX() + face.getWidth() * 0.397f,
                             face.getY() + face.getHeight() * 0.791f, false);
    gl.addColour (0.42, juce::Colours::white.withAlpha (0.18f));
    gl.addColour (0.43, juce::Colours::white.withAlpha (0.0f));
    g.setGradientFill (gl); g.fillRect (face);
}

class VuFace : public juce::Component, private juce::Timer
{
public:
    struct Reading { float grDb = 0; float outDb = -100; bool active = false; };

    VuFace (Theme& t, std::function<Reading()> source) : theme (t), src (std::move (source))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (60);
    }
    ~VuFace() override { stopTimer(); }

    // refs : niveau (dBFS, convention AES17 : sinus crête = valeur efficace + 3 dB) qui donne 0 VU pour chaque mode de sortie
    void setModes (std::vector<float> refsDbfs, juce::StringArray outLabels, bool withOff)
    {
        refs = std::move (refsDbfs); labels = std::move (outLabels); hasOff = withOff;
    }
    void setMode (int m) { mode = m; repaint(); }
    int getMode() const { return mode; }
    void setLampOff (bool off) { if (off != lampOff) { lampOff = off; repaint(); } }   // bypass : lampe éteinte

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const float W = (float) getWidth(), H = (float) getHeight();
        const juce::Rectangle<float> screen (0.5f, 0.5f, W - 1, H - 1);
        g.setColour (p.surface); g.fillRoundedRectangle (screen, 10);
        // cadran encastré, 14 px à l'intérieur de l'écran
        drawVuDial (g, theme, juce::Rectangle<float> (14, 14, W - 28, H - 28), needle, modeText(), valueText(), lampOff);
        g.setColour (p.rail); g.drawRoundedRectangle (screen, 10, 1);
    }

private:
    bool isOff() const { return hasOff && mode > (int) refs.size(); }

    juce::String modeText() const
    {
        if (isOff()) return "METER OFF";
        if (mode == 0) return "GAIN REDUCTION";
        return labels[mode - 1];
    }
    juce::String valueText() const
    {
        if (isOff()) return {};
        if (mode == 0) return juce::String (std::abs (grShown) < 0.05 ? 0.0 : -grShown, 1) + " dB GR";
        return (outShown > 0 ? "+" : "") + juce::String (outShown, 1) + " VU";
    }

    void timerCallback() override
    {
        const auto r = src();
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const double dt = std::min (0.1, lastT > 0 ? now - lastT : 1.0 / 60);
        lastT = now;
        const float gr = r.active ? std::min (0.0f, r.grDb) : 0.0f;
        // mémoire courte : la réduction la plus forte depuis la dernière lecture redescend en 60 ms
        grSm += (gr - grSm) * (1 - std::exp (-dt / 0.06));
        grShown = (float) grSm;
        double target;
        if (isOff()) target = -22;
        else if (mode == 0) target = grSm;
        else
        {
            const int k = juce::jlimit (0, (int) refs.size() - 1, mode - 1);
            target = (r.active ? r.outDb : -100) - refs[(size_t) k];
            outShown += (float) (((r.active ? r.outDb : -100) - refs[(size_t) k] - outShown) * (1 - std::exp (-dt / 0.3)));
        }
        // aiguille VU normalisée (2e ordre) : 99 % en 300 ms, dépassement ≈ 1,3 %
        const double wn = 13.42, z = 0.81;
        target = std::max (-23.0, std::min (6.0, target));
        for (double t = 0; t < dt; t += 0.001)
        {
            const double h1 = std::min (0.001, dt - t);
            const double acc = wn * wn * (target - needle) - 2 * z * wn * vel;
            vel += acc * h1; needle += vel * h1;
        }
        // rien ne bouge quand le signal est à l'arrêt : on ne redessine que si l'affichage change
        const auto vt = valueText();
        if (std::abs (needle - drawnNeedle) > 0.002 || vt != drawnText || mode != drawnMode)
        {
            drawnNeedle = needle; drawnText = vt; drawnMode = mode;
            repaint();
        }
    }

    Theme& theme;
    std::function<Reading()> src;
    std::vector<float> refs;
    juce::StringArray labels;
    bool hasOff = false, lampOff = false;
    int mode = 0, drawnMode = -1;
    double needle = -20, vel = 0, grSm = 0, lastT = 0, drawnNeedle = 1e9;
    float grShown = 0, outShown = -20;
    juce::String drawnText;
};
} // namespace audiolab
