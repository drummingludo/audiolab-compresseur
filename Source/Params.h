// Paramètres, plages et presets — repris d'Audio_Lab.html (outil « comp »), plus les réglages ajoutés en 2.0.0.
// Identifiants et sens des paramètres 1.x inchangés (sessions compatibles).
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
// 2.0.0 (panneau Advanced settings)
inline constexpr const char* truePeak  = "truePeak";     // limiteur ∞ : plafond en crête vraie (Off / On)
inline constexpr const char* scSource  = "scSource";     // Internal / External
inline constexpr const char* link      = "stereoLink";   // 0 … 100 %
inline constexpr const char* stMode    = "stereoMode";   // Stereo / Mid-Side
inline constexpr const char* hold      = "hold";         // 0 … 500 ms
inline constexpr const char* autoRel   = "autoRelease";  // Off / On
inline constexpr const char* rmsWin    = "rmsWindow";    // 1 … 300 ms
inline constexpr const char* oversamp  = "oversampling"; // Off / 2× / 4×

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
// Plage « en puissance » (0 inclus, plus de finesse près de 0) : v = a + (b − a) · n^k
inline juce::NormalisableRange<float> powRange (float a, float b, float step, float k)
{
    return { a, b,
             [=] (float, float, float n) { return snapTo (a + (b - a) * std::pow (juce::jlimit (0.0f, 1.0f, n), k), a, b, step); },
             [=] (float, float, float v) { return std::pow (juce::jlimit (0.0f, 1.0f, (v - a) / (b - a)), 1.0f / k); },
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

inline juce::String fmtRatio (float v) { return std::abs (v - std::round (v)) < 0.05f ? juce::String (juce::roundToInt (v)) : juce::String (v, 1); }
inline juce::String fmtHz (float f) { return f >= 1000 ? juce::String (f / 1000.0f, 1) + " k" : juce::String (juce::roundToInt (f)) + " Hz"; }
inline juce::String fmtMs (float v) { return (v < 10 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto fl = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit,
                   std::function<String (float, int)> toText, int version = 1)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, r, def,
                   AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (std::move (toText))));
    };
    // Threshold au 1/10 de dB et Ratio au 1/10 (plus fins que dans Audio Lab web : 1 dB et 0,5)
    fl (thr, "Threshold", linRange (-60, 0, 0.1f), 0, "dB", [] (float v, int) { return String (v, 1) + " dB"; });
    fl (ratio, "Ratio", logRange (1, 20, 0.1f), 1, ":1", [] (float v, int) { return v >= 20 ? String (CharPointer_UTF8 ("\xe2\x88\x9e:1")) : fmtRatio (v) + ":1"; });
    fl (knee, "Knee", linRange (0, 20, 1), 0, "dB", [] (float v, int) { return String (roundToInt (v)) + " dB"; });
    fl (attack, "Attack", logRange (0.1f, 200, 0.1f), 0.1f, "ms", [] (float v, int) { return (v < 10 ? String (v, 1) : String (roundToInt (v))) + " ms"; });
    fl (release, "Release", logRange (10, 1000, 1), 10, "ms", [] (float v, int) { return String (roundToInt (v)) + " ms"; });
    fl (makeup, "Makeup", linRange (0, 24, 1), 0, "dB", [] (float v, int) { return "+" + String (roundToInt (v)) + " dB"; });
    fl (look, "Lookahead", linRange (0, 10, 0.5f), 0, "ms", [] (float v, int) { return String (v, 1) + " ms"; });
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { detect, 1 }, "Detection", StringArray { "Peak", "RMS" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { scType, 1 }, "Side-chain", StringArray { "Off", "HPF", "LPF", "Bell +6" }, 0));
    fl (scFreq, "SC Freq", freqRange(), 1000, "Hz", [] (float v, int) { return fmtHz (v); });
    fl (scQ, "SC Q", linRange (0.3f, 8, 0.1f), 0.7f, "", [] (float v, int) { return String (v, 1); });
    l.add (std::make_unique<AudioParameterBool> (ParameterID { scListen, 1 }, "SC Listen", false));
    fl (mix, "Dry/Wet", linRange (0, 100, 0.1f), 100, "%", [] (float v, int) { return String (roundToInt (v)) + "%"; });
    l.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));

    // 2.0.0 : nouveaux réglages, valeurs par défaut = comportement de la 1.x (sauf True peak, utile seulement en ∞:1)
    const auto times = String (CharPointer_UTF8 ("\xc3\x97"));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { truePeak, 2 }, "True Peak", StringArray { "Off", "On" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { scSource, 2 }, "SC Source", StringArray { "Internal", "External" }, 0));
    fl (link, "Stereo Link", linRange (0, 100, 1), 100, "%", [] (float v, int) { return String (roundToInt (v)) + " %"; }, 2);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { stMode, 2 }, "Stereo Mode", StringArray { "Stereo", "Mid-Side" }, 0));
    fl (hold, "Hold", powRange (0, 500, 1, 2), 0, "ms", [] (float v, int) { return String (roundToInt (v)) + " ms"; }, 2);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { autoRel, 2 }, "Auto Release", StringArray { "Off", "On" }, 0));
    fl (rmsWin, "RMS Window", logRange (1, 300, 0.1f), 3, "ms", [] (float v, int) { return fmtMs (v); }, 2);
    // suréchantillonnage : change la latence, donc non automatisable (réglé une fois, hors lecture de préférence)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { oversamp, 2 }, "Oversampling", StringArray { "Off", "2" + times, "4" + times }, 0,
                                                   AudioParameterChoiceAttributes().withAutomatable (false)));
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
    { "Transparent vocal", "Ratio 3:1, light threshold, 10 ms attack, 80 ms release, soft knee. GR should move 2 to 5 dB without being heard.", -22, 3, 8, 10, 80, 4, 0, 0, 0, 1000 },
    { "Drum punch", "Slow attack (40 ms) lets the transient through, fast release. The snare \"pops out\".", -18, 4, 2, 40, 50, 3, 0, 0, 0, 1000 },
    { "Deliberate pumping", "Very low threshold, 8:1 ratio, 1 ms attack, 400 ms release: the level breathes with the beat. Learn to hear it.", -40, 8, 0, 1, 400, 10, 0, 1, 0, 1000 },
    { "Brickwall limiter", "\xe2\x88\x9e:1 true limiter, 5 ms lookahead, True peak on: the output never goes above threshold + makeup (\xe2\x88\x92" "4 dBFS here).", -10, 20, 0, 0.1f, 60, 6, 5, 0, 0, 1000 },
    { "Side-chain anti-kick", "Detector high-passed at 150 Hz: the kick no longer triggers the compression.", -24, 4, 6, 5, 120, 3, 0, 0, 1, 150 },
};
inline constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
} // namespace audiolab::params
