// Charte UI des plugins Audio Lab (fiche 02, V2 du 01/11/2026) : thème sombre unique, police Poppins.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace audiolab
{
// Couleurs de la charte (C = charte Transversal, D = teinte dérivée)
struct Pal
{
    juce::Colour bg       { 0xFF1D1E1C };   // fond fenêtre, fond des pistes (C)
    juce::Colour surface  { 0xFF2A2B29 };   // écrans, bandes, panneaux (D)
    juce::Colour cap      { 0xFF373837 };   // capuchons, boutons (D)
    juce::Colour rail     { 0xFF3E3F3D };   // rails, grilles, bords (D)
    juce::Colour railHi   { 0xFF4B4C4A };   // rail au survol (D)
    juce::Colour mark2    { 0xFF757675 };   // repères secondaires (D)
    juce::Colour text     { 0xFFF9F9FB };   // texte (C)
    juce::Colour text2    { 0xFFC9C9CA };   // texte secondaire (D)
    juce::Colour soft     { 0xFFA5A6A6 };   // unités, graduations, légendes (D)
    juce::Colour teal     { 0xFF0A9396 };   // niveau, seuil, courbe (C)
    juce::Colour teal2    { 0xFF79AFB0 };   // gain, niveaux mesurés (C)
    juce::Colour tealSel  { 0xFF005F73 };   // élément choisi (C)
    juce::Colour orange0  { 0xFFBB3E03 };   // départ des dégradés orange, sur le fond seulement (C)
    juce::Colour orange   { 0xFFEAA93D };   // temps, action ; ambre (C)
    juce::Colour redLight { 0xFF9B2226 };   // alerte sur cadran clair
    juce::Colour redDark  { 0xFFBC6D71 };   // alerte sur fond sombre (D)
    // cadran VU
    juce::Colour vuFace   { 0xFFF9F9FB };
    juce::Colour vuInk    { 0xFF1D1E1C };
    juce::Colour vuText   { 0xFF686868 };
    juce::Colour vuMinor  { 0xFF969697 };
};

// Thème partagé par tous les éléments d'une fenêtre de plugin (sombre uniquement).
class Theme
{
public:
    Theme()
    {
        tf[0] = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
        tf[1] = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
        tf[2] = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsSemiBold_ttf, BinaryData::PoppinsSemiBold_ttfSize);
        tf[3] = juce::Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    }

    const Pal& pal() const { return p; }

    static int index (int weight) { return weight >= 700 ? 3 : weight >= 600 ? 2 : weight >= 500 ? 1 : 0; }

    // Taille en px comme « font-size » en CSS ; weight : 400, 500, 600 ou 700 ; tracking en em
    juce::Font ui (float size, int weight = 400, float tracking = 0) const
    {
        return juce::Font (juce::FontOptions (tf[index (weight)]).withPointHeight (size)).withExtraKerningFactor (tracking);
    }

    // Largeur d'une cellule de chiffre = chiffre le plus large de la graisse (Poppins n'a pas de chiffres à chasse fixe)
    static float digitCell (int weight)
    {
        static constexpr float em[4] = { 0.635f, 0.647f, 0.661f, 0.677f };
        return em[index (weight)];
    }

private:
    Pal p;
    juce::Typeface::Ptr tf[4];
};

inline float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0, 0);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// Largeur d'un texte dont chaque chiffre occupe une cellule fixe (valeurs qui bougent : pas de tremblement)
inline float tabularWidth (const Theme& t, const juce::String& s, float size, int weight, float tracking = 0)
{
    const auto f = t.ui (size, weight, tracking);
    float w = 0;
    for (auto c : s)
        w += juce::CharacterFunctions::isDigit (c) ? size * Theme::digitCell (weight) : textWidth (f, juce::String::charToString (c));
    return w;
}

// Dessine un texte à chiffres en cellules fixes, centré verticalement dans « area »
inline void drawTabular (juce::Graphics& g, const Theme& t, const juce::String& s, float size, int weight,
                         juce::Rectangle<float> area, juce::Justification just, float tracking = 0)
{
    const auto f = t.ui (size, weight, tracking);
    g.setFont (f);
    const float w = tabularWidth (t, s, size, weight, tracking);
    float x = just.testFlags (juce::Justification::right) ? area.getRight() - w
            : just.testFlags (juce::Justification::left)  ? area.getX()
                                                          : area.getCentreX() - w / 2;
    const float base = area.getCentreY() + (f.getAscent() - f.getDescent()) / 2;
    for (auto c : s)
    {
        const auto ch = juce::String::charToString (c);
        const float adv = textWidth (f, ch);
        const float cell = juce::CharacterFunctions::isDigit (c) ? size * Theme::digitCell (weight) : adv;
        juce::GlyphArrangement ga; ga.addLineOfText (f, ch, x + (cell - adv) / 2, base); ga.draw (g);
        x += cell;
    }
}

inline juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
} // namespace audiolab
