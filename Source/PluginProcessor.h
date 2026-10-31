#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CompressorCore.h"
#include "Params.h"

class AudioLabCompProcessor : public juce::AudioProcessor
{
public:
    AudioLabCompProcessor();
    ~AudioLabCompProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ── Interface ─────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;
    juce::ValueTree ui; // thème, vue, snapshots A/B/C, preset choisi, taille

    void applyPreset (int index);   // -1 = Default (Reset)
    void storeSnapshot (const juce::String& slot);
    bool recallSnapshot (const juce::String& slot);
    bool hasSnapshot (const juce::String& slot) const;
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    // Pro Tools (AAX) exige une latence qui ne change jamais pendant la lecture : la latence annoncée
    // est alors fixée au lookahead maximal (10 ms) et un retard interne complète le lookahead choisi.
    // Activé automatiquement en AAX (prepareToPlay) ; VST3 et AU gardent la latence = lookahead.
    bool constantLatency = false;

    // Mesures pour l'affichage (thread audio → interface)
    struct Meters { float inDb = -80, outDb = -80, grDb = 0, grMaxDb = 0; bool active = false; };
    Meters readMeters();

private:
    void setParam (const char* id, float value);
    audiolab::CompParams currentParams() const;

    audiolab::CompressorCore core;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    // Chemin sec retardé du même lookahead que le chemin traité (pas de filtrage en peigne au Dry/Wet)
    std::vector<float> dryL, dryR;
    int dryW = 0;
    std::vector<float> wetL, wetR, monoR;
    juce::SmoothedValue<float> mixSm, bypassSm;
    int latency = -1;

    // Retard de complément (mode latence constante) : fixedLatency − lookahead
    std::vector<float> padL, padR;
    int padW = 0, fixedLatency = 0;

    // Valeur efficace sur 4096 échantillons (AnalyserNode fftSize 4096 dans Audio Lab), voies sommées en mono
    struct Rms
    {
        std::vector<float> ring = std::vector<float> (4096, 0.0f);
        int w = 0; double sum = 0; int sinceExact = 0;
        void push (float x)
        {
            const float sq = x * x;
            sum += sq - ring[(size_t) w];
            ring[(size_t) w] = sq;
            w = (w + 1) & 4095;
            if (++sinceExact >= 65536) { sum = 0; for (auto v : ring) sum += v; sinceExact = 0; }
        }
        float db() const { const double r = std::sqrt (std::max (0.0, sum) / 4096.0); return r > 0.00001 ? (float) (20 * std::log10 (r)) : -80.0f; }
        void clear() { std::fill (ring.begin(), ring.end(), 0.0f); sum = 0; w = 0; }
    } rmsIn, rmsOut;
    std::atomic<float> inDb { -80 }, outDb { -80 };
    std::atomic<juce::uint32> lastBlockMs { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabCompProcessor)
};
