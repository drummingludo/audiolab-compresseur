#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"
#include "Display.h"

class AudioLabCompEditor : public juce::AudioProcessorEditor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    explicit AudioLabCompEditor (AudioLabCompProcessor&);
    ~AudioLabCompEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // 3.0.0, mise en page « C · Symétrie » validée (docs/06_GUI_Plugin_Compresseur.md) : module de 8 px,
    // 592 × 368 replié (74 × 46 modules), 592 × 696 déplié (panneau avancé de 9 lignes au pas de 32)
    static constexpr int baseW = 592, baseH = 368, baseHOpen = 696;
    static constexpr int advRows = 9;

private:
    // Contenu dessiné à taille fixe puis mis à l'échelle (fenêtre redimensionnable, proportions fixes)
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabCompEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        AudioLabCompEditor& ed;
    };

    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void refreshState();
    void setAdvanced (bool open, bool resizeWindow);
    int currentH() const { return advOpen ? baseHOpen : baseH; }

    AudioLabCompProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    std::unique_ptr<juce::ParameterAttachment> bypassAtt, listenAtt;

    std::unique_ptr<audiolab::ArcKnob> kThr, kRatio, kAtk, kRel, kMk;
    std::unique_ptr<audiolab::SliderRow> rowKnee, rowLook, rowScF, rowScQ;
    audiolab::SegButtons detectSeg { theme, { "Peak", "RMS" } };
    // 2.0.0
    std::unique_ptr<audiolab::SliderRow> rowRms, rowHold, rowLink;
    // 3.0.0
    std::unique_ptr<audiolab::SliderRow> rowRange, rowMix;
    audiolab::SegButtons agSeg { theme, { "Off", "On" } };
    audiolab::SegButtons osfSeg { theme, { "Linear", "Minimum" } };
    audiolab::SegButtons relSeg { theme, { "Fixed", "Auto", "Smart" } };
    audiolab::SegButtons tpSeg { theme, { "Off", "On" } };
    audiolab::SegButtons scSrcSeg { theme, { "Internal", "External" } };
    audiolab::SegButtons modeSeg { theme, { "Stereo", "Mid-Side" } };
    audiolab::SegButtons osSeg { theme, { "Off", juce::String::fromUTF8 ("2\xc3\x97"), juce::String::fromUTF8 ("4\xc3\x97"), juce::String::fromUTF8 ("8\xc3\x97") } };
    // libellés des lignes à sélecteur du panneau (dessinés par Content::paint)
    struct SegLabel { juce::String text; audiolab::SegButtons* seg; };
    std::vector<SegLabel> segLabels;
    juce::ComboBox scBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scAtt;
    audiolab::PillButton listenBtn;
    audiolab::CompDisplay display;
    audiolab::SegButtons viewSeg { theme, { "Transfer", "Activity", "Meters" } };
    audiolab::Disclosure advBtn { theme, "Advanced settings" };
    audiolab::Toast toast { theme };

    juce::Array<juce::Component*> advItems;
    bool bypassed = false, advOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabCompEditor)
};
