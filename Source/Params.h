// Paramètres, plages et presets — repris d'Audio_Lab.html (outil « comp »).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace audiolab::params
{
inline constexpr const char* thr     = "thr";
inline constexpr const char* ratio   = "ratio";
inline constexpr const char* knee    = "knee";
inline constexpr const char* attack  = "attack";
inline constexpr const char* release = "release";
inline constexpr const char* makeup  = "makeup";
inline constexpr const char* look    = "lookahead";
inline constexpr const char* detect  = "detect";
inline constexpr const char* scType  = "scType";
inline constexpr const char* scFreq  = "scFreq";
inline constexpr const char* scQ     = "scQ";
inline constexpr const char* scListen = "scListen";
inline constexpr const char* mix     = "mix";
inline constexpr const char* bypass  = "bypass";

// Même « accrochage » que snap() dans Audio Lab : pas du curseur d'origine.
inline float snapTo (float v, float mn, float mx, float step)
{
    v = juce::jlimit (mn, mx, v);
    return (float) (std::round ((v - mn) / step) * step + mn);
}

// Courbe linéaire ou logarithmique des potentiomètres (CKN.def dans Audio Lab).
inline juce::NormalisableRange<float> linRange (float a, float b, float step)
{
    return { a, b,
             [=] (float, float, float n) { return snapTo (a + (b - a) * n, a, b, step); },
             [=] (float, float, float v) { return juce::jlimit (0.0f, 1.0f, (v - a) / (b - a)); },
             [=] (float, float, float v) { return snapTo (v, a, b, step); } };
}
inline juce::NormalisableRange<float> logRange (float a, float b, float step)
{
    return { a, b,
             [=] (float, float, float n) { return snapTo (a * std::pow (b / a, n), a, b, step); },
             [=] (float, float, float v) { return juce::jlimit (0.0f, 1.0f, std::log (std::max (a, v) / a) / std::log (b / a)); },
             [=] (float, float, float v) { return snapTo (v, a, b, step); } };
}
// Fréquence du side-chain : l2f(v) = round(20 · 1000^v) Hz
inline juce::NormalisableRange<float> freqRange()
{
    return { 20.0f, 20000.0f,
             [] (float, float, float n) { return (float) std::round (20.0 * std::pow (1000.0, (double) n)); },
             [] (float, float, float v) { return juce::jlimit (0.0f, 1.0f, (float) (std::log (std::max (20.0f, v) / 20.0) / std::log (1000.0))); },
             [] (float, float, float v) { return juce::jlimit (20.0f, 20000.0f, std::round (v)); } };
}

inline juce::String fmtHz (float f) { return f >= 1000 ? juce::String (f / 1000.0f, 1) + " k" : juce::String (juce::roundToInt (f)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto fl = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit,
                   std::function<String (float, int)> toText)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                   AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (toText))));
    };
    fl (thr, "Threshold", linRange (-60, 0, 1), 0, "dB", [] (float v, int) { return String (roundToInt (v)) + " dB"; });
    fl (ratio, "Ratio", logRange (1, 20, 0.5f), 1, ":1", [] (float v, int) { return v >= 20 ? String (CharPointer_UTF8 ("\xe2\x88\x9e:1")) : String (v, std::fmod (v, 1.0f) > 0 ? 1 : 0) + ":1"; });
    fl (knee, "Knee", linRange (0, 20, 1), 0, "dB", [] (float v, int) { return String (roundToInt (v)) + " dB"; });
    fl (attack, "Attack", logRange (0.1f, 200, 0.1f), 0.1f, "ms", [] (float v, int) { return (v < 10 ? String (v, 1) : String (roundToInt (v))) + " ms"; });
    fl (release, "Release", logRange (10, 1000, 1), 10, "ms", [] (float v, int) { return String (roundToInt (v)) + " ms"; });
    fl (makeup, "Makeup", linRange (0, 24, 1), 0, "dB", [] (float v, int) { return "+" + String (roundToInt (v)) + " dB"; });
    fl (look, "Lookahead", linRange (0, 10, 0.5f), 0, "ms", [] (float v, int) { return String (v, 1) + " ms"; });
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { detect, 1 }, "Detection", StringArray { "Peak", "RMS" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { scType, 1 }, "Side-chain", StringArray { "Off", "HPF", "LPF", "Bell +6" }, 0));
    fl (scFreq, "SC Freq", freqRange(), 1000, "Hz", [] (float v, int) { return fmtHz (v); });
    fl (scQ, "SC Q", linRange (0.3f, 8, 0.1f), 0.7f, "", [] (float v, int) { return String (v, 1); });
    l.add (std::make_unique<AudioParameterBool> (ParameterID { scListen, 1 }, "Ecoute SC", false));
    fl (mix, "Dry/Wet", linRange (0, 100, 0.1f), 100, "%", [] (float v, int) { return String (roundToInt (v)) + "%"; });
    l.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));
    return l;
}

struct Preset
{
    const char* name;
    const char* desc;
    float thr, ratio, knee, atk, rel, mk, look;
    int rms, sc;
    float scF;
};

// Presets pédagogiques d'Audio Lab : valeurs + ce qu'on doit entendre.
// (« Side-chain anti-kick » : fréquence réglée à 150 Hz comme l'annonce sa description.)
inline const Preset presets[] = {
    { "Voix transparente", "Ratio 3:1, seuil léger, attaque 10 ms, release 80 ms, knee doux. La GR doit bouger de 2 à 5 dB sans s'entendre.", -22, 3, 8, 10, 80, 4, 0, 0, 0, 1000 },
    { "Punch batterie", "Attaque lente (40 ms) laisse passer le transitoire, release rapide. La caisse claire « ressort ».", -18, 4, 2, 40, 50, 3, 0, 0, 0, 1000 },
    { "Pompage volontaire", "Seuil très bas, ratio 8:1, attaque 1 ms, release 400 ms : le niveau respire au rythme. À reconnaître à l'oreille.", -40, 8, 0, 1, 400, 10, 0, 1, 0, 1000 },
    { "Limiteur brickwall", "\xe2\x88\x9e:1, attaque minimale, lookahead 5 ms : plus rien ne dépasse le seuil.", -10, 20, 0, 0.1f, 60, 6, 5, 0, 0, 1000 },
    { "Side-chain anti-kick", "Détecteur filtré passe-haut 150 Hz : le kick ne déclenche plus la compression.", -24, 4, 6, 5, 120, 3, 0, 0, 1, 150 },
};
inline constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
} // namespace audiolab::params
