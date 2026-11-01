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
      listenBtn (theme, U ("Écouter")),
      display (theme, p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    auto& ap = proc.apvts;
    auto prm = [&] (const char* id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };
    auto i0 = [] (float v) { return String (juce::roundToInt (v)); };

    // Charte V2 : seuil, ratio, knee = teal ; temps = orange ; gain (Make-up) et filtre side-chain = teal secondaire
    kThr = std::make_unique<ArcKnob> (theme, prm (params::thr), "Threshold", "dB", Family::teal, ArcKnob::Size::md,
        [] (float v) { return String (v, 1); }, 0.0f, U (helpThr));
    kRatio = std::make_unique<ArcKnob> (theme, prm (params::ratio), "Ratio", ":1", Family::teal, ArcKnob::Size::md,
        [] (float v) { return v >= 20 ? U ("∞") : params::fmtRatio (v); }, 1.0f, U (helpRat));
    // réglage plus précis du seuil et du ratio : course de souris plus longue, molette plus fine
    for (auto* k : { kThr.get(), kRatio.get() }) { k->dragPixels = 500; k->wheelStep = 0.004f; }
    kAtk = std::make_unique<ArcKnob> (theme, prm (params::attack), "Attack", "ms", Family::orange, ArcKnob::Size::md,
        [] (float v) { return v < 10 ? String (v, 1) : String (juce::roundToInt (v)); }, 0.1f, U (helpAtk));
    kRel = std::make_unique<ArcKnob> (theme, prm (params::release), "Release", "ms", Family::orange, ArcKnob::Size::md, i0, 10.0f, U (helpRel));
    kMk = std::make_unique<ArcKnob> (theme, prm (params::makeup), "Makeup", "dB", Family::teal2, ArcKnob::Size::xs,
        [] (float v) { return "+" + String (juce::roundToInt (v)); }, 0.0f, U (helpMk));

    rowKnee = std::make_unique<SliderRow> (theme, prm (params::knee), "Knee", Family::teal, [] (float v) { return String (juce::roundToInt (v)) + " dB"; });
    rowKnee->setTooltip (U (helpKn));
    rowLook = std::make_unique<SliderRow> (theme, prm (params::look), "Lookahead", Family::orange, [] (float v) { return String (v, 1) + " ms"; });
    rowLook->setTooltip (U (helpLook));
    rowScF = std::make_unique<SliderRow> (theme, prm (params::scFreq), "SC Freq", Family::teal2, [] (float v) { return params::fmtHz (v); });
    rowScQ = std::make_unique<SliderRow> (theme, prm (params::scQ), "SC Q", Family::teal2, [] (float v) { return String (v, 1); });

    detectSeg.onSurface = true;
    detectSeg.attach (prm (params::detect));
    viewSeg.fontSize = 10;   // sélecteur de l'écran : 10 px (charte § 5)
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
    bypassAtt->sendInitialUpdate();   // Bypass du logiciel : la lampe du VU s'éteint
    viewSeg.onSelect = [this] (int v) { display.setView (v); proc.ui.setProperty ("view", v, nullptr); };
    advBtn.setTooltip (U ("Afficher / masquer Knee, Lookahead, Détection et Side-chain"));
    advBtn.onClick = [this] { setAdvanced (! advOpen, true); };

    advItems = { rowKnee.get(), rowLook.get(), &detectSeg, &scBox, &listenBtn, rowScF.get(), rowScQ.get() };
    for (auto* c : std::initializer_list<juce::Component*> { kThr.get(), kRatio.get(), kAtk.get(), kRel.get(), kMk.get(), &display, &viewSeg, &advBtn })
        content.addAndMakeVisible (c);
    for (auto* c : advItems) content.addAndMakeVisible (c);
    content.addAndMakeVisible (toast);

    for (auto* id : { params::scType }) ap.addParameterListener (id, this);
    const int v = (int) proc.ui.getProperty ("view", 0);
    viewSeg.setSelected (v); display.setView (v);

    const double sc = juce::jlimit (0.6, 2.0, (double) proc.ui.getProperty ("scale", 1.0));   // lu avant que les limites ne redimensionnent
    setAdvanced ((bool) proc.ui.getProperty ("adv", false), false);   // panneau avancé caché par défaut
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
    setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
    setSize (juce::roundToInt (baseW * sc), juce::roundToInt (currentH() * sc));
    refreshState();
}

AudioLabCompEditor::~AudioLabCompEditor()
{
    proc.apvts.removeParameterListener (params::scType, this);
    cancelPendingUpdate();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabCompEditor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

// Ouvre / ferme le panneau « Réglages avancés » : la fenêtre grandit ou rétrécit de 142 px (à l'échelle)
void AudioLabCompEditor::setAdvanced (bool open, bool resizeWindow)
{
    advOpen = open;
    proc.ui.setProperty ("adv", open, nullptr);
    advBtn.open = open; advBtn.repaint();
    for (auto* c : advItems) c->setVisible (open);
    if (resizeWindow)
    {
        const double s = (double) getWidth() / baseW;   // échelle actuelle, lue avant de changer les limites
        getConstrainer()->setFixedAspectRatio ((double) baseW / currentH());
        setResizeLimits (baseW * 6 / 10, currentH() * 6 / 10, baseW * 2, currentH() * 2);
        setSize (juce::roundToInt (baseW * s), juce::roundToInt (currentH() * s));
    }
    resized();
}
void AudioLabCompEditor::handleAsyncUpdate() { refreshState(); }

void AudioLabCompEditor::refreshState()
{
    const bool scOff = proc.getParam (params::scType) < 0.5f;
    rowScF->setDimmed (scOff); rowScQ->setDimmed (scOff);
    display.setLampOff (bypassed);   // charte V2 : en bypass, la lampe du VU s'éteint ; les réglages restent accessibles
}

void AudioLabCompEditor::resized()
{
    const float s = (float) getWidth() / baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, baseW, currentH());
    proc.ui.setProperty ("scale", (double) s, nullptr);
}

void AudioLabCompEditor::Content::paint (juce::Graphics& g)
{
    const auto& p = ed.theme.pal();
    g.fillAll (p.bg);
    if (! ed.advOpen) return;
    // panneau des réglages avancés (« Surface », coins 12) + libellés des lignes Détection / Side-chain
    g.setColour (p.surface); g.fillRoundedRectangle (24, 354, 532, 126, 12);
    g.setColour (p.text2); g.setFont (ed.theme.ui (12));
    g.drawText (U ("Détection"), juce::Rectangle<float> (40, 438, 68, 26), juce::Justification::centredLeft, false);
    g.drawText ("Side-chain", juce::Rectangle<float> (306, 370, 68, 26), juce::Justification::centredLeft, false);   // trait d'union normal (Poppins)
}

// Grille de 24 px (voir docs/06_GUI_Plugin_Compresseur.md)
void AudioLabCompEditor::Content::resized()
{
    auto& e = ed;
    // 4 gros potentiomètres en 2 × 2 (centres x 75,5 / 202,5 ; y 76 / 223)
    e.kThr->setBounds (4, 20, 143, 130);
    e.kRatio->setBounds (131, 20, 143, 130);
    e.kAtk->setBounds (4, 167, 143, 130);
    e.kRel->setBounds (131, 167, 143, 130);
    // écran carré
    e.display.setBounds (286, 24, 270, 270);
    // rangée du bas : flèche à gauche ; sélecteur de vue + petit Make-up sur la largeur de l'écran
    e.advBtn.setBounds (16, 310, juce::roundToInt (e.advBtn.idealWidth()), 28);
    const int mkW = juce::roundToInt (e.kMk->idealWidth());
    e.kMk->setBounds (286 + 270 - mkW, 310, mkW, 28);
    e.viewSeg.setBounds (286, 310, 270 - 8 - mkW, 28);
    // panneau avancé : 2 colonnes de 234 px, 3 lignes de 26 px
    const int L = 40, R = 306, cw = 234, y0 = 370, y1 = 404, y2 = 438;
    e.rowKnee->setBounds (L, y0, cw, 26);
    e.rowLook->setBounds (L, y1, cw, 26);
    e.detectSeg.setBounds (L + 78, y2, juce::roundToInt (e.detectSeg.idealWidth()), 26);
    e.scBox.setBounds (R + 78, y0, 60, 26);
    const int lw = juce::roundToInt (e.listenBtn.idealWidth());
    e.listenBtn.setBounds (R + 78 + 60 + 10, y0, lw, 26);
    e.rowScF->setBounds (R, y1, cw, 26);
    e.rowScQ->setBounds (R, y2, cw, 26);
    e.toast.setBounds (getLocalBounds());
}
