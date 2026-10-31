// Vérifie le plugin complet (pas seulement le moteur) : latence annoncée, Dry/Wet aligné
// avec le lookahead, Bypass, mono, sauvegarde / rappel de l'état et des snapshots.
#include "PluginProcessor.h"
#include <cstdio>

static int fails = 0;
static void check (bool c, const char* msg) { std::printf ("  %s %s\n", c ? "OK   " : "ÉCHEC", msg); if (! c) ++fails; }

static void setP (AudioLabCompProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static std::vector<float> impulse (AudioLabCompProcessor& p, int channels, int n = 2048, int at = 100)
{
    juce::AudioBuffer<float> b (channels, n); b.clear();
    for (int c = 0; c < channels; ++c) b.setSample (c, at, 0.5f);
    juce::MidiBuffer m;
    for (int s = 0; s < n; s += 256) { juce::AudioBuffer<float> sub (b.getArrayOfWritePointers(), channels, s, std::min (256, n - s)); p.processBlock (sub, m); }
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + n);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000;

    std::printf ("\nPlugin complet\n");
    {
        AudioLabCompProcessor p;
        setP (p, audiolab::params::look, 5);
        p.setPlayConfigDetails (2, 2, fs, 256); p.prepareToPlay (fs, 256);
        check (p.getLatencySamples() == 240, "latence annoncée à l'hôte = lookahead 5 ms (240 éch.)");

        setP (p, audiolab::params::mix, 50);
        p.prepareToPlay (fs, 256);
        auto y = impulse (p, 2);
        int peaks = 0; size_t where = 0;
        for (size_t i = 0; i < y.size(); ++i) if (std::abs (y[i]) > 1e-4f) { ++peaks; where = i; }
        check (peaks == 1 && where == 340, "Dry/Wet 50 % + lookahead : sec et traité alignés (une seule impulsion, pas d'effet de peigne)");
        check (std::abs (y[340] - 0.5f * 2 * std::cos (juce::MathConstants<float>::pi / 4)) < 1e-4f, "Dry/Wet à puissance constante (cos/sin), comme Audio Lab");

        setP (p, audiolab::params::mix, 100);
        setP (p, audiolab::params::bypass, 1);
        p.prepareToPlay (fs, 256);
        y = impulse (p, 2);
        check (std::abs (y[340] - 0.5f) < 1e-5f, "Bypass : signal intact, avec la même latence");
    }
    {
        AudioLabCompProcessor p;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.setBusesLayout (mono), "piste mono acceptée");
        p.setPlayConfigDetails (1, 1, fs, 256); p.prepareToPlay (fs, 256);
        auto y = impulse (p, 1);
        check (std::abs (y[100] - 0.5f) < 1e-5f, "mono : traitement correct");
    }
    {
        AudioLabCompProcessor a;
        a.applyPreset (3);                       // Limiteur brickwall
        a.storeSnapshot ("B");
        setP (a, audiolab::params::thr, -33);
        a.ui.setProperty ("theme", "dark", nullptr);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        AudioLabCompProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (juce::approximatelyEqual (b.getParam (audiolab::params::thr), -33.0f) && juce::approximatelyEqual (b.getParam (audiolab::params::look), 5.0f),
               "réglages rappelés à la réouverture du projet");
        check (b.ui.getProperty ("theme").toString() == "dark" && (int) b.ui.getProperty ("preset") == 3, "thème et preset rappelés");
        check (b.recallSnapshot ("B") && juce::approximatelyEqual (b.getParam (audiolab::params::thr), -10.0f), "snapshot B rappelé après réouverture");
        b.applyPreset (-1);
        check (juce::approximatelyEqual (b.getParam (audiolab::params::ratio), 1.0f) && juce::approximatelyEqual (b.getParam (audiolab::params::release), 10.0f), "Reset → réglages par défaut d'Audio Lab");
        check (juce::approximatelyEqual (b.getParam (audiolab::params::mix), 100.0f), "Reset laisse le Dry/Wet inchangé (comme Audio Lab)");
    }
    {
        AudioLabCompProcessor p;
        auto* r = p.apvts.getParameter (audiolab::params::ratio);
        check (r->getText (r->convertTo0to1 (20), 16).contains (juce::String::fromUTF8 ("∞")), "ratio maximal affiché ∞:1 dans l'automation");
        auto* f = p.apvts.getParameter (audiolab::params::scFreq);
        check (juce::approximatelyEqual (f->convertFrom0to1 (f->convertTo0to1 (150)), 150.0f), "fréquence SC 150 Hz exacte");
        auto* at = p.apvts.getParameter (audiolab::params::attack);
        check (juce::approximatelyEqual (at->convertFrom0to1 (0.5f), 4.5f), "potentiomètre Attack logarithmique (milieu = 4,5 ms, comme Audio Lab)");
    }
    std::printf ("\n%s\n", fails == 0 ? "RÉSULTAT : tout est conforme" : "RÉSULTAT : des écarts à corriger");
    return fails == 0 ? 0 : 1;
}
