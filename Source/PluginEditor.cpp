#include "PluginEditor.h"

using namespace audiolab;
using juce::String;

static String U (const char* s) { return String::fromUTF8 (s); }

// Textes d'aide « Pourquoi ? » d'Audio Lab
static const char* helpThr  = "Level above which the processing acts. Lower = more of the signal is affected.";
static const char* helpRat  = "Reduction slope above the threshold: 4:1 → 4 dB in gives only 1 dB out. ∞:1 = true limiter: the output never goes above threshold + makeup, the lookahead sets its attack.";
static const char* helpKn   = "Smooth transition around the threshold. Wide knee = gradual, more discreet compression.";
static const char* helpAtk  = "Reaction time. Long: the transient gets through (punch). Short: everything is held, the sound flattens.";
static const char* helpRel  = "Time to return to unity gain. Too short: pumping/distortion; too long: the level stays squashed.";
static const char* helpMk   = "Make-up gain after compression. Beware the \"louder = better\" bias: compare at equal loudness.";
static const char* helpLook = "The detector sees the signal in advance: the attack can catch the transients without letting them through.";
// 2.0.0
static const char* helpRms  = "RMS window: how long the detector averages the level (RMS detection only). Short = closer to the peaks, long = follows the loudness. Default 3 ms.";
static const char* helpHold = "Hold: keeps the gain reduction in place for this time before the release starts. Avoids distortion on bass notes and a fluttering gain.";
static const char* helpRelA = "Auto release adapts to the music: after a short peak the gain comes back 3 times faster than the Release setting, after a long compression 3 times slower. Less pumping, more punch. Smart: Auto, plus a hold that follows the pitch of sustained notes, so a fast release no longer distorts the bass; single hits release as in Auto.";
static const char* helpTp   = "True peak (\xe2\x88\x9e:1 only): the limiter also catches the peaks between the samples, so the output stays under the ceiling after conversion. Needs 32 samples of lookahead (1 ms at 44.1 and 48 kHz) and adds 64 samples of latency.";
static const char* helpScS  = "Side-chain source. Internal: the compressor listens to its own input. External: it listens to the side-chain input set in your DAW (ducking). With no side-chain connected, it stays internal.";
static const char* helpLink = "Stereo link: 100 % = both channels get the same gain reduction (stable stereo image). 0 % = each channel is compressed on its own. 50 % = the quieter channel gets half the reduction.";
static const char* helpMode = "Stereo: works on Left / Right. Mid-Side: works on the centre (Mid) and the sides (Side) separately; lower the Stereo link to treat them independently. The \xe2\x88\x9e:1 limiter always works on Left / Right.";
static const char* helpOs   = "Oversampling: runs the compressor at 2, 4 or 8 times the sample rate. Less aliasing when the gain moves fast (short attack and release). More CPU, and 85 to 96 samples of latency with the Linear filter. Can be changed while playing. Not used by the \xe2\x88\x9e:1 limiter.";
// 3.0.0
static const char* helpRange = "Range: the most gain reduction the compressor may apply. At 6 dB it never turns the sound down by more than 6 dB, however loud the input. Off = no limit. Not used by the \xe2\x88\x9e:1 limiter.";
static const char* helpAg   = "Auto gain: brings the output back to the loudness of the input, slowly (about 1.5 s), so you compare at equal loudness. Makeup still applies on top. Not used by the \xe2\x88\x9e:1 limiter.";
static const char* helpMix  = "Mix: blends the compressed sound with the untouched input (parallel compression). 100 % = compressed only.";
static const char* helpOsf  = "Oversampling filter. Linear: no phase shift, adds latency (reported to your DAW). Minimum: no latency, for recording and playing live; the phase shifts slightly in the top octave.";

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

// Icônes du sélecteur de vue (16 × 14 px, trait 1,5) : courbe de transfert, activité (forme d'onde), VU-mètres (barres)
static void iconTransfer (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre (16, 14);
    juce::Path p;   // montée en diagonale puis coude : la courbe d'un compresseur
    p.startNewSubPath (b.getX() + 1, b.getBottom() - 1);
    p.lineTo (b.getX() + 8, b.getY() + 3.5f);
    p.lineTo (b.getRight() - 1, b.getY() + 1.5f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
static void iconActivity (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre (17, 14);
    static constexpr float h[] = { 3, 7, 12, 8, 13, 6, 9, 4 };   // forme d'onde symétrique autour du centre
    g.setColour (c);
    for (int i = 0; i < 8; ++i)
    {
        const float x = b.getX() + 1 + i * 2.1f;
        g.drawLine (x, b.getCentreY() - h[i] / 2, x, b.getCentreY() + h[i] / 2, 1.2f);
    }
}
static void iconMeters (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre (14, 13);
    static constexpr float h[] = { 6, 13, 9 };   // trois barres de niveau posées sur une ligne
    g.setColour (c);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (b.getX() + 1 + i * 4.5f, b.getBottom() - h[i], 2.6f, h[i], 1.0f);
}

AudioLabCompEditor::AudioLabCompEditor (AudioLabCompProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      listenBtn (theme, U ("Listen")),
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
    detectSeg.setTooltip (U ("Peak: reacts to the peaks of the signal. RMS: reacts to its average level (closer to loudness). The \xe2\x88\x9e:1 limiter always uses the peaks."));

    // 2.0.0 : réglages du détecteur, side-chain externe, stéréo, suréchantillonnage
    rowRms = std::make_unique<SliderRow> (theme, prm (params::rmsWin), "RMS time", Family::orange, [] (float v) { return params::fmtMs (v); });
    rowRms->setTooltip (U (helpRms));
    rowHold = std::make_unique<SliderRow> (theme, prm (params::hold), "Hold", Family::orange, [] (float v) { return String (juce::roundToInt (v)) + " ms"; });
    rowHold->setTooltip (U (helpHold));
    rowLink = std::make_unique<SliderRow> (theme, prm (params::link), "Stereo link", Family::teal2, [] (float v) { return String (juce::roundToInt (v)) + " %"; });
    rowLink->setTooltip (U (helpLink));
    // 3.0.0
    rowRange = std::make_unique<SliderRow> (theme, prm (params::range), "Range", Family::teal, [] (float v) { return v >= 60 ? String ("Off") : String (v, 1) + " dB"; });
    rowRange->setTooltip (U (helpRange));
    rowMix = std::make_unique<SliderRow> (theme, prm (params::mix), "Mix", Family::teal2, [] (float v) { return String (juce::roundToInt (v)) + " %"; });
    rowMix->setTooltip (U (helpMix));
    for (auto* sg : { &relSeg, &tpSeg, &scSrcSeg, &modeSeg, &osSeg, &agSeg, &osfSeg }) sg->onSurface = true;
    agSeg.attach (prm (params::autoGain));    agSeg.setTooltip (U (helpAg));
    osfSeg.attach (prm (params::osFilter));   osfSeg.setTooltip (U (helpOsf));
    relSeg.attach (prm (params::autoRel));    relSeg.setTooltip (U (helpRelA));
    tpSeg.attach (prm (params::truePeak));    tpSeg.setTooltip (U (helpTp));
    scSrcSeg.attach (prm (params::scSource)); scSrcSeg.setTooltip (U (helpScS));
    modeSeg.attach (prm (params::stMode));    modeSeg.setTooltip (U (helpMode));
    osSeg.attach (prm (params::oversamp));    osSeg.setTooltip (U (helpOs));
    {
        auto sel = scSrcSeg.onSelect;   // choix « External » sans side-chain branché : on le dit
        scSrcSeg.onSelect = [this, sel] (int i)
        {
            sel (i);
            if (i == 1 && ! proc.sideChainConnected()) toast.show (U ("No side-chain input connected: internal detection"));
        };
    }
    segLabels = { { U ("Detection"), &detectSeg }, { U ("Release"), &relSeg }, { U ("True peak"), &tpSeg },
                  { U ("SC source"), &scSrcSeg }, { U ("Mode"), &modeSeg }, { U ("Oversampling"), &osSeg },
                  { U ("Auto gain"), &agSeg }, { U ("OS filter"), &osfSeg } };
    viewSeg.fontSize = 10;
    viewSeg.inset = 4;       // sélecteur de vue : hauteur 32, pastille de 24 (rayons 8 et 4)
    viewSeg.icons = { iconTransfer, iconActivity, iconMeters };   // icônes à la place des mots Transfer / Activity / Meters
    scBox.addItemList ({ "Off", "HPF", "LPF", "Bell +6" }, 1);
    scBox.setTitle ("Side-chain");
    scBox.setTooltip (U ("Detector filter: the compressor only reacts to the chosen part of the spectrum."));
    scAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, params::scType, scBox);

    listenBtn.icon = headphonesIcon;
    listenBtn.setTooltip (U ("Listen to the signal that drives the compression (filtered side-chain)"));
    listenAtt = std::make_unique<juce::ParameterAttachment> (prm (params::scListen), [this] (float v) {
        listenBtn.on = v > 0.5f; listenBtn.repaint(); }, nullptr);
    listenBtn.onClick = [this]
    {
        const bool now = ! listenBtn.on;
        listenAtt->setValueAsCompleteGesture (now ? 1.0f : 0.0f);
        if (now) toast.show (proc.getParam (params::scType) < 0.5f ? U ("Side-chain listen: detector signal (no filter active)")
                                                                   : U ("Listening to the filtered side-chain"), true);
    };
    listenAtt->sendInitialUpdate();

    bypassAtt = std::make_unique<juce::ParameterAttachment> (prm (params::bypass), [this] (float v) {
        bypassed = v > 0.5f; refreshState(); }, nullptr);
    bypassAtt->sendInitialUpdate();   // Bypass du logiciel : la lampe du VU s'éteint
    viewSeg.onSelect = [this] (int v) { display.setView (v); proc.ui.setProperty ("view", v, nullptr); };
    advBtn.setTooltip (U ("Show / hide the detector, side-chain, stereo and oversampling settings"));
    advBtn.onClick = [this] { setAdvanced (! advOpen, true); };

    advItems = { rowKnee.get(), rowLook.get(), &detectSeg, &scBox, &listenBtn, rowScF.get(), rowScQ.get(),
                 rowRms.get(), rowHold.get(), &relSeg, &tpSeg, &scSrcSeg, rowLink.get(), &modeSeg, &osSeg,
                 rowRange.get(), rowMix.get(), &agSeg, &osfSeg };
    for (auto* c : std::initializer_list<juce::Component*> { kThr.get(), kRatio.get(), kAtk.get(), kRel.get(), kMk.get(), &display, &viewSeg, &advBtn })
        content.addAndMakeVisible (c);
    for (auto* c : advItems) content.addAndMakeVisible (c);
    content.addAndMakeVisible (toast);

    for (auto* id : { params::scType, params::detect, params::ratio, params::look, params::oversamp }) ap.addParameterListener (id, this);
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
    for (auto* id : { params::scType, params::detect, params::ratio, params::look, params::oversamp }) proc.apvts.removeParameterListener (id, this);
    cancelPendingUpdate();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabCompEditor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

// Ouvre / ferme le panneau « Réglages avancés » : la fenêtre grandit ou rétrécit de 278 px (à l'échelle)
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
    // grisé = sans effet dans la situation actuelle (le réglage reste modifiable)
    const bool inf = proc.getParam (params::ratio) >= 20;   // ∞:1 : limiteur crête, Left / Right, sans knee
    rowKnee->setDimmed (inf);
    detectSeg.setDimmed (inf);
    rowRms->setDimmed (inf || proc.getParam (params::detect) < 0.5f);
    modeSeg.setDimmed (inf);
    // crête vraie : active dès 32 échantillons d'anticipation (0,67 ms à 48 kHz)
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    tpSeg.setDimmed (! inf || juce::roundToInt (proc.getParam (params::look) / 1000.0 * sr) < 32);
    rowRange->setDimmed (inf);
    agSeg.setDimmed (inf);
    osfSeg.setDimmed (proc.getParam (params::oversamp) < 0.5f);
    content.repaint();
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
    // panneau des réglages avancés (« Surface », coins 12) : 544 × 312, de y 360 à 672
    g.setColour (p.surface); g.fillRoundedRectangle (24, 360, 544, 312, 12);
    g.setFont (ed.theme.ui (12));
    g.setColour (p.text2);
    g.drawText ("Side-chain", juce::Rectangle<float> (304, 376 + 32 * 3, 96, 24), juce::Justification::centredLeft, false);   // trait d'union normal (Poppins)
    for (auto& sl : ed.segLabels)
    {
        auto b = sl.seg->getBounds().toFloat();
        g.setColour (sl.seg->isDimmed() ? p.soft : p.text2);
        g.drawText (sl.text, juce::Rectangle<float> (b.getX() - 96, b.getY(), 96, b.getHeight()), juce::Justification::centredLeft, false);
    }
}

// Mise en page « C · Symétrie », module de 8 px (voir docs/06_GUI_Plugin_Compresseur.md) :
// marges et écarts de 24, 16 entre éléments liés ; axe de symétrie x = 296
void AudioLabCompEditor::Content::resized()
{
    auto& e = ed;
    // potentiomètres : niveau à gauche (teal), temps à droite (orange) ; axes x 80 / 512, y 80 / 224
    // (boîte 144 × 136, centre de l'arc à 72 / 60 ; l'arc de 112 commence à la marge de 24, comme l'écran)
    e.kThr->setBounds (8, 20, 144, 136);
    e.kRatio->setBounds (8, 164, 144, 136);
    e.kAtk->setBounds (440, 20, 144, 136);
    e.kRel->setBounds (440, 164, 144, 136);
    // écran carré 272 (34 modules) = hauteur des deux rangées de potentiomètres, centré sur l'axe
    e.display.setBounds (160, 24, 272, 272);
    // rangée du bas (y 312, hauteur 32) : flèche à gauche, sélecteur de vue centré (3 × 48 + 8), petit Makeup calé à droite
    e.advBtn.setBounds (16, 312, juce::roundToInt (e.advBtn.idealWidth()), 32);
    e.viewSeg.setBounds (220, 312, 152, 32);
    const int mkW = juce::roundToInt (e.kMk->idealWidth());
    e.kMk->setBounds (568 - mkW, 312, mkW, 32);
    // panneau avancé : 2 colonnes de 248 (x 40 et 304), 9 lignes de 24 au pas de 32 ;
    // chaque ligne : étiquette 96, commande 104, valeur 48 ; les sélecteurs font 152 et partent tous de x + 96
    const int L = 40, R = 304, cw = 248;
    auto y = [] (int row) { return 376 + 32 * row; };
    auto seg = [&] (audiolab::SegButtons& s, int x, int row) { s.setBounds (x + 96, y (row), 152, 24); };
    // colonne gauche : dynamique et sortie
    e.rowKnee->setBounds (L, y (0), cw, 24);
    e.rowRange->setBounds (L, y (1), cw, 24);
    e.rowLook->setBounds (L, y (2), cw, 24);
    e.rowHold->setBounds (L, y (3), cw, 24);
    seg (e.relSeg, L, 4);
    seg (e.agSeg, L, 5);
    e.rowMix->setBounds (L, y (6), cw, 24);
    seg (e.tpSeg, L, 7);
    seg (e.osSeg, L, 8);
    // colonne droite : détecteur, side-chain, stéréo
    seg (e.detectSeg, R, 0);
    e.rowRms->setBounds (R, y (1), cw, 24);
    seg (e.scSrcSeg, R, 2);
    e.scBox.setBounds (R + 96, y (3), 72, 24);
    e.listenBtn.setBounds (R + 96 + 80, y (3), 72, 24);
    e.rowScF->setBounds (R, y (4), cw, 24);
    e.rowScQ->setBounds (R, y (5), cw, 24);
    e.rowLink->setBounds (R, y (6), cw, 24);
    seg (e.modeSeg, R, 7);
    seg (e.osfSeg, R, 8);
    e.toast.setBounds (getLocalBounds());
}
