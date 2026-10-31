#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace audiolab;

static const char* snapshotParams[] = { params::thr, params::ratio, params::knee, params::attack, params::release,
                                        params::makeup, params::look, params::detect, params::scType, params::scFreq,
                                        params::scQ, params::mix };

AudioLabCompProcessor::AudioLabCompProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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
    return out == layouts.getMainInputChannelSet();
}

CompParams AudioLabCompProcessor::currentParams() const
{
    CompParams p;
    p.thr = getParam (params::thr);
    const float r = getParam (params::ratio);
    p.ratio = r >= 20 ? 100 : r;                    // ∞:1, comme compParams()
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
    return p;
}

void AudioLabCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    core.prepare (sampleRate);
    core.setParams (currentParams());
    const int maxLook = core.maxLookahead();
    dryL.assign ((size_t) maxLook + 1, 0.0f);
    dryR.assign ((size_t) maxLook + 1, 0.0f);
    dryW = 0;
    const int block = std::max (512, samplesPerBlock);
    wetL.assign ((size_t) block, 0.0f);
    wetR.assign ((size_t) block, 0.0f);
    monoR.assign ((size_t) block, 0.0f);
    mixSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (getParam (params::mix) / 100.0f);
    bypassSm.reset (sampleRate, 0.02);
    bypassSm.setCurrentAndTargetValue (getParam (params::bypass) > 0.5f ? 1.0f : 0.0f);
    rmsIn.clear(); rmsOut.clear();

    if (wrapperType == wrapperType_AAX) constantLatency = true;
    const double lookMaxSec = apvts.getParameterRange (params::look).end / 1000.0;
    fixedLatency = std::min (maxLook, (int) std::floor (lookMaxSec * sampleRate + 0.5));
    padL.assign ((size_t) fixedLatency + 1, 0.0f);
    padR.assign ((size_t) fixedLatency + 1, 0.0f);
    padW = 0;

    latency = constantLatency ? fixedLatency : core.lookaheadSamples();
    setLatencySamples (latency);
}

void AudioLabCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels();
    for (int ch = nIn; ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, total);
    if (nIn == 0 || total == 0) return;

    const auto p = currentParams();
    core.setParams (p);
    const int look = core.lookaheadSamples();
    if (! constantLatency && look != latency) { latency = look; setLatencySamples (look); }
    const int pad = constantLatency ? std::max (0, fixedLatency - look) : 0;
    const int plen = (int) padL.size();

    mixSm.setTargetValue (getParam (params::mix) / 100.0f);
    bypassSm.setTargetValue (getParam (params::bypass) > 0.5f ? 1.0f : 0.0f);

    float* L = buffer.getWritePointer (0);
    float* R = nIn > 1 ? buffer.getWritePointer (1) : nullptr;
    const int len = (int) dryL.size();
    const int chunkMax = (int) wetL.size();

    for (int off = 0; off < total; off += chunkMax)
    {
        const int n = std::min (chunkMax, total - off);
        const float* inR = R != nullptr ? R + off : L + off; // mono : R = L (comme inp[1]||inp[0])
        core.process (L + off, inR, wetL.data(), wetR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[off + i], xr = inR[i];
            rmsIn.push ((xl + xr) * 0.5f);

            dryL[(size_t) dryW] = xl; dryR[(size_t) dryW] = xr;
            const int ri = (dryW - look + len) % len;
            const float dl = dryL[(size_t) ri], dr = dryR[(size_t) ri];
            dryW = (dryW + 1) % len;

            const float m = mixSm.getNextValue(), b = bypassSm.getNextValue();
            float pl, pr;
            if (p.scListen) { pl = wetL[(size_t) i]; pr = wetR[(size_t) i]; }   // on écoute le détecteur
            else
            {
                const float gd = std::cos (m * juce::MathConstants<float>::halfPi);
                const float gw = std::sin (m * juce::MathConstants<float>::halfPi);
                pl = dl * gd + wetL[(size_t) i] * gw;
                pr = dr * gd + wetR[(size_t) i] * gw;
            }
            const float ol = pl * (1 - b) + dl * b, orr = pr * (1 - b) + dr * b;
            float yl = ol, yr = orr;
            if (constantLatency)
            {
                padL[(size_t) padW] = ol; padR[(size_t) padW] = orr;
                const int pi = (padW - pad + plen) % plen;
                yl = padL[(size_t) pi]; yr = padR[(size_t) pi];
                padW = (padW + 1) % plen;
            }
            L[off + i] = yl;
            if (R != nullptr) R[off + i] = yr;
            rmsOut.push (R != nullptr ? (ol + orr) * 0.5f : ol);
        }
    }
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
    setParam (params::thr, 0); setParam (params::ratio, 1); setParam (params::knee, 0);
    setParam (params::attack, 0.1f); setParam (params::release, 10); setParam (params::makeup, 0);
    setParam (params::look, 0); setParam (params::detect, 0); setParam (params::scType, 0);
    setParam (params::scFreq, 1000); setParam (params::scQ, 0.7f); setParam (params::scListen, 0);
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
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* AudioLabCompProcessor::createEditor() { return new AudioLabCompEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioLabCompProcessor(); }
