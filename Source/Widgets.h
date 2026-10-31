// Composants d'interface d'Audio Lab, reproduits pour le plugin :
// potentiomètres à arc, Dry/Wet, boutons segmentés, curseurs, boutons, menus, toast.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace audiolab
{
// Ombre douce façon CSS box-shadow
inline void softShadow (juce::Graphics& g, const juce::Path& p, juce::Colour c, int radius, juce::Point<int> offset)
{
    juce::DropShadow (c, radius, offset).drawForPath (g, p);
}

// Halo léger autour d'un trait (≈ flou gaussien de 2 px, opacité 55 %, comme la maquette validée)
inline void glowStroke (juce::Graphics& g, const juce::Path& path, juce::Colour c, float width, float k = 1.0f)
{
    const float extra[] = { 6.0f * k, 3.6f * k, 1.6f * k };
    const float alpha[] = { 0.05f, 0.10f, 0.18f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (c.withMultipliedAlpha (alpha[i]));
        g.strokePath (path, juce::PathStrokeType (width + extra[i], juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
}

// ── Potentiomètre à arc (opKnob) ─────────────────────────────────────────
// md : gros bouton (arc Ø 98, capuchon 74, valeur au centre, nom dessous)
// xs : petit bouton de 28 px, légende (nom + valeur) à sa gauche — utilisé pour le Make-up
class ArcKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Size { md, xs };

    ArcKnob (Theme& t, juce::RangedAudioParameter& prm, juce::String label, juce::String unit,
             std::function<juce::Colour (const Pal&)> colour, Size size, std::function<juce::String (float)> format,
             float zeroValue, juce::String help = {})
        : theme (t), param (prm), labelText (std::move (label)), unitText (std::move (unit)),
          colourFn (std::move (colour)), sz (size), fmt (std::move (format)),
          zeroNorm (prm.convertTo0to1 (zeroValue)),
          attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (labelText);
        setTooltip (help);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        attachment.sendInitialUpdate();
    }

    // Largeur de la légende du petit bouton (nom au-dessus, valeur dessous)
    float captionWidth() const
    {
        auto w = [] (const juce::Font& f, const juce::String& t) { juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0); return ga.getBoundingBox (0, -1, true).getWidth(); };
        return std::ceil (std::max (w (theme.ui (9, 600, 0.04f), labelText.toUpperCase()),
                                    w (theme.mono (10), fmt (param.convertFrom0to1 (1.0f)) + " " + unitText)));
    }
    float idealWidth() const { return sz == Size::xs ? captionWidth() + 6 + 28 : 143.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const bool xs = sz == Size::xs;
        const float cx = xs ? (float) getWidth() - 14.0f : getWidth() * 0.5f;
        const float cy = xs ? getHeight() * 0.5f : 56.0f;
        const float r = xs ? 11.5f : 49.0f, sw = xs ? 2.4f : 5.2f, cd = xs ? 16.0f : 74.0f, k = xs ? 0.6f : 1.0f;
        const float A0 = -135.0f * juce::MathConstants<float>::pi / 180, A1 = -A0;
        const float n = param.getValue();
        auto ang = [&] (float t) { return A0 + (A1 - A0) * t; };
        auto pt = [&] (float a, float rad) { return juce::Point<float> (cx + std::sin (a) * rad, cy - std::cos (a) * rad); };
        const juce::Colour col = colourFn (p);

        // rail gris (bouts ronds)
        juce::Path trk; trk.addCentredArc (cx, cy, r, r, 0, A0, A1, true);
        g.setColour (p.bg4); g.strokePath (trk, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // arc de valeur : bout mobile coupé droit, départ arrondi, léger halo
        const float a = ang (n), az = ang (zeroNorm);
        if (std::abs (a - az) >= 0.004f)
        {
            juce::Path arc; arc.addCentredArc (cx, cy, r, r, 0, std::min (a, az), std::max (a, az), true);
            const auto z = pt (az, r);
            juce::Path dot; dot.addEllipse (z.x - sw / 2, z.y - sw / 2, sw, sw);
            glowStroke (g, arc, col, sw, k);
            for (float e : { 6.0f * k, 3.6f * k, 1.6f * k })
            {
                g.setColour (col.withMultipliedAlpha (e > 5 * k ? 0.05f : e > 3 * k ? 0.10f : 0.18f));
                g.fillEllipse (z.x - (sw + e) / 2, z.y - (sw + e) / 2, sw + e, sw + e);
            }
            g.setColour (col);
            g.strokePath (arc, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            g.fillPath (dot);
        }
        // capuchon
        juce::Path cap; cap.addEllipse (cx - cd / 2, cy - cd / 2, cd, cd);
        if (xs)
        {
            softShadow (g, cap, juce::Colours::black.withAlpha (p.dark ? 0.45f : 0.16f), 5, { 0, 2 });
            softShadow (g, cap, juce::Colours::black.withAlpha (p.dark ? 0.4f : 0.10f), 2, { 0, 1 });
        }
        else
        {
            softShadow (g, cap, juce::Colours::black.withAlpha (p.dark ? 0.45f : 0.14f), 20, { 0, 8 });
            softShadow (g, cap, juce::Colours::black.withAlpha (p.dark ? 0.4f : 0.08f), 2, { 0, 1 });
        }
        g.setColour (p.bg2); g.fillPath (cap);
        // repère : trait fin collé au bord du capuchon, dirigé vers la valeur
        {
            juce::Path m; const auto p1 = pt (a, cd / 2), p2 = pt (a, cd / 2 - (xs ? 3.5f : 8.0f));
            m.startNewSubPath (p1); m.lineTo (p2);
            g.setColour (col); g.strokePath (m, juce::PathStrokeType (xs ? 1.2f : 1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
        if (focused)
        {
            const float fr = r + sw / 2 + 3;
            g.setColour (p.accent.withAlpha (0.6f)); g.drawEllipse (cx - fr, cy - fr, 2 * fr, 2 * fr, 2.0f);
        }
        if (xs)
        {
            // légende à gauche du petit bouton : NOM / valeur
            const float right = cx - 14 - 6;
            g.setColour (p.text); g.setFont (theme.ui (9, 600, 0.04f));
            g.drawText (labelText.toUpperCase(), juce::Rectangle<float> (0, cy - 12.5f, right, 11), juce::Justification::centredRight, false);
            g.setColour (p.dim); g.setFont (theme.mono (10));
            g.drawText (fmt (param.convertFrom0to1 (n)) + " " + unitText, juce::Rectangle<float> (0, cy - 0.5f, right, 12), juce::Justification::centredRight, false);
            return;
        }
        // valeur + unité
        g.setColour (p.text); g.setFont (theme.ui (18, 600, -0.02f));
        g.drawText (fmt (param.convertFrom0to1 (n)), juce::Rectangle<float> (cx - cd / 2, cy - 18 * 0.78f, cd, 18 * 1.2f), juce::Justification::centred, false);
        g.setColour (p.muted); g.setFont (theme.ui (11));
        g.drawText (unitText, juce::Rectangle<float> (cx - cd / 2, cy + 18 * 0.38f, cd, 11 * 1.3f), juce::Justification::centred, false);
        // libellé (58 px sous le centre)
        g.setColour (p.text); g.setFont (theme.ui (11, 600, 0.06f));
        g.drawText (labelText.toUpperCase(), juce::Rectangle<float> (0, cy + 58, (float) getWidth(), 14), juce::Justification::centredTop, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        y0 = e.position.y; v0 = param.getValue();
        attachment.beginGesture(); dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float k = e.mods.isShiftDown() ? 1200.0f : 220.0f;   // Maj = réglage fin
        setNorm (v0 + (y0 - e.position.y) / k, true);
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { attachment.endGesture(); dragging = false; } }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) < 1e-6f || ! isEnabled()) return;
        setNorm (param.getValue() + (w.deltaY > 0 ? 0.01f : -0.01f), false);
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const float st = 1.0f / 50;
        if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::rightKey)) { setNorm (param.getValue() + st, false); return true; }
        if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::leftKey)) { setNorm (param.getValue() - st, false); return true; }
        if (k.isKeyCode (juce::KeyPress::homeKey)) { setNorm (0, false); return true; }
        if (k.isKeyCode (juce::KeyPress::endKey)) { setNorm (1, false); return true; }
        return false;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        struct Value : juce::AccessibilityRangedNumericValueInterface
        {
            explicit Value (ArcKnob& k) : knob (k) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return knob.param.convertFrom0to1 (knob.param.getValue()); }
            void setValue (double v) override { knob.attachment.setValueAsCompleteGesture ((float) v); }
            AccessibleValueRange getRange() const override
            {
                const auto& r = knob.param.getNormalisableRange();
                return { { r.start, r.end }, r.interval > 0 ? r.interval : 0.01 };
            }
            ArcKnob& knob;
        };
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
                                                              juce::AccessibilityActions(),
                                                              juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
    }

private:
    void setNorm (float n, bool partOfGesture)
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        const float v = param.convertFrom0to1 (n);
        if (partOfGesture) attachment.setValueAsPartOfGesture (v);
        else attachment.setValueAsCompleteGesture (v);
    }

    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText, unitText;
    std::function<juce::Colour (const Pal&)> colourFn;
    Size sz;
    std::function<juce::String (float)> fmt;
    float zeroNorm;
    juce::ParameterAttachment attachment;
    float y0 = 0, v0 = 0;
    bool dragging = false, focused = false;
};

// ── Dry/Wet (dknob) ─────────────────────────────────────────────────────
class MixKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    MixKnob (Theme& t, juce::RangedAudioParameter& prm)
        : theme (t), param (prm), attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setTooltip (fr ("Dry / Wet — glisser verticalement"));
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTitle ("Dry/Wet");
        attachment.sendInitialUpdate();
    }
    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const float v = param.convertFrom0to1 (param.getValue()), n = v / 100;
        const float x = getWidth() * 0.5f, y = 19, r = 15;
        g.setColour (p.cv); g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
        g.setColour (p.grid); g.drawEllipse (x - r, y - r, 2 * r, 2 * r, 1);
        // arc de 135° à 405° (sens horaire depuis 3 h) = -135°…+135° depuis midi
        const float A0 = juce::MathConstants<float>::pi * 1.25f;
        juce::Path bgArc; bgArc.addCentredArc (x, y, r - 2, r - 2, 0, A0, A0 + 1.5f * juce::MathConstants<float>::pi, true);
        g.setColour (p.grid); g.strokePath (bgArc, juce::PathStrokeType (2.5f));
        if (n > 0.001f)
        {
            juce::Path a; a.addCentredArc (x, y, r - 2, r - 2, 0, A0, A0 + n * 1.5f * juce::MathConstants<float>::pi, true);
            g.setColour (p.accent); g.strokePath (a, juce::PathStrokeType (2.5f));
        }
        const float ang = A0 + n * 1.5f * juce::MathConstants<float>::pi;
        g.setColour (p.txt2);
        g.drawLine (x, y, x + std::sin (ang) * (r - 5), y - std::cos (ang) * (r - 5), 1.5f);
        g.setColour (p.muted); g.setFont (theme.mono (9));
        g.drawText (juce::String (juce::roundToInt (v)) + "%", juce::Rectangle<float> (0, 39, (float) getWidth(), 12), juce::Justification::centred, false);
    }
    void mouseDown (const juce::MouseEvent& e) override { sy = e.position.y; sv = param.convertFrom0to1 (param.getValue()); attachment.beginGesture(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float nv = juce::jlimit (0.0f, 100.0f, sv + (sy - e.position.y) * (e.mods.isShiftDown() ? 0.2f : 0.8f));
        attachment.setValueAsPartOfGesture (nv);
    }
    void mouseUp (const juce::MouseEvent&) override { attachment.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (100.0f); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) < 1e-6f) return;
        attachment.setValueAsCompleteGesture (juce::jlimit (0.0f, 100.0f, param.convertFrom0to1 (param.getValue()) + (w.deltaY > 0 ? 1.0f : -1.0f)));
    }

private:
    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float sy = 0, sv = 0;
};

// ── Boutons segmentés (.wbtns .wb) ───────────────────────────────────────
class SegButtons : public juce::Component
{
public:
    SegButtons (Theme& t, juce::StringArray opts) : theme (t), options (std::move (opts)) {}

    bool equalWidths = false;   // segments de même largeur (sélecteur de vue)
    bool onPanel = false;       // posé sur le panneau gris : bande un ton plus foncé
    float fontSize = 12, gap = 4, padX = 10;

    // Relie à un paramètre « choix » (Peak/RMS…)
    void attach (juce::RangedAudioParameter& prm)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (prm, [this, &prm] (float) {
            selected = juce::roundToInt (prm.convertFrom0to1 (prm.getValue())); repaint(); }, nullptr);
        onSelect = [this] (int i) { attachment->setValueAsCompleteGesture ((float) i); };
        attachment->sendInitialUpdate();
    }

    void setSelected (int i) { selected = i; repaint(); }
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect;

    float idealWidth() const
    {
        float w = 6;
        for (auto& o : options) w += textWidth (o) + 2 * padX + gap;
        return w - gap;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        auto b = getLocalBounds().toFloat();
        g.setColour (onPanel ? p.bg4 : p.bg3); g.fillRoundedRectangle (b, 9);
        for (int i = 0; i < options.size(); ++i)
        {
            auto r = cell (i);
            if (i == selected)
            {
                juce::Path pill; pill.addRoundedRectangle (r, 7);
                softShadow (g, pill, juce::Colours::black.withAlpha (0.12f), 3, { 0, 1 });
                g.setColour (p.bg2); g.fillPath (pill);
            }
            g.setColour (i == selected || i == hover ? p.text : p.dim);
            g.setFont (theme.ui (fontSize));
            g.drawText (options[i], r, juce::Justification::centred, false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h >= 0) { selected = h; repaint(); if (onSelect) onSelect (h); }
    }

private:
    float textWidth (const juce::String& s) const
    {
        juce::GlyphArrangement ga; ga.addLineOfText (theme.ui (fontSize), s, 0, 0);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }
    juce::Rectangle<float> cell (int i) const
    {
        const int n = options.size();
        if (equalWidths)
        {
            const float w = ((float) getWidth() - 6 - gap * (n - 1)) / n;
            return { 3 + i * (w + gap), 3.0f, w, (float) getHeight() - 6 };
        }
        float x = 3;
        for (int k = 0; k < i; ++k) x += textWidth (options[k]) + 2 * padX + gap;
        return { x, 3.0f, textWidth (options[i]) + 2 * padX, (float) getHeight() - 6 };
    }
    int hit (juce::Point<float> pt) const
    {
        for (int i = 0; i < options.size(); ++i) if (cell (i).contains (pt)) return i;
        return -1;
    }
    Theme& theme;
    juce::StringArray options;
    int selected = 0, hover = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ── Curseur horizontal (.slr) : libellé · piste · valeur ─────────────────
class SliderRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    SliderRow (Theme& t, juce::RangedAudioParameter& prm, juce::String label, std::function<juce::String (float)> format)
        : theme (t), param (prm), labelText (std::move (label)), fmt (std::move (format)),
          attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (labelText);
        attachment.sendInitialUpdate();
    }
    void setDimmed (bool d) { if (dimmed != d) { dimmed = d; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const float a = dimmed ? 0.4f : 1.0f;
        const float h = (float) getHeight(), cy = h * 0.5f;
        g.setColour (p.dim.withMultipliedAlpha (a)); g.setFont (theme.ui (12));
        g.drawText (labelText, juce::Rectangle<float> (0, 0, 64, h), juce::Justification::centredLeft, false);
        auto tr = track();
        g.setColour (p.bg4.withMultipliedAlpha (a)); g.fillRoundedRectangle (tr.getX(), cy - 2, tr.getWidth(), 4, 2);
        const float x = tr.getX() + 9 + (tr.getWidth() - 18) * param.getValue();
        juce::Path th; th.addEllipse (x - 9, cy - 9, 18, 18);
        softShadow (g, th, juce::Colours::black.withAlpha (0.2f * a), 6, { 0, 2 });
        g.setColour (juce::Colours::white.withMultipliedAlpha (a)); g.fillPath (th);
        g.setColour (juce::Colours::black.withAlpha (0.08f * a)); g.strokePath (th, juce::PathStrokeType (0.5f));
        if (focused) { g.setColour (p.accent.withAlpha (0.3f)); g.drawEllipse (x - 10.5f, cy - 10.5f, 21, 21, 3); }
        g.setColour (p.text.withMultipliedAlpha (a)); g.setFont (theme.mono (12));
        g.drawText (fmt (param.convertFrom0to1 (param.getValue())), juce::Rectangle<float> ((float) getWidth() - 48, 0, 48, h), juce::Justification::centredRight, false);
    }
    void mouseDown (const juce::MouseEvent& e) override { attachment.beginGesture(); drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override { attachment.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const float st = param.getNormalisableRange().interval > 0
                           ? param.getNormalisableRange().interval / (param.getNormalisableRange().end - param.getNormalisableRange().start) : 0.01f;
        float n = param.getValue();
        if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::upKey)) n += std::max (st, 0.001f);
        else if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::downKey)) n -= std::max (st, 0.001f);
        else return false;
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
        return true;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }

private:
    juce::Rectangle<float> track() const { return { 74.0f, 0, (float) getWidth() - 74 - 58, (float) getHeight() }; }
    void drag (const juce::MouseEvent& e)
    {
        auto tr = track();
        const float n = juce::jlimit (0.0f, 1.0f, (e.position.x - tr.getX() - 9) / (tr.getWidth() - 18));
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
    }
    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText;
    std::function<juce::String (float)> fmt;
    juce::ParameterAttachment attachment;
    bool dimmed = false, focused = false;
};

// ── Boutons (ibtn, Reset, On/Bypass, A/B/C) ──────────────────────────────
class PillButton : public juce::Button
{
public:
    enum class Style { ibtn, rst, bp, snap };
    PillButton (Theme& t, Style s, juce::String label) : juce::Button (label), theme (t), style (s) {}

    bool on = false;          // ibtn « actif », bp « On », snap « courant »
    bool has = false;         // snap : contient un réglage
    bool armed = false;       // snap : prêt à enregistrer
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& p = theme.pal();
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        juce::Colour bg = style == Style::ibtn ? p.bg2 : p.bg3, fg = p.text, br = juce::Colours::transparentBlack;
        float rad = 8;
        juce::String txt = getButtonText();
        if (style == Style::rst) fg = p.dim;
        if (style == Style::ibtn && on) { bg = p.accent; fg = juce::Colours::white; }
        if (style == Style::bp)
        {
            rad = b.getHeight() / 2;
            if (on) { bg = p.green; br = p.green; fg = juce::Colours::white; txt = "On"; }
            else { bg = p.bg3; br = p.border; fg = p.muted; txt = "Bypass"; }
        }
        if (style == Style::snap)
        {
            rad = 7; bg = juce::Colours::transparentBlack; fg = p.muted;
            if (has) { bg = p.bg3; fg = p.text; }
            if (on) { bg = p.accent; fg = juce::Colours::white; }
        }
        if ((over || down) && style != Style::bp && ! (style == Style::ibtn && on) && ! (style == Style::snap && on)) bg = p.bg4;
        g.setColour (bg); g.fillRoundedRectangle (b, rad);
        if (style == Style::snap && ! on)
        {
            juce::Path outline; outline.addRoundedRectangle (b, rad);
            if (has) { g.setColour (p.border); g.strokePath (outline, juce::PathStrokeType (1)); }
            else
            {
                juce::Path dashed; const float d[] = { 3, 3 };
                juce::PathStrokeType (1).createDashedStroke (dashed, outline, d, 2);
                g.setColour (p.dim.withAlpha (0.45f)); g.fillPath (dashed);
            }
        }
        else if (! br.isTransparent()) { g.setColour (br); g.drawRoundedRectangle (b, rad, 1); }

        if (style == Style::snap && armed)
        {
            g.setColour (p.red); g.fillEllipse (b.getCentreX() - 4, b.getCentreY() - 4, 8, 8);
            return;
        }
        g.setFont (style == Style::bp ? theme.ui (11, 600) : style == Style::snap ? theme.ui (11, 600) : theme.ui (12, 400));
        if (icon)
        {
            // [10 px][icône 13 px][6 px][texte][10 px], le tout centré dans le bouton
            juce::GlyphArrangement ga; ga.addLineOfText (g.getCurrentFont(), txt, 0, 0);
            const float tw = ga.getBoundingBox (0, -1, true).getWidth();
            const float x0 = b.getCentreX() - (13 + 6 + tw) / 2;
            icon (g, juce::Rectangle<float> (x0, b.getY(), 13, b.getHeight()), fg);
            g.setColour (fg);
            g.drawText (txt, juce::Rectangle<float> (x0 + 19, b.getY(), tw + 4, b.getHeight()), juce::Justification::centredLeft, false);
            return;
        }
        g.setColour (fg);
        g.drawText (txt, b, juce::Justification::centred, false);
    }

    float idealWidth() const
    {
        if (style == Style::snap) return 26;
        const juce::String t = style == Style::bp ? juce::String ("Bypass") : getButtonText();
        juce::GlyphArrangement ga; ga.addLineOfText (style == Style::bp ? theme.ui (11, 600) : theme.ui (12), t, 0, 0);
        return ga.getBoundingBox (0, -1, true).getWidth() + (style == Style::bp ? 20 : 20) + (icon ? 19 : 0);
    }

private:
    Theme& theme;
    Style style;
};

// ── Flèche d'ouverture (« Réglages avancés ») ────────────────────────────
class Disclosure : public juce::Button
{
public:
    Disclosure (Theme& t, juce::String label) : juce::Button (label), theme (t) {}
    bool open = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& p = theme.pal();
        const juce::Colour c = open || over ? p.text : p.dim;
        const float x = 8, y = getHeight() * 0.5f - 6;   // boîte 12 × 12 du chevron
        juce::Path ch;
        if (open) { ch.startNewSubPath (x + 2.5f, y + 4.5f); ch.lineTo (x + 6, y + 8); ch.lineTo (x + 9.5f, y + 4.5f); }
        else      { ch.startNewSubPath (x + 4.5f, y + 2.5f); ch.lineTo (x + 8, y + 6); ch.lineTo (x + 4.5f, y + 9.5f); }
        g.setColour (c);
        g.strokePath (ch, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (theme.ui (12));
        g.drawText (getButtonText(), juce::Rectangle<float> (26, 0, (float) getWidth() - 26, (float) getHeight()), juce::Justification::centredLeft, false);
    }
    float idealWidth() const
    {
        juce::GlyphArrangement ga; ga.addLineOfText (theme.ui (12), getButtonText(), 0, 0);
        return 26 + ga.getBoundingBox (0, -1, true).getWidth() + 8;
    }

private:
    Theme& theme;
};

// ── Menus déroulants (select) ────────────────────────────────────────────
class ALLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit ALLookAndFeel (Theme& t) : theme (t) { refresh(); }
    void refresh()
    {
        const auto& p = theme.pal();
        setColour (juce::ComboBox::textColourId, p.text);
        setColour (juce::PopupMenu::backgroundColourId, p.bg2);
        setColour (juce::PopupMenu::textColourId, p.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, p.bg2);
        setColour (juce::TooltipWindow::textColourId, p.text);
        setColour (juce::TooltipWindow::outlineColourId, p.border);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto& p = theme.pal();
        auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (box.isMouseOver (true) ? p.bg4 : p.bg2); g.fillRoundedRectangle (b, 8);
        if (box.hasKeyboardFocus (true)) { g.setColour (p.accent.withAlpha (0.3f)); g.drawRoundedRectangle (b.reduced (1), 8, 2); }
        juce::Path chev; const float cx = (float) w - 13, cy = h * 0.5f;
        chev.startNewSubPath (cx - 4, cy - 2); chev.lineTo (cx, cy + 2); chev.lineTo (cx + 4, cy - 2);
        g.setColour (p.dim); g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme.ui (12); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBorderSize ({ 0, 0, 0, 0 });
        label.setBounds (10, 0, box.getWidth() - 30, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return theme.ui (13); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto& p = theme.pal();
        g.fillAll (p.bg2);
        g.setColour (p.border); g.drawRect (0, 0, w, h);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
    {
        const auto& p = theme.pal();
        g.setColour (p.bg2); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
        g.setColour (p.border); g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6, 1);
        g.setColour (p.text); g.setFont (theme.ui (12));
        g.drawFittedText (tip, juce::Rectangle<int> (0, 0, w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::GlyphArrangement ga; ga.addFittedText (theme.ui (12), tip, 0, 0, 260, 200, juce::Justification::topLeft, 4);
        const int w = juce::jmin (276, (int) ga.getBoundingBox (0, -1, true).getWidth() + 18);
        const int h = (int) ga.getBoundingBox (0, -1, true).getHeight() + 10;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y + 14, w, h).constrainedWithin (parent);
    }

private:
    Theme& theme;
};

// ── Message bref (toast) ─────────────────────────────────────────────────
class Toast : public juce::Component, private juce::Timer
{
public:
    explicit Toast (Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); setAlpha (0); }
    void show (const juce::String& s, bool ok = false)
    {
        text = s; good = ok; shownAt = juce::Time::getMillisecondCounter();
        setAlpha (1); setVisible (true); toFront (false); repaint(); startTimerHz (30);
    }
    void paint (juce::Graphics& g) override
    {
        if (text.isEmpty()) return;
        const auto& p = theme.pal();
        juce::GlyphArrangement ga; ga.addLineOfText (theme.ui (13, 600), text, 0, 0);
        const float w = ga.getBoundingBox (0, -1, true).getWidth() + 36, h = 34;
        auto r = juce::Rectangle<float> ((getWidth() - w) / 2, (float) getHeight() - h - 14, w, h);
        juce::Path box; box.addRoundedRectangle (r, 10);
        softShadow (g, box, juce::Colours::black.withAlpha (0.25f), 18, { 0, 6 });
        g.setColour (p.dark ? p.bg3 : p.text); g.fillPath (box);
        g.setColour (good ? p.green : (p.dark ? p.text : p.bg2));
        g.drawText (text, r, juce::Justification::centred, false);
    }

private:
    void timerCallback() override
    {
        const auto el = juce::Time::getMillisecondCounter() - shownAt;
        if (el > 2200) { setAlpha (0); stopTimer(); text.clear(); return; }
        setAlpha (el > 1800 ? 1.0f - (el - 1800) / 400.0f : 1.0f);
    }
    Theme& theme;
    juce::String text;
    bool good = false;
    juce::uint32 shownAt = 0;
};
} // namespace audiolab
