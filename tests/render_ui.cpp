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

    auto adv = [&] (bool o) { proc->ui.setProperty ("adv", o, nullptr); };

    steps.push_back ({ 0,    [&] { adv (false); proc->applyPreset (0); setView (0); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("01_replie_transfert"); setView (1); rebuild(); } });
    steps.push_back ({ 2500, [&] { shot ("02_replie_activite"); setView (2); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("03_replie_vumetres"); adv (true); setView (0); rebuild(); } });
    steps.push_back ({ 1200, [&] { shot ("04_deplie_transfert"); proc->applyPreset (2); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("05_deplie_pompage"); adv (false); setView (2); rebuild(); } });
    steps.push_back ({ 1500, [&] { shot ("06_replie_vumetres"); setView (1); rebuild(); } });
    steps.push_back ({ 2500, [&] { shot ("07_replie_activite"); proc->applyPreset (4); adv (true); setView (0); rebuild(); } });
    steps.push_back ({ 1200, [&] { shot ("08_deplie_sidechain"); bypass (1.0f); rebuild(); } });
    steps.push_back ({ 600,  [&] { shot ("09_bypass"); bypass (0.0f); proc->applyPreset (-1); adv (false); rebuild();
                                   ed->setSize (AudioLabCompEditor::baseW * 6 / 10, AudioLabCompEditor::baseH * 6 / 10); } });
    steps.push_back ({ 800,  [&] { shot ("10_petite_taille_default");
                                   // ouverture du panneau par la flèche, à 60 % : la fenêtre doit grandir dans les mêmes proportions
                                   for (auto* c : ed->getChildren().getFirst()->getChildren())
                                       if (auto* d = dynamic_cast<audiolab::Disclosure*> (c)) d->triggerClick(); } });
    steps.push_back ({ 800,  [&] { std::printf ("taille apres ouverture : %dx%d (attendu %dx%d)\n", ed->getWidth(), ed->getHeight(),
                                                juce::roundToInt (AudioLabCompEditor::baseW * 0.6), juce::roundToInt (AudioLabCompEditor::baseHOpen * 0.6));
                                   shot ("11_petite_taille_deplie");
                                   for (auto* c : ed->getChildren().getFirst()->getChildren())
                                       if (auto* d = dynamic_cast<audiolab::Disclosure*> (c)) d->triggerClick(); } });
    steps.push_back ({ 800,  [&] { std::printf ("taille apres fermeture : %dx%d (attendu %dx%d)\n", ed->getWidth(), ed->getHeight(),
                                                juce::roundToInt (AudioLabCompEditor::baseW * 0.6), juce::roundToInt (AudioLabCompEditor::baseH * 0.6));
                                   ed->setSize (AudioLabCompEditor::baseW * 2, AudioLabCompEditor::baseH * 2); } });
    steps.push_back ({ 800,  [&] { shot ("12_200pc"); juce::MessageManager::getInstance()->stopDispatchLoop(); } });

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
