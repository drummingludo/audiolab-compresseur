#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

// Textes d'aide « Pourquoi ? » d'Audio Lab
static const char* helpThr  = "Niveau à partir duquel le traitement agit. Plus bas = plus de signal concerné.";
static const char* helpRat  = "Pente de réduction au-dessus du seuil : 4:1 → 4 dB en entrée ne donnent que 1 dB en sortie. ∞:1 = limiteur.";
static const char* helpKn   = "Transition douce autour du seuil. Knee large = compression progressive, plus discrète.";
static const char* helpAtk  = "Temps de réaction. Longue : le transitoire passe (punch). Courte : tout est tenu, le son s'aplatit.";
static const char* helpRel  = "Temps de retour au gain unitaire. Trop court : pompage/distorsion ; trop long : le niveau reste écrasé.";
static const char* helpMk   = "Gain de compensation après compression. Attention au biais « plus fort = mieux » : comparez à niveau égal.";
static const char* helpLook = "Le détecteur voit le signal en avance : l'attaque peut rattraper les transitoires sans les laisser passer.";

static void headphonesIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    r = r.withSizeKeepingCentre (13, 12);
    juce::Path p;
    p.addCentredArc (r.getCentreX(), r.getY() + 6.5f, 5.5f, 5.5f, 0, -juce::MathConstants<float>::halfPi * 1.05f, juce::MathConstants<float>::halfPi * 1.05f, true);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillRoundedRectangle (r.getX(), r.getY() + 6.5f, 3.2f, 5.5f, 1.2f);
    g.fillRoundedRectangle (r.getRight() - 3.2f, r.getY() + 6.5f, 3.2f, 5.5f, 1.2f);
}

AudioLabCompEditor::AudioLabCompEditor (AudioLabCompProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      listenBtn (theme, PillButton::Style::ibtn, U ("Écouter le SC")),
      display (theme, p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& ap = proc.apvts;
    auto prm = [&] (const char* id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };
    auto acc = [] (const Pal& q) { return q.accent; };
    auto ora = [] (const Pal& q) { return q.orange; };
    auto grn = [] (const Pal& q) { return q.green; };
    auto i0 = [] (float v) { return String (juce::roundToInt (v)); };

    kThr = std::make_unique<ArcKnob> (theme, prm (params::thr), "Threshold", "dB", acc, ArcKnob::Size::md, i0, 0.0f, U (helpThr));
    kRatio = std::make_unique<ArcKnob> (theme, prm (params::ratio), "Ratio", ":1", acc, ArcKnob::Size::md,
        [] (float v) { return v >= 20 ? U ("∞") : (std::fmod (v, 1.0f) > 0 ? String (v, 1) : String ((int) v)); }, 1.0f, U (helpRat));
    kKnee = std::make_unique<ArcKnob> (theme, prm (params::knee), "Knee", "dB", acc, ArcKnob::Size::sm, i0, 0.0f, U (helpKn));
    kAtk = std::make_unique<ArcKnob> (theme, prm (params::attack), "Attack", "ms", ora, ArcKnob::Size::md,
        [] (float v) { return v < 10 ? String (v, 1) : String (juce::roundToInt (v)); }, 0.1f, U (helpAtk));
    kRel = std::make_unique<ArcKnob> (theme, prm (params::release), "Release", "ms", ora, ArcKnob::Size::md, i0, 10.0f, U (helpRel));
    kMk = std::make_unique<ArcKnob> (theme, prm (params::makeup), "Makeup", "dB", grn, ArcKnob::Size::sm,
        [] (float v) { return "+" + String (juce::roundToInt (v)); }, 0.0f, U (helpMk));

    rowLook = std::make_unique<SliderRow> (theme, prm (params::look), "Lookahead", [] (float v) { return String (v, 1) + " ms"; });
    rowLook->setTooltip (U (helpLook));
    rowScF = std::make_unique<SliderRow> (theme, prm (params::scFreq), "SC Freq", [] (float v) { return params::fmtHz (v); });
    rowScQ = std::make_unique<SliderRow> (theme, prm (params::scQ), "SC Q", [] (float v) { return String (v, 1); });

    detectSeg.attach (prm (params::detect));
    scBox.addItemList ({ "Off", "HPF", "LPF", "Bell +6" }, 1);
    scBox.setTitle ("Side-chain");
    scBox.setTooltip (U ("Filtre du détecteur : le compresseur réagit seulement à la partie du spectre choisie."));
    scAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, params::scType, scBox);

    listenBtn.icon = headphonesIcon;
    listenBtn.setTooltip (U ("Écouter le signal qui pilote la compression (side-chain filtré)"));
    listenAtt = std::make_unique<juce::ParameterAttachment> (prm (params::scListen), [this] (float v) {
        listenBtn.on = v > 0.5f; listenBtn.repaint(); }, nullptr);
    listenBtn.onClick = [this]
    {
        const bool now = ! listenBtn.on;
        listenAtt->setValueAsCompleteGesture (now ? 1.0f : 0.0f);
        if (now) toast.show (proc.getParam (params::scType) < 0.5f ? U ("Écoute du side-chain : signal du détecteur (aucun filtre actif)")
                                                                   : U ("Écoute du side-chain filtré"), true);
    };
    listenAtt->sendInitialUpdate();

    bypassAtt = std::make_unique<juce::ParameterAttachment> (prm (params::bypass), [this] (float v) {
        bypassed = v > 0.5f; refreshState(); }, nullptr);
    bypassAtt->sendInitialUpdate();   // Bypass du logiciel : la fenêtre s'estompe
    viewSeg.onSelect = [this] (int v) { display.setView (v); proc.ui.setProperty ("view", v, nullptr); };

    body = { kThr.get(), kRatio.get(), kKnee.get(), kAtk.get(), kRel.get(), kMk.get(), rowLook.get(), &detectSeg,
             &scBox, &listenBtn, rowScF.get(), rowScQ.get(), &display, &viewSeg };
    for (auto* c : body) content.addAndMakeVisible (c);
    content.addAndMakeVisible (toast);

    for (auto* id : { params::scType }) ap.addParameterListener (id, this);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    const int v = (int) proc.ui.getProperty ("view", 0);
    viewSeg.setSelected (v); display.setView (v);

    applyTheme();
    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant que les limites ne redimensionnent
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (baseH * sc));
    refreshState();
}

AudioLabCompEditor::~AudioLabCompEditor()
{
    proc.apvts.removeParameterListener (params::scType, this);
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    cancelPendingUpdate();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabCompEditor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
void AudioLabCompEditor::handleAsyncUpdate() { refreshState(); }

void AudioLabCompEditor::applyTheme()
{
    // Clair / sombre selon le réglage de l'ordinateur (« themeOverride » sert seulement aux tests)
    const String t = proc.ui.getProperty ("themeOverride", "auto").toString();
    const bool dark = t == "dark" || (t != "light" && juce::Desktop::getInstance().isDarkModeActive());
    theme.setDark (dark);
    lnf.refresh();
    sendLookAndFeelChange();   // les menus déroulants reprennent les couleurs du thème
    content.repaint();
    for (auto* c : content.getChildren()) c->repaint();
}

void AudioLabCompEditor::refreshState()
{
    const bool scOff = proc.getParam (params::scType) < 0.5f;
    rowScF->setDimmed (scOff); rowScQ->setDimmed (scOff);
    for (auto* c : body) { c->setAlpha (bypassed ? 0.35f : 1.0f); c->setEnabled (! bypassed); }
    content.repaint();
}

void AudioLabCompEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, baseH);
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

void AudioLabCompEditor::Content::paint (juce::Graphics& g)
{
    const auto& p = ed.theme.pal();
    g.fillAll (p.bg2);
    // libellés de la ligne Détection / Side-chain
    const float a = ed.bypassed ? 0.35f : 1.0f;
    g.setColour (p.dim.withMultipliedAlpha (a)); g.setFont (ed.theme.ui (12));
    g.drawText (U ("Détection"), juce::Rectangle<float> (20, 334, 80, 28), juce::Justification::centredLeft, false);
    g.drawText (U ("Side‑chain"), juce::Rectangle<float> (20, 370, 80, 28), juce::Justification::centredLeft, false);
}

void AudioLabCompEditor::Content::resized()
{
    auto& e = ed;
    // Corps : deux colonnes
    const int colW = (baseW - 40 - 18) / 2, lx = 20, rx = 20 + colW + 18, top = 18;
    const int cw = colW / 3;
    ArcKnob* row1[] = { e.kThr.get(), e.kRatio.get(), e.kKnee.get() };
    ArcKnob* row2[] = { e.kAtk.get(), e.kRel.get(), e.kMk.get() };
    for (int i = 0; i < 3; ++i)
    {
        row1[i]->setBounds (lx + i * cw, top, cw, 130);
        row2[i]->setBounds (lx + i * cw, top + 140, cw, 130);
    }
    e.rowLook->setBounds (lx, 296, colW, 32);
    e.detectSeg.setBounds (lx + 86, 334, juce::roundToInt (e.detectSeg.idealWidth()), 28);
    e.scBox.setBounds (lx + 86, 370, 104, 28);
    e.listenBtn.setBounds (lx + 86 + 104 + 10, 371, juce::roundToInt (e.listenBtn.idealWidth()), 26);
    e.rowScF->setBounds (lx, 404, colW, 32);
    e.rowScQ->setBounds (lx, 438, colW, 32);
    e.display.setBounds (rx, top, colW, baseH - 18 - 36 - top);
    e.viewSeg.setBounds (rx, baseH - 18 - 28, juce::roundToInt (e.viewSeg.idealWidth()), 28);
    e.toast.setBounds (getLocalBounds());
}
