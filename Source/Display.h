// Écran carré du compresseur (270 × 270) : vues Transfert, Activité et Vu-Mètres (voir docs/06_GUI_Plugin_Compresseur.md).
#pragma once
#include "PluginProcessor.h"
#include "Theme.h"
#include <map>

namespace audiolab
{
class CompDisplay : public juce::Component, private juce::Timer
{
public:
    CompDisplay (Theme& t, AudioLabCompProcessor& p);
    ~CompDisplay() override { stopTimer(); }

    enum View { transfer = 0, activity = 1, meters = 2 };
    void setView (int v);
    int getView() const { return view; }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    struct M { float in, gr, out; };
    void drawTransfer (juce::Graphics&, float W, float H, const M&);
    void drawActivity (juce::Graphics&, float W, float H, const M&);
    void drawMeters (juce::Graphics&, float W, float H, const M&);
    void hBar (juce::Graphics&, const juce::String& key, float y, float norm, bool gr,
               const juce::String& label, const juce::String& value, float thrNorm = -1);
    void needleVU (juce::Graphics&, float x, float y, float w, float h);
    double curve (double di) const;

    Theme& theme;
    AudioLabCompProcessor& proc;
    int view = transfer;
    float smIn = -80, smGR = 0, smOut = -80;
    std::vector<float> actIn, actOut, actGR;
    struct Peak { float v = 0; double t = 0; };
    std::map<juce::String, Peak> peaks;
    // VU à aiguille : réponse normalisée (99 % en 300 ms, dépassement ≈ 1,3 %)
    double vuPos = 0, vuVel = 0, vuGr = 0, lastT = 0;
    M cur { -80, 0, -80 };
};
} // namespace audiolab
