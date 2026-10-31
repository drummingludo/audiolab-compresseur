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
    std::printf ("\nMode AAX : latence constante (exigence Pro Tools)\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        AudioLabCompProcessor p;
        p.constantLatency = true;                // ce que fait le wrapper AAX
        const int fixed = (int) std::floor (0.010 * sr + 0.5);
        setP (p, audiolab::params::look, 5);
        p.setPlayConfigDetails (2, 2, sr, 256); p.prepareToPlay (sr, 256);
        char msg[128];
        std::snprintf (msg, sizeof msg, "latence annoncée fixe = 10 ms (%d éch. à %.1f kHz)", fixed, sr / 1000);
        check (p.getLatencySamples() == fixed, msg);

        setP (p, audiolab::params::mix, 50);
        p.prepareToPlay (sr, 256);
        const int n = 4096;
        auto onePeakAt = [] (const std::vector<float>& y, size_t at)
        {
            int peaks = 0; size_t where = 0;
            for (size_t i = 0; i < y.size(); ++i) if (std::abs (y[i]) > 1e-4f) { ++peaks; where = i; }
            return peaks == 1 && where == at;
        };
        auto y = impulse (p, 2, n);
        check (onePeakAt (y, (size_t) (100 + fixed)), "lookahead 5 ms + Dry/Wet 50 % : une seule impulsion, retard total = latence annoncée");

        // Pendant la lecture (sans prepareToPlay) : changer le lookahead ne change ni la latence ni l'alignement
        bool ok = true;
        for (float l : { 0.0f, 2.5f, 10.0f, 7.0f })
        {
            setP (p, audiolab::params::look, l);
            y = impulse (p, 2, n);
            ok = ok && p.getLatencySamples() == fixed && onePeakAt (y, (size_t) (100 + fixed));
        }
        check (ok, "lookahead 0 / 2,5 / 10 / 7 ms en cours de lecture : latence et alignement inchangés");

        setP (p, audiolab::params::mix, 100);
        setP (p, audiolab::params::bypass, 1);
        p.prepareToPlay (sr, 256);
        y = impulse (p, 2, n);
        check (onePeakAt (y, (size_t) (100 + fixed)) && std::abs (y[(size_t) (100 + fixed)] - 0.5f) < 1e-5f,
               "Bypass : signal intact et même latence (test de nul Avid)");
    }
    {
        // Même son qu'en VST3 / AU, simplement décalé : compression active (limiteur, lookahead 5 ms,
        // side-chain HPF, Dry/Wet 70 %), signal de test = rafales de sinus 220 Hz à niveaux variables.
        auto run = [&] (bool aax)
        {
            AudioLabCompProcessor p;
            p.constantLatency = aax;
            p.applyPreset (3);
            setP (p, audiolab::params::scType, 1);
            setP (p, audiolab::params::scFreq, 150);
            setP (p, audiolab::params::mix, 70);
            p.setPlayConfigDetails (2, 2, fs, 256); p.prepareToPlay (fs, 256);
            const int n = 48000;
            juce::AudioBuffer<float> b (2, n);
            juce::Random rnd (42);
            for (int i = 0; i < n; ++i)
            {
                const float env = ((i / 4800) % 2 == 0) ? 0.9f : 0.15f;
                const float s = env * std::sin (2 * juce::MathConstants<float>::pi * 220 * i / (float) fs);
                b.setSample (0, i, s);
                b.setSample (1, i, s * 0.8f + 0.01f * (rnd.nextFloat() - 0.5f));
            }
            juce::MidiBuffer m;
            for (int s = 0; s < n; s += 256) { juce::AudioBuffer<float> sub (b.getArrayOfWritePointers(), 2, s, std::min (256, n - s)); p.processBlock (sub, m); }
            return std::make_pair (b, p.getLatencySamples());
        };
        auto [vst, latV] = run (false);
        auto [aax, latA] = run (true);
        const int shift = latA - latV;
        float maxDiff = 0, peak = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i + shift < vst.getNumSamples(); ++i)
            {
                maxDiff = std::max (maxDiff, std::abs (aax.getSample (c, i + shift) - vst.getSample (c, i)));
                peak = std::max (peak, std::abs (vst.getSample (c, i)));
            }
        check (shift == 240 && maxDiff == 0.0f && peak > 0.1f,
               "compression active : sortie AAX identique au bit près à la sortie VST3, décalée de 240 éch. (10 − 5 ms)");
    }
    {
        AudioLabCompProcessor p;                 // VST3 / AU : comportement d'origine
        setP (p, audiolab::params::look, 0);
        p.setPlayConfigDetails (2, 2, fs, 256); p.prepareToPlay (fs, 256);
        check (p.getLatencySamples() == 0, "VST3 / AU inchangés : lookahead 0 → latence 0");
    }

    std::printf ("\n%s\n", fails == 0 ? "RÉSULTAT : tout est conforme" : "RÉSULTAT : des écarts à corriger");
    return fails == 0 ? 0 : 1;
}
