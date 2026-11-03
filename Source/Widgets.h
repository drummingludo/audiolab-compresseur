// Composants d'interface des plugins Audio Lab selon la charte V2 (fiche 02) :
// potentiomètres à arc (gros, moyen, petit), sélecteurs segmentés, bulles d'aide.
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

// Familles de sens (charte § 4) : teal = niveau / seuil (dégradé), teal2 = gain (plein), orange = temps (dégradé)
enum class Family { teal, teal2, orange };
inline juce::Colour familyStart (const Pal& p, Family f) { return f == Family::teal ? p.teal : f == Family::orange ? p.orange0 : p.teal2; }
inline juce::Colour familyEnd (const Pal& p, Family f)   { return f == Family::orange ? p.orange : p.teal2; }

// Capuchon (potentiomètres) : liseré de lumière en haut + deux ombres portées
inline void drawCap (juce::Graphics& g, const Pal& p, float cx, float cy, float d, bool small)
{
    juce::Path cap; cap.addEllipse (cx - d / 2, cy - d / 2, d, d);
    if (small)
    {
        softShadow (g, cap, juce::Colours::black.withAlpha (0.45f), 5, { 0, 2 });
        softShadow (g, cap, juce::Colours::black.withAlpha (0.5f), 2, { 0, 1 });
    }
    else
    {
        softShadow (g, cap, juce::Colours::black.withAlpha (0.5f), d > 60 ? 20 : 14, { 0, d > 60 ? 8 : 6 });
        softShadow (g, cap, juce::Colours::black.withAlpha (0.45f), 2, { 0, 1 });
    }
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (cap);
    g.setColour (juce::Colours::white.withAlpha (small ? 0.08f : 0.07f)); g.fillPath (cap);
    g.setColour (p.cap); g.fillEllipse (cx - d / 2, cy - d / 2 + 1, d, d);
}

// ── Potentiomètre à arc ──────────────────────────────────────────────────
// md : potentiomètre principal (arc Ø 98, épaisseur 3,2, capuchon 74, valeur au centre, nom dessous, aide facultative)
// sm : potentiomètre moyen (arc Ø 66, capuchon 50 ; le nom est posé par l'éditeur)
// xs : petit réglage de 28 px, nom et valeur à sa gauche (Mix)
class ArcKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Size { md, sm, xs };

    juce::String hint;                               // petite aide sous le nom (gros bouton)
    std::function<juce::String (float)> subFn;       // 2ᵉ ligne du capuchon à la place de l'unité (ex. « 50 ms »)
    float dragPixels = 220;                          // course de souris pour toute la plage (plus grand = plus précis)
    float wheelStep = 0.01f;                         // pas de la molette (fraction de la plage)

    ArcKnob (Theme& t, juce::RangedAudioParameter& prm, juce::String label, juce::String unit, Family family,
             Size size, std::function<juce::String (float)> format, float zeroValue, juce::String help = {})
        : theme (t), param (prm), labelText (std::move (label)), unitText (std::move (unit)), fam (family),
          sz (size), fmt (std::move (format)), zeroNorm (prm.convertTo0to1 (zeroValue)),
          attachment (prm, [this] (float) { repaint(); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (labelText);
        setTooltip (help);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        attachment.sendInitialUpdate();
    }

    // Largeur de la légende du petit réglage (nom au-dessus, valeur dessous)
    float captionWidth() const
    {
        const float wn = textWidth (theme.ui (9, 600, 0.04f), labelText.toUpperCase());
        const float wv = tabularWidth (theme, valueLine (param.convertFrom0to1 (1.0f)), 10, 400);
        return std::ceil (std::max (wn, wv));
    }
    float idealWidth() const { return sz == Size::xs ? captionWidth() + 6 + 28 : sz == Size::sm ? 76.0f : 143.0f; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        const bool xs = sz == Size::xs, sm = sz == Size::sm;
        const float cx = xs ? (float) getWidth() - 14.0f : getWidth() * 0.5f;
        const float cy = xs || sm ? getHeight() * 0.5f : 56.0f;
        const float r = xs ? 11.5f : sm ? 33.0f : 49.0f, sw = xs ? 2.4f : sm ? 3.0f : 3.2f;
        const float cd = xs ? 16.0f : sm ? 50.0f : 74.0f, k = xs ? 0.6f : sm ? 0.9f : 1.0f;
        const float A0 = -135.0f * juce::MathConstants<float>::pi / 180, A1 = -A0;
        const float n = param.getValue();
        auto ang = [&] (float t) { return A0 + (A1 - A0) * t; };
        auto pt = [&] (float a, float rad) { return juce::Point<float> (cx + std::sin (a) * rad, cy - std::cos (a) * rad); };
        const juce::Colour c0 = familyStart (p, fam), c1 = familyEnd (p, fam);

        // rail (bouts ronds), éclairci au survol
        juce::Path trk; trk.addCentredArc (cx, cy, r, r, 0, A0, A1, true);
        g.setColour (hover && isEnabled() ? p.railHi : p.rail);
        g.strokePath (trk, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // partie colorée : du départ à la valeur, dégradé en ligne droite entre les deux points,
        // bout mobile coupé droit, pastille de départ ronde, halo léger (renforcé pendant le réglage)
        const float a = ang (n), az = ang (zeroNorm);
        if (std::abs (a - az) >= 0.004f)
        {
            const auto z = pt (az, r), e = pt (a, r);
            auto brush = [&] (float alpha) -> juce::FillType
            {
                if (fam == Family::teal2 || z.getDistanceFrom (e) < 0.5f) return juce::FillType (c1.withMultipliedAlpha (alpha));
                return juce::FillType (juce::ColourGradient (c0.withMultipliedAlpha (alpha), z, c1.withMultipliedAlpha (alpha), e, false));
            };
            juce::Path arc; arc.addCentredArc (cx, cy, r, r, 0, std::min (a, az), std::max (a, az), true);
            // halo fin et discret (2 voiles : +1,6 et +3,2 px), à peine renforcé pendant le réglage
            const float boost = dragging ? 1.5f : 1.0f;
            const float extra[] = { 3.2f * k, 1.6f * k }, alpha[] = { 0.05f, 0.12f };
            for (int i = 0; i < 2; ++i)
            {
                g.setFillType (brush (std::min (1.0f, alpha[i] * boost)));
                g.strokePath (arc, juce::PathStrokeType (sw + extra[i], juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                g.setColour (c0.withMultipliedAlpha (std::min (1.0f, alpha[i] * boost)));
                const float d = sw + extra[i];
                g.fillEllipse (z.x - d / 2, z.y - d / 2, d, d);
            }
            g.setFillType (brush (1.0f));
            g.strokePath (arc, juce::PathStrokeType (sw, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            const float dr = xs ? 1.2f : sw / 2;
            g.setColour (c0); g.fillEllipse (z.x - dr, z.y - dr, 2 * dr, 2 * dr);
        }

        drawCap (g, p, cx, cy, cd, xs);
        // repère : trait fin, couleur de fin de l'arc, au bord du capuchon, dirigé vers la valeur
        {
            juce::Path m; m.startNewSubPath (pt (a, cd / 2)); m.lineTo (pt (a, cd / 2 - (xs ? 3.5f : sm ? 6.0f : 8.0f)));
            g.setColour (c1);
            g.strokePath (m, juce::PathStrokeType (xs ? 1.2f : 1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
        if (focused)
        {
            const float fr = r + sw / 2 + 3;
            g.setColour (p.teal2.withAlpha (0.6f)); g.drawEllipse (cx - fr, cy - fr, 2 * fr, 2 * fr, 1.5f);
        }

        const float v = param.convertFrom0to1 (n);
        if (xs)
        {
            // légende à gauche du petit réglage : NOM / valeur
            const float right = cx - 14 - 6;
            g.setColour (p.text); g.setFont (theme.ui (9, 600, 0.04f));
            g.drawText (labelText.toUpperCase(), juce::Rectangle<float> (0, cy - 12.5f, right, 11), juce::Justification::centredRight, false);
            g.setColour (p.text2);
            drawTabular (g, theme, valueLine (v), 10, 400, juce::Rectangle<float> (0, cy - 0.5f, right, 12), juce::Justification::right);
            return;
        }
        // valeur (14 px) + unité ou 2ᵉ ligne (11 px)
        g.setColour (p.text);
        drawTabular (g, theme, fmt (v), 14, 600, juce::Rectangle<float> (cx - cd / 2, cy - 16, cd, 18), juce::Justification::centred, -0.01f);
        g.setColour (p.soft);
        const juce::String second = subFn ? subFn (v) : unitText;
        if (subFn) drawTabular (g, theme, second, 11, 400, juce::Rectangle<float> (cx - cd / 2 - 6, cy + 2, cd + 12, 14), juce::Justification::centred);
        else { g.setFont (theme.ui (11)); g.drawText (second, juce::Rectangle<float> (cx - cd / 2, cy + 2, cd, 14), juce::Justification::centred, false); }
        if (sm) return;
        // nom (58 px sous le centre) + aide
        g.setColour (p.text); g.setFont (theme.ui (11, 600, 0.06f));
        g.drawText (labelText.toUpperCase(), juce::Rectangle<float> (0, cy + 58, (float) getWidth(), 13), juce::Justification::centredTop, false);
        if (hint.isNotEmpty())
        {
            g.setColour (p.text2); g.setFont (theme.ui (11));
            g.drawText (hint, juce::Rectangle<float> (0, cy + 74, (float) getWidth(), 14), juce::Justification::centredTop, false);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled()) return;
        y0 = e.position.y; v0 = param.getValue();
        attachment.beginGesture(); dragging = true; repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float kk = dragPixels * (e.mods.isShiftDown() ? 5.5f : 1.0f);   // Maj = réglage fin
        setNorm (v0 + (y0 - e.position.y) / kk, true);
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { attachment.endGesture(); dragging = false; repaint(); } }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) < 1e-6f || ! isEnabled()) return;
        setNorm (param.getValue() + (w.deltaY > 0 ? wheelStep : -wheelStep), false);
    }
    bool keyPressed (const juce::KeyPress& kp) override
    {
        const float st = 1.0f / 50;
        if (kp.isKeyCode (juce::KeyPress::upKey) || kp.isKeyCode (juce::KeyPress::rightKey)) { setNorm (param.getValue() + st, false); return true; }
        if (kp.isKeyCode (juce::KeyPress::downKey) || kp.isKeyCode (juce::KeyPress::leftKey)) { setNorm (param.getValue() - st, false); return true; }
        if (kp.isKeyCode (juce::KeyPress::homeKey)) { setNorm (0, false); return true; }
        if (kp.isKeyCode (juce::KeyPress::endKey)) { setNorm (1, false); return true; }
        return false;
    }
    void focusGained (FocusChangeType c) override { focused = c == focusChangedByTabKey; repaint(); }
    void focusLost (FocusChangeType) override { focused = false; repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        struct Value : juce::AccessibilityRangedNumericValueInterface
        {
            explicit Value (ArcKnob& kn) : knob (kn) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return knob.param.convertFrom0to1 (knob.param.getValue()); }
            void setValue (double v) override { knob.attachment.setValueAsCompleteGesture ((float) v); }
            AccessibleValueRange getRange() const override
            {
                const auto& rg = knob.param.getNormalisableRange();
                return { { rg.start, rg.end }, rg.interval > 0 ? rg.interval : 0.01 };
            }
            ArcKnob& knob;
        };
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
                                                              juce::AccessibilityActions(),
                                                              juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
    }

private:
    juce::String valueLine (float v) const { return fmt (v) + (unitText.isNotEmpty() ? " " + unitText : juce::String()); }
    void setNorm (float nn, bool partOfGesture)
    {
        nn = juce::jlimit (0.0f, 1.0f, nn);
        const float v = param.convertFrom0to1 (nn);
        if (partOfGesture) attachment.setValueAsPartOfGesture (v);
        else attachment.setValueAsCompleteGesture (v);
    }

    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText, unitText;
    Family fam;
    Size sz;
    std::function<juce::String (float)> fmt;
    float zeroNorm;
    juce::ParameterAttachment attachment;
    float y0 = 0, v0 = 0;
    bool dragging = false, focused = false, hover = false;
};

// ── Sélecteur segmenté ───────────────────────────────────────────────────
// Bande « Surface » (« Rails » sur une surface), marge 3 px, segments égaux ;
// segment choisi : pastille #005F73, texte #F9F9FB en 500.
class SegButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegButtons (Theme& t, juce::StringArray opts) : theme (t), options (std::move (opts)) {}

    bool vertical = false;      // boutons empilés (Ratio, Meter)
    bool onSurface = false;     // posé sur une surface : bande couleur « Rails »
    float fontSize = 12, gap = 2, padX = 12;
    // Icônes à la place du texte (facultatif) : le nom reste en bulle d'aide et pour l'accessibilité
    using Icon = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;
    std::vector<Icon> icons;

    // Relie à un paramètre « choix »
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
        float w = 0;
        for (auto& o : options) w = std::max (w, textWidth (theme.ui (fontSize, 500), o));
        return 6 + options.size() * (w + 2 * padX) + gap * (options.size() - 1);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = theme.pal();
        g.setColour (onSurface ? p.rail : p.surface); g.fillRoundedRectangle (getLocalBounds().toFloat(), 9);
        for (int i = 0; i < options.size(); ++i)
        {
            auto r = cell (i);
            const bool sel = i == selected;
            if (sel)
            {
                juce::Path pill; pill.addRoundedRectangle (r, 7);
                softShadow (g, pill, juce::Colours::black.withAlpha (0.18f), 3, { 0, 1 });
                g.setColour (p.tealSel); g.fillPath (pill);
            }
            const auto c = sel || (i == hover && isEnabled()) ? p.text : p.text2;
            if ((size_t) i < icons.size() && icons[(size_t) i]) { icons[(size_t) i] (g, r, c); continue; }
            g.setColour (c);
            g.setFont (theme.ui (fontSize, sel ? 500 : 400));
            g.drawText (options[i], r, juce::Justification::centred, false);
        }
    }
    // avec des icônes, la bulle d'aide donne le nom du segment survolé
    juce::String getTooltip() override
    {
        if (! icons.empty() && hover >= 0 && hover < options.size()) return options[hover];
        return SettableTooltipClient::getTooltip();
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h >= 0) { selected = h; repaint(); if (onSelect) onSelect (h); }
    }

private:
    juce::Rectangle<float> cell (int i) const
    {
        const int n = options.size();
        if (vertical)
        {
            const float h = ((float) getHeight() - 6 - gap * (n - 1)) / n;
            return { 3.0f, 3 + i * (h + gap), (float) getWidth() - 6, h };
        }
        const float w = ((float) getWidth() - 6 - gap * (n - 1)) / n;
        return { 3 + i * (w + gap), 3.0f, w, (float) getHeight() - 6 };
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

// ── Curseur (charte § 7) : libellé 68 px · piste · valeur 48 px ──────────
// Piste 4 px « Rails », partie avant le bouton remplie de la couleur de la famille (halo léger),
// bouton 18 px « Capuchon » ; rien de coloré à la valeur minimale ; désactivé : piste en gris doux.
class SliderRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    SliderRow (Theme& t, juce::RangedAudioParameter& prm, juce::String label, Family family, std::function<juce::String (float)> format)
        : theme (t), param (prm), labelText (std::move (label)), fam (family), fmt (std::move (format)),
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
        const float h = (float) getHeight(), cy = h * 0.5f;
        g.setColour (dimmed ? p.soft : p.text2); g.setFont (theme.ui (12));
        g.drawText (labelText, juce::Rectangle<float> (0, 0, 68, h), juce::Justification::centredLeft, false);
        auto tr = track();
        g.setColour (hover && ! dimmed ? p.railHi : p.rail); g.fillRoundedRectangle (tr.getX(), cy - 2, tr.getWidth(), 4, 2);
        const float n = param.getValue();
        const float x = tr.getX() + 9 + (tr.getWidth() - 18) * n;
        if (n > 0.001f)
        {
            // partie avant le bouton : teal en dégradé, orange plein, teal secondaire plein (posé sur une surface)
            const juce::Rectangle<float> f (tr.getX(), cy - 2, x - tr.getX(), 4);
            juce::FillType ft;
            if (dimmed) ft = juce::FillType (p.soft);
            else if (fam == Family::teal) ft = juce::FillType (juce::ColourGradient (p.teal, f.getX(), 0, p.teal2, f.getRight(), 0, false));
            else ft = juce::FillType (fam == Family::orange ? p.orange : p.teal2);
            if (! dimmed)
            {
                const auto glow = fam == Family::orange ? p.orange : p.teal2;
                g.setColour (glow.withAlpha (0.05f)); g.fillRoundedRectangle (f.expanded (1.6f), 3.6f);   // halo fin et discret
                g.setColour (glow.withAlpha (0.12f)); g.fillRoundedRectangle (f.expanded (0.8f), 2.8f);
            }
            g.setFillType (ft); g.fillRoundedRectangle (f, 2);
        }
        juce::Path th; th.addEllipse (x - 9, cy - 9, 18, 18);
        softShadow (g, th, juce::Colours::black.withAlpha (0.5f), 6, { 0, 2 });
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (th);
            g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillPath (th);
            g.setColour (p.cap); g.fillEllipse (x - 9, cy - 8, 18, 18);
        }
        g.setColour (juce::Colours::black.withAlpha (0.4f)); g.strokePath (th, juce::PathStrokeType (0.5f));
        if (focused) { g.setColour (p.teal2.withAlpha (0.5f)); g.drawEllipse (x - 10.5f, cy - 10.5f, 21, 21, 1.5f); }
        g.setColour (dimmed ? p.soft : p.text);
        drawTabular (g, theme, fmt (param.convertFrom0to1 (n)), 12, 400,
                     juce::Rectangle<float> ((float) getWidth() - 48, 0, 48, h), juce::Justification::right);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { attachment.beginGesture(); drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override { attachment.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto& rg = param.getNormalisableRange();
        const float st = rg.interval > 0 ? rg.interval / (rg.end - rg.start) : 0.01f;
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
    juce::Rectangle<float> track() const { return { 78.0f, 0, (float) getWidth() - 78 - 58, (float) getHeight() }; }
    void drag (const juce::MouseEvent& e)
    {
        auto tr = track();
        const float n = juce::jlimit (0.0f, 1.0f, (e.position.x - tr.getX() - 9) / (tr.getWidth() - 18));
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
    }
    Theme& theme;
    juce::RangedAudioParameter& param;
    juce::String labelText;
    Family fam;
    std::function<juce::String (float)> fmt;
    juce::ParameterAttachment attachment;
    bool dimmed = false, focused = false, hover = false;
};

// ── Bouton à icône (ex. « Écouter ») : fond « Capuchon », actif = #005F73 ──
class PillButton : public juce::Button
{
public:
    PillButton (Theme& t, juce::String label) : juce::Button (label), theme (t) {}
    bool on = false;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& p = theme.pal();
        auto b = getLocalBounds().toFloat();
        g.setColour (on ? p.tealSel : (over || down) ? p.railHi : p.cap); g.fillRoundedRectangle (b, 8);
        g.setFont (theme.ui (12, on ? 500 : 400));
        const auto txt = getButtonText();
        const float tw = textWidth (g.getCurrentFont(), txt);
        const float x0 = b.getCentreX() - ((icon ? 13 + 6 : 0) + tw) / 2;
        if (icon) icon (g, juce::Rectangle<float> (x0, b.getY(), 13, b.getHeight()), p.text);
        g.setColour (p.text);
        g.drawText (txt, juce::Rectangle<float> (x0 + (icon ? 19 : 0), b.getY(), tw + 4, b.getHeight()), juce::Justification::centredLeft, false);
    }
    float idealWidth() const { return textWidth (theme.ui (12, 500), getButtonText()) + 20 + (icon ? 19 : 0); }

private:
    Theme& theme;
};

// ── Flèche d'ouverture (« Réglages avancés ») : fermé = texte secondaire, ouvert = texte ──
class Disclosure : public juce::Button
{
public:
    Disclosure (Theme& t, juce::String label) : juce::Button (label), theme (t) {}
    bool open = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& p = theme.pal();
        const juce::Colour c = open || over ? p.text : p.text2;
        const float x = 8, y = getHeight() * 0.5f - 6;   // boîte 12 × 12 du chevron
        juce::Path ch;
        if (open) { ch.startNewSubPath (x + 2.5f, y + 4.5f); ch.lineTo (x + 6, y + 8); ch.lineTo (x + 9.5f, y + 4.5f); }
        else      { ch.startNewSubPath (x + 4.5f, y + 2.5f); ch.lineTo (x + 8, y + 6); ch.lineTo (x + 4.5f, y + 9.5f); }
        g.setColour (c);
        g.strokePath (ch, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (theme.ui (12));
        g.drawText (getButtonText(), juce::Rectangle<float> (26, 0, (float) getWidth() - 26, (float) getHeight()), juce::Justification::centredLeft, false);
    }
    float idealWidth() const { return 26 + textWidth (theme.ui (12), getButtonText()) + 8; }

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
        const float w = textWidth (theme.ui (13, 600), text) + 36, h = 34;
        auto r = juce::Rectangle<float> ((getWidth() - w) / 2, (float) getHeight() - h - 14, w, h);
        juce::Path box; box.addRoundedRectangle (r, 10);
        softShadow (g, box, juce::Colours::black.withAlpha (0.4f), 18, { 0, 6 });
        g.setColour (p.surface); g.fillPath (box);
        g.setColour (p.rail); g.strokePath (box, juce::PathStrokeType (1));
        g.setColour (good ? p.teal2 : p.text); g.setFont (theme.ui (13, 600));
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

// ── Bulles d'aide et menus ───────────────────────────────────────────────
class ALLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit ALLookAndFeel (Theme& t) : theme (t)
    {
        const auto& p = theme.pal();
        setColour (juce::PopupMenu::backgroundColourId, p.surface);
        setColour (juce::PopupMenu::textColourId, p.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.tealSel);
        setColour (juce::PopupMenu::highlightedTextColourId, p.text);
        setColour (juce::TooltipWindow::backgroundColourId, p.surface);
        setColour (juce::TooltipWindow::textColourId, p.text);
        setColour (juce::TooltipWindow::outlineColourId, p.rail);
        setColour (juce::ComboBox::textColourId, p.text);
    }
    // Menu déroulant (ex. Side-chain) : fond « Capuchon », coins 8, chevron en texte secondaire
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto& p = theme.pal();
        auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.setColour (box.isMouseOver (true) ? p.railHi : p.cap); g.fillRoundedRectangle (b, 8);
        if (box.hasKeyboardFocus (true)) { g.setColour (p.teal2.withAlpha (0.5f)); g.drawRoundedRectangle (b.reduced (1), 8, 1.5f); }
        juce::Path chev; const float cx = (float) w - 13, cy = h * 0.5f;
        chev.startNewSubPath (cx - 3, cy - 1.5f); chev.lineTo (cx, cy + 1.5f); chev.lineTo (cx + 3, cy - 1.5f);
        g.setColour (p.text2); g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme.ui (12); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBorderSize ({ 0, 0, 0, 0 });
        label.setBounds (10, 0, box.getWidth() - 26, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return theme.ui (13); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto& p = theme.pal();
        g.fillAll (p.surface);
        g.setColour (p.rail); g.drawRect (0, 0, w, h);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& tip, int w, int h) override
    {
        const auto& p = theme.pal();
        g.setColour (p.surface); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6);
        g.setColour (p.rail); g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6, 1);
        g.setColour (p.text); g.setFont (theme.ui (12));
        g.drawFittedText (tip, juce::Rectangle<int> (0, 0, w, h).reduced (9, 5), juce::Justification::centredLeft, 5);
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::GlyphArrangement ga; ga.addFittedText (theme.ui (12), tip, 0, 0, 260, 200, juce::Justification::topLeft, 5);
        const int w = juce::jmin (280, (int) ga.getBoundingBox (0, -1, true).getWidth() + 20);
        const int h = (int) ga.getBoundingBox (0, -1, true).getHeight() + 12;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12, pos.y + 14, w, h).constrainedWithin (parent);
    }

private:
    Theme& theme;
};
} // namespace audiolab
