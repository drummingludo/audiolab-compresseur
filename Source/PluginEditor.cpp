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

static void sunMoonIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool dark)
{
    const auto ctr = r.getCentre();
    g.setColour (c);
    if (dark)   // soleil : passer en clair
    {
        g.fillEllipse (ctr.x - 3.5f, ctr.y - 3.5f, 7, 7);
        for (int i = 0; i < 8; ++i)
        {
            const float a = i * juce::MathConstants<float>::pi / 4;
            g.drawLine (ctr.x + std::sin (a) * 5.5f, ctr.y - std::cos (a) * 5.5f, ctr.x + std::sin (a) * 7.5f, ctr.y - std::cos (a) * 7.5f, 1.3f);
        }
    }
    else        // lune : passer en sombre
    {
        juce::Path circ; circ.addEllipse (ctr.x - 6, ctr.y - 6, 12, 12);
        juce::Path cut; cut.addEllipse (ctr.x - 2.5f, ctr.y - 8.5f, 12, 12);
        juce::Path m (circ); m.addPath (cut); m.setUsingNonZeroWinding (false);
        g.saveState(); g.reduceClipRegion (circ);
        g.fillPath (m);
        g.restoreState();
    }
}

static void drawLogo (juce::Graphics& g, juce::Rectangle<float> r, const Pal& p)
{
    // Erlenmeyer au liquide bleu, petites notes en guise de fumée
    const float x = r.getX(), y = r.getY(), s = r.getWidth() / 28.0f;
    auto P = [&] (float px, float py) { return juce::Point<float> (x + px * s, y + py * s); };
    juce::Path flask;
    flask.startNewSubPath (P (11, 9)); flask.lineTo (P (11, 14)); flask.lineTo (P (5, 25));
    flask.quadraticTo (P (4.5f, 27), P (6.5f, 27)); flask.lineTo (P (21.5f, 27)); flask.quadraticTo (P (23.5f, 27), P (23, 25));
    flask.lineTo (P (17, 14)); flask.lineTo (P (17, 9)); flask.closeSubPath();
    juce::Path liquid;
    liquid.startNewSubPath (P (8, 19.5f)); liquid.lineTo (P (20, 19.5f)); liquid.lineTo (P (23, 25));
    liquid.quadraticTo (P (23.5f, 27), P (21.5f, 27)); liquid.lineTo (P (6.5f, 27)); liquid.quadraticTo (P (4.5f, 27), P (5, 25)); liquid.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xFF5AC8FA), P (14, 19).x, P (14, 19).y, juce::Colour (0xFF007AFF), P (14, 27).x, P (14, 27).y, false));
    g.fillPath (liquid);
    g.setColour (p.text.withAlpha (0.85f));
    g.strokePath (flask, juce::PathStrokeType (1.6f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (P (9.5f, 9).x, P (9.5f, 9).y, P (18.5f, 9).x, P (18.5f, 9).y, 1.6f * s);
    auto note = [&] (float nx, float ny, float a)
    {
        g.setColour (juce::Colour (0xFF5AC8FA).withAlpha (a));
        g.fillEllipse (P (nx, ny).x, P (nx, ny).y, 3.2f * s, 2.6f * s);
        g.drawLine (P (nx + 3, ny + 1).x, P (nx + 3, ny + 1).y, P (nx + 3, ny - 4.5f).x, P (nx + 3, ny - 4.5f).y, 1.1f * s);
    };
    note (12.5f, 5.5f, 0.9f); note (17.5f, 2.8f, 0.6f);
}

AudioLabCompEditor::AudioLabCompEditor (AudioLabCompProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      mixKnob (theme, *p.apvts.getParameter (params::mix)),
      resetBtn (theme, PillButton::Style::rst, "Reset"),
      bypassBtn (theme, PillButton::Style::bp, {}),
      themeBtn (theme, PillButton::Style::ibtn, {}),
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

    // En-tête
    presetBox.addItem ("Default", 1);
    for (int i = 0; i < params::numPresets; ++i) presetBox.addItem (U (params::presets[i].name), i + 2);
    presetBox.setTitle ("Presets");
    presetBox.onChange = [this] { choosePreset (presetBox.getSelectedId() - 2); };

    const char* slots[] = { "A", "B", "C" };
    for (int i = 0; i < 3; ++i)
    {
        snaps[i] = std::make_unique<PillButton> (theme, PillButton::Style::snap, slots[i]);
        snaps[i]->setTooltip (U ("Snapshot : clic sur une case vide (deux fois) pour enregistrer, clic pour rappeler, Alt-clic pour remplacer"));
        snaps[i]->onClick = [this, i] { snapClicked (i, juce::ModifierKeys::getCurrentModifiers()); };
        content.addAndMakeVisible (*snaps[i]);
    }
    resetBtn.onClick = [this] { choosePreset (-1); currentSlot = -1; refreshState(); };
    bypassAtt = std::make_unique<juce::ParameterAttachment> (prm (params::bypass), [this] (float v) {
        bypassed = v > 0.5f; refreshState(); }, nullptr);
    bypassBtn.onClick = [this] { bypassAtt->setValueAsCompleteGesture (bypassed ? 0.0f : 1.0f); };
    bypassAtt->sendInitialUpdate();
    bypassBtn.setTitle ("On / Bypass");
    themeBtn.setTooltip (U ("Thème clair / sombre"));
    themeBtn.setTitle (U ("Thème"));
    themeBtn.icon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { sunMoonIcon (g, r.translated (-4, 0).withSizeKeepingCentre (16, 16), c, theme.pal().dark); };
    themeBtn.onClick = [this]
    {
        proc.ui.setProperty ("theme", theme.pal().dark ? "light" : "dark", nullptr);
        applyTheme();
    };

    viewSeg.onSelect = [this] (int v) { display.setView (v); proc.ui.setProperty ("view", v, nullptr); };

    for (juce::Component* c : { (juce::Component*) &mixKnob, (juce::Component*) &presetBox, (juce::Component*) &resetBtn,
                                (juce::Component*) &bypassBtn, (juce::Component*) &themeBtn })
        content.addAndMakeVisible (c);
    body = { kThr.get(), kRatio.get(), kKnee.get(), kAtk.get(), kRel.get(), kMk.get(), rowLook.get(), &detectSeg,
             &scBox, &listenBtn, rowScF.get(), rowScQ.get(), &display, &viewSeg };
    for (auto* c : body) content.addAndMakeVisible (c);
    content.addAndMakeVisible (toast);

    for (auto* id : { params::scType }) ap.addParameterListener (id, this);

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
    cancelPendingUpdate();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AudioLabCompEditor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
void AudioLabCompEditor::handleAsyncUpdate() { refreshState(); }

void AudioLabCompEditor::applyTheme()
{
    const String t = proc.ui.getProperty ("theme", "auto").toString();
    const bool dark = t == "dark" || (t == "auto" && juce::Desktop::getInstance().isDarkModeActive());
    theme.setDark (dark);
    lnf.refresh();
    sendLookAndFeelChange();   // les menus déroulants reprennent les couleurs du thème
    content.repaint();
    for (auto* c : content.getChildren()) c->repaint();
}

juce::String AudioLabCompEditor::noteText() const
{
    const int i = (int) proc.ui.getProperty ("preset", -1);
    if (i >= 0 && i < params::numPresets)
        return U (params::presets[i].name) + U (" — ") + U (params::presets[i].desc);
    return U ("Default — choisissez un preset pour découvrir un réglage type et ce qu'il faut entendre.");
}

void AudioLabCompEditor::choosePreset (int index)
{
    proc.applyPreset (index);
    currentSlot = -1;
    refreshState();
}

void AudioLabCompEditor::snapClicked (int i, const juce::ModifierKeys& mods)
{
    const String slot = snaps[i]->getButtonText();
    const bool armedValid = armedSlot == i && juce::Time::getMillisecondCounter() - armedAt < 2500;
    if (armedValid)
    {
        proc.storeSnapshot (slot);
        armedSlot = -1;
        toast.show ("Snapshot " + slot + U (" enregistré"), true);
    }
    else if (proc.hasSnapshot (slot) && ! mods.isAltDown())
    {
        proc.recallSnapshot (slot);
        currentSlot = i;
        toast.show ("Snapshot " + slot + U (" rappelé"));
    }
    else
    {
        armedSlot = i; armedAt = juce::Time::getMillisecondCounter();
        toast.show (U ("Cliquez à nouveau pour enregistrer dans ") + slot);
        juce::Timer::callAfterDelay (2600, [safe = juce::Component::SafePointer<AudioLabCompEditor> (this)] { if (safe) safe->refreshState(); });
    }
    refreshState();
}

void AudioLabCompEditor::refreshState()
{
    const bool scOff = proc.getParam (params::scType) < 0.5f;
    rowScF->setDimmed (scOff); rowScQ->setDimmed (scOff);
    bypassBtn.on = ! bypassed; bypassBtn.repaint();
    for (auto* c : body) { c->setAlpha (bypassed ? 0.35f : 1.0f); c->setEnabled (! bypassed); }
    if (armedSlot >= 0 && juce::Time::getMillisecondCounter() - armedAt >= 2500) armedSlot = -1;
    for (int i = 0; i < 3; ++i)
    {
        snaps[i]->has = proc.hasSnapshot (snaps[i]->getButtonText());
        snaps[i]->on = currentSlot == i;
        snaps[i]->armed = armedSlot == i;
        snaps[i]->repaint();
    }
    presetBox.setSelectedId ((int) proc.ui.getProperty ("preset", -1) + 2, juce::dontSendNotification);
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
    // En-tête : logo + titre
    drawLogo (g, { 20, 25, 28, 28 }, p);
    g.setColour (p.text); g.setFont (ed.theme.ui (15, 600));
    g.drawText ("Compresseur", juce::Rectangle<float> (56, 24, 200, 18), juce::Justification::centredLeft, false);
    g.setColour (p.muted); g.setFont (ed.theme.ui (11));
    g.drawText (U ("Audio Lab · Chimiste du son"), juce::Rectangle<float> (56, 42, 220, 14), juce::Justification::centredLeft, false);
    g.setColour (p.border); g.fillRect (20.0f, 80.0f, (float) baseW - 40, 1.0f);
    // note du preset
    auto nr = juce::Rectangle<float> (20, 94, (float) baseW - 40, 40);
    g.setColour (p.bg3); g.fillRoundedRectangle (nr, 8);
    g.setColour (p.dim); g.setFont (ed.theme.ui (12));
    g.drawFittedText (ed.noteText(), nr.reduced (12, 6).toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
    // libellés de la ligne Détection / Side-chain
    const float a = ed.bypassed ? 0.35f : 1.0f;
    g.setColour (p.dim.withMultipliedAlpha (a)); g.setFont (ed.theme.ui (12));
    g.drawText (U ("Détection"), juce::Rectangle<float> (20, 462, 80, 28), juce::Justification::centredLeft, false);
    g.drawText (U ("Side‑chain"), juce::Rectangle<float> (20, 498, 80, 28), juce::Justification::centredLeft, false);
}

void AudioLabCompEditor::Content::resized()
{
    auto& e = ed;
    // En-tête (de droite à gauche) : Thème · On · Reset · A B C · Preset · Dry/Wet
    float x = (float) baseW - 20;
    auto place = [&] (juce::Component& c, float w, float h)
    {
        x -= w; c.setBounds (juce::roundToInt (x), juce::roundToInt (49 - h / 2), juce::roundToInt (w), juce::roundToInt (h)); x -= 8;
    };
    place (e.themeBtn, 30, 26);
    place (e.bypassBtn, e.bypassBtn.idealWidth(), 24);
    place (e.resetBtn, e.resetBtn.idealWidth(), 26);
    x += 4;
    for (int i = 2; i >= 0; --i) { x -= 26; e.snaps[i]->setBounds (juce::roundToInt (x), 36, 26, 26); x -= 3; }
    x -= 1;
    place (e.presetBox, 170, 26);
    place (e.mixKnob, 40, 52);
    e.mixKnob.setBounds (e.mixKnob.getBounds().withY (25));

    // Corps : deux colonnes
    const int colW = (baseW - 40 - 18) / 2, lx = 20, rx = 20 + colW + 18, top = 146;
    const int cw = colW / 3;
    ArcKnob* row1[] = { e.kThr.get(), e.kRatio.get(), e.kKnee.get() };
    ArcKnob* row2[] = { e.kAtk.get(), e.kRel.get(), e.kMk.get() };
    for (int i = 0; i < 3; ++i)
    {
        row1[i]->setBounds (lx + i * cw, top, cw, 130);
        row2[i]->setBounds (lx + i * cw, top + 140, cw, 130);
    }
    e.rowLook->setBounds (lx, 424, colW, 32);
    e.detectSeg.setBounds (lx + 86, 462, juce::roundToInt (e.detectSeg.idealWidth()), 28);
    e.scBox.setBounds (lx + 86, 498, 104, 28);
    e.listenBtn.setBounds (lx + 86 + 104 + 10, 499, juce::roundToInt (e.listenBtn.idealWidth()), 26);
    e.rowScF->setBounds (lx, 532, colW, 32);
    e.rowScQ->setBounds (lx, 566, colW, 32);
    e.display.setBounds (rx, top, colW, baseH - 18 - 36 - top);
    e.viewSeg.setBounds (rx, baseH - 18 - 28, juce::roundToInt (e.viewSeg.idealWidth()), 28);
    e.toast.setBounds (getLocalBounds());
}
