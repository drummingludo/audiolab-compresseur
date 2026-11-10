#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace audiolab;

static const char* snapshotParams[] = { params::thr, params::ratio, params::knee, params::attack, params::release,
                                        params::makeup, params::look, params::detect, params::scType, params::scFreq,
                                        params::scQ, params::mix,
                                        params::truePeak, params::scSource, params::link, params::stMode, params::hold,
                                        params::autoRel, params::rmsWin, params::oversamp,
                                        params::range, params::autoGain, params::osFilter };

AudioLabCompProcessor::AudioLabCompProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),   // bus optionnel (2.0.0)
      apvts (*this, nullptr, "AudioLabCompresseur", params::createLayout()),
      ui ("ui")
{
    bypassParam = apvts.getParameter (params::bypass);
    ui.setProperty ("theme", "auto", nullptr);
    ui.setProperty ("view", "tr", nullptr);
    ui.setProperty ("preset", -1, nullptr);
    ui.setProperty ("scale", 1.0, nullptr);
}

bool AudioLabCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (out != layouts.getMainInputChannelSet()) return false;
    // side-chain : absent, mono (AAX) ou stéréo
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

CompParams AudioLabCompProcessor::currentParams() const
{
    CompParams p;
    p.thr = getParam (params::thr);
    p.ratio = getParam (params::ratio);             // ≥ 20 = ∞:1 : vrai limiteur (le moteur le reconnaît)
    p.knee = getParam (params::knee);
    p.atk = getParam (params::attack) / 1000.0;
    p.rel = getParam (params::release) / 1000.0;
    p.mk = getParam (params::makeup);
    p.look = getParam (params::look) / 1000.0;
    p.rms = getParam (params::detect) > 0.5f;
    p.scType = juce::roundToInt (getParam (params::scType));
    p.scF = getParam (params::scFreq);
    p.scQ = getParam (params::scQ);
    p.scListen = getParam (params::scListen) > 0.5f;
    p.truePeak = getParam (params::truePeak) > 0.5f;
    p.scExternal = getParam (params::scSource) > 0.5f;
    p.link = getParam (params::link) / 100.0;
    p.midSide = getParam (params::stMode) > 0.5f;
    p.hold = getParam (params::hold) / 1000.0;
    p.autoRel = getParam (params::autoRel) > 0.5f;
    p.rmsWin = getParam (params::rmsWin) / 1000.0;
    p.os = juce::roundToInt (getParam (params::oversamp));
    p.osMin = getParam (params::osFilter) > 0.5f;
    p.range = getParam (params::range);
    p.autoGain = getParam (params::autoGain) > 0.5f;
    return p;
}

void AudioLabCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (wrapperType == wrapperType_AAX) constantLatency = true;
    core.prepare (sampleRate, constantLatency, currentParams());   // toute la mémoire du moteur est réservée ici
    const int block = std::max (512, samplesPerBlock);
    for (auto* v : { &wetL, &wetR, &dryL, &dryR }) v->assign ((size_t) block, 0.0f);
    mixSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (getParam (params::mix) / 100.0f);
    bypassSm.reset (sampleRate, 0.02);
    bypassSm.setCurrentAndTargetValue (getParam (params::bypass) > 0.5f ? 1.0f : 0.0f);
    rmsIn.clear(); rmsOut.clear();

    latency = core.latency();
    setLatencySamples (latency);
}

void AudioLabCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int nIn = getMainBusNumInputChannels(), nOut = getMainBusNumOutputChannels();
    for (int ch = nIn; ch < nOut; ++ch) buffer.clear (ch, 0, total);
    if (nIn == 0 || total == 0) return;

    // side-chain externe : seulement si le logiciel hôte a branché le bus (sinon détection interne)
    const float* sL = nullptr; const float* sR = nullptr;
    if (getBusCount (true) > 1)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) { sL = sc.getReadPointer (0); sR = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0); }
    }
    scConnected.store (sL != nullptr);

    const auto p = currentParams();
    core.setParams (p);

    mixSm.setTargetValue (getParam (params::mix) / 100.0f);
    bypassSm.setTargetValue (getParam (params::bypass) > 0.5f ? 1.0f : 0.0f);

    float* L = buffer.getWritePointer (0);
    float* R = nIn > 1 ? buffer.getWritePointer (1) : nullptr;
    const int chunkMax = (int) wetL.size();

    for (int off = 0; off < total; off += chunkMax)
    {
        const int n = std::min (chunkMax, total - off);
        const float* inR = R != nullptr ? R + off : L + off; // mono : R = L (comme inp[1]||inp[0])
        core.process (L + off, inR, sL != nullptr ? sL + off : nullptr, sR != nullptr ? sR + off : nullptr,
                      wetL.data(), wetR.data(), dryL.data(), dryR.data(), n);
        // latence annoncée : elle ne change qu'au début d'un fondu (lookahead en VST3 / AU, suréchantillonnage)
        if (core.latency() != latency) { latency = core.latency(); setLatencySamples (latency); }

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[off + i], xr = inR[i];
            rmsIn.push ((xl + xr) * 0.5f);

            const float dl = dryL[(size_t) i], dr = dryR[(size_t) i];   // sec recalé par le moteur

            const float m = mixSm.getNextValue(), b = bypassSm.getNextValue();
            float pl, pr;
            if (p.scListen) { pl = wetL[(size_t) i]; pr = wetR[(size_t) i]; }   // on écoute le détecteur
            else
            {
                // aux deux bouts les gains sont exacts (cos (π/2) calculé en float ne vaut pas 0) : à 100 %,
                // la sortie est le traité au bit près, ce qui permet le test de nul parfait (3.0.0)
                const float gd = m >= 1.0f ? 0.0f : std::cos (m * juce::MathConstants<float>::halfPi);
                const float gw = m >= 1.0f ? 1.0f : m <= 0.0f ? 0.0f : std::sin (m * juce::MathConstants<float>::halfPi);
                pl = dl * gd + wetL[(size_t) i] * gw;
                pr = dr * gd + wetR[(size_t) i] * gw;
            }
            const float ol = pl * (1 - b) + dl * b, orr = pr * (1 - b) + dr * b;
            L[off + i] = ol;
            if (R != nullptr) R[off + i] = orr;
            rmsOut.push (R != nullptr ? (ol + orr) * 0.5f : ol);
        }
    }
    tpActive.store (core.truePeakActive());
    inDb.store (rmsIn.db());
    outDb.store (rmsOut.db());
    lastBlockMs.store (juce::Time::getMillisecondCounter());
}

AudioLabCompProcessor::Meters AudioLabCompProcessor::readMeters()
{
    Meters m;
    m.active = juce::Time::getMillisecondCounter() - lastBlockMs.load() < 250;
    m.inDb = inDb.load();
    m.outDb = outDb.load();
    const bool byp = getParam (params::bypass) > 0.5f;
    m.grDb = byp ? 0.0f : core.getGR();
    m.grMaxDb = byp ? 0.0f : core.takeGRMax();
    return m;
}

void AudioLabCompProcessor::setParam (const char* id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void AudioLabCompProcessor::applyPreset (int index)
{
    // Comme Audio Lab : Reset de l'outil (Dry/Wet inchangé), puis valeurs du preset.
    // Les réglages 2.0.0 du détecteur reviennent aussi à leur valeur par défaut ; le suréchantillonnage
    // (choix de qualité et de charge de calcul) et le Dry/Wet ne changent pas.
    setParam (params::thr, 0); setParam (params::ratio, 1); setParam (params::knee, 0);
    setParam (params::attack, 0.1f); setParam (params::release, 10); setParam (params::makeup, 0);
    setParam (params::look, 0); setParam (params::detect, 0); setParam (params::scType, 0);
    setParam (params::scFreq, 1000); setParam (params::scQ, 0.7f); setParam (params::scListen, 0);
    setParam (params::truePeak, 1); setParam (params::scSource, 0); setParam (params::link, 100);
    setParam (params::stMode, 0); setParam (params::hold, 0); setParam (params::autoRel, 0); setParam (params::rmsWin, 3);
    setParam (params::range, 60);   // 3.0.0 ; Auto gain et OS filter, choix de travail, ne changent pas
    if (index >= 0 && index < params::numPresets)
    {
        const auto& pr = params::presets[index];
        setParam (params::thr, pr.thr); setParam (params::ratio, pr.ratio); setParam (params::knee, pr.knee);
        setParam (params::attack, pr.atk); setParam (params::release, pr.rel); setParam (params::makeup, pr.mk);
        setParam (params::look, pr.look); setParam (params::detect, (float) pr.rms);
        setParam (params::scType, (float) pr.sc); setParam (params::scFreq, pr.scF);
    }
    ui.setProperty ("preset", index, nullptr);
}

void AudioLabCompProcessor::storeSnapshot (const juce::String& slot)
{
    juce::ValueTree s ("snap");
    s.setProperty ("slot", slot, nullptr);
    for (auto* id : snapshotParams) s.setProperty (id, getParam (id), nullptr);
    if (auto old = ui.getChildWithProperty ("slot", slot); old.isValid()) ui.removeChild (old, nullptr);
    ui.appendChild (s, nullptr);
}

bool AudioLabCompProcessor::hasSnapshot (const juce::String& slot) const
{
    return ui.getChildWithProperty ("slot", slot).isValid();
}

bool AudioLabCompProcessor::recallSnapshot (const juce::String& slot)
{
    auto s = ui.getChildWithProperty ("slot", slot);
    if (! s.isValid()) return false;
    for (auto* id : snapshotParams)
        if (s.hasProperty (id)) setParam (id, (float) s.getProperty (id));
    return true;
}

void AudioLabCompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto old = state.getChildWithName ("ui"); old.isValid()) state.removeChild (old, nullptr);
    state.appendChild (ui.createCopy(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AudioLabCompProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        if (auto u = state.getChildWithName ("ui"); u.isValid())
        {
            ui.copyPropertiesAndChildrenFrom (u, nullptr);
            state.removeChild (u, nullptr);
        }
        // Session d'une version antérieure : les réglages ajoutés depuis n'y sont pas. JUCE leur laisserait leur
        // valeur du moment ; on les remet explicitement à leur valeur par défaut (comportement de la 1.x).
        for (auto* prm : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (! state.getChildWithProperty ("id", rp->paramID).isValid())
                {
                    juce::ValueTree child ("PARAM");
                    child.setProperty ("id", rp->paramID, nullptr);
                    child.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                    state.appendChild (child, nullptr);
                }
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* AudioLabCompProcessor::createEditor() { return new AudioLabCompEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioLabCompProcessor(); }
