// Test hors DAW : fait tourner le plugin sur un signal musical et enregistre des captures
// de l'interface (vues, thèmes, presets, bypass, tailles) en PNG.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

static void fillMusic (juce::AudioBuffer<float>& b, double fs, int64_t& pos, juce::Random& rnd)
{
    for (int i = 0; i < b.getNumSamples(); ++i, ++pos)
    {
        const double t = pos / fs, beat = 0.5, tb = std::fmod (t, beat);
        const int k = (int) (t / beat);
        double s = std::sin (2 * juce::MathConstants<double>::pi * (50 + 120 * std::exp (-tb * 30)) * tb) * std::exp (-tb * 9) * 0.9;
        if (k % 2) s += (rnd.nextFloat() * 2 - 1) * std::exp (-tb * 18) * 0.5;
        s += 0.25 * std::sin (2 * juce::MathConstants<double>::pi * 55 * t);
        s += 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 220 * t + 3 * std::sin (2 * juce::MathConstants<double>::pi * 5 * t));
        b.setSample (0, i, (float) s); b.setSample (1, i, (float) (s * 0.95));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? argv[1] : "/tmp");
    auto proc = std::make_unique<AudioLabCompProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000, 512);
    proc->prepareToPlay (48000, 512);

    std::atomic<bool> run { true };
    std::thread audio ([&] {
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; int64_t pos = 0; juce::Random rnd (7);
        while (run) { fillMusic (buf, 48000, pos, rnd); proc->processBlock (buf, midi); std::this_thread::sleep_for (std::chrono::microseconds (10600)); }
    });

    std::unique_ptr<juce::AudioProcessorEditor> ed;

    // Séquence pilotée par minuteries (pas de boucle modale dans un plugin)
    std::vector<std::pair<int, std::function<void()>>> steps;
    auto shot = [&] (const juce::String& name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto f = out.getChildFile (name + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("%s %dx%d\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    auto setView = [&] (int v) { proc->ui.setProperty ("view", v, nullptr); };
    auto rebuild = [&]
    {
        if (ed != nullptr) { proc->editorBeingDeleted (ed.get()); ed.reset(); }   // ce que fait l'hôte avant de fermer la fenêtre
        ed.reset (proc->createEditorAndMakeActive()); ed->setVisible (true);
    };
    auto bypass = [&] (float v) { proc->apvts.getParameter (audiolab::params::bypass)->setValueNotifyingHost (v); };

    steps.push_back ({ 0,    [&] { proc->ui.setProperty ("theme", "light", nullptr); proc->applyPreset (0); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("01_clair_transfert_voix"); setView (2); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("02_clair_metres_voix"); setView (1); rebuild(); } });
    steps.push_back ({ 2500, [&] { shot ("03_clair_activite_voix"); proc->ui.setProperty ("theme", "dark", nullptr); proc->applyPreset (2); setView (0); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("04_sombre_transfert_pompage"); setView (2); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("05_sombre_metres_pompage"); proc->applyPreset (4); setView (0); rebuild(); } });
    steps.push_back ({ 1200, [&] { shot ("06_sombre_sidechain"); bypass (1.0f); rebuild(); } });
    steps.push_back ({ 600,  [&] { shot ("07_sombre_bypass"); bypass (0.0f); proc->ui.setProperty ("theme", "light", nullptr); proc->applyPreset (-1); rebuild();
                                   ed->setSize (AudioLabCompEditor::baseW * 6 / 10, AudioLabCompEditor::baseH * 6 / 10); } });
    steps.push_back ({ 800,  [&] { shot ("08_clair_petite_taille_default"); juce::MessageManager::getInstance()->stopDispatchLoop(); } });

    std::function<void (size_t)> next = [&] (size_t i)
    {
        if (i >= steps.size()) return;
        juce::Timer::callAfterDelay (std::max (1, steps[i].first), [&, i] { steps[i].second(); next (i + 1); });
    };
    next (0);
    juce::MessageManager::getInstance()->runDispatchLoop();

    run = false; audio.join();
    proc->editorBeingDeleted (ed.get()); ed.reset();
    proc->releaseResources();
    return 0;
}
