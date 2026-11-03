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

    // Dimensions validées (docs/06_GUI_Plugin_Compresseur.md, charte V2) : 580 × 362 replié ;
    // déplié 580 × 640 depuis la 2.0.0 (panneau avancé de 7 lignes au lieu de 3)
    static constexpr int baseW = 580, baseH = 362, baseHOpen = 640;
    static constexpr int advRows = 7;

private:
    // Contenu dessiné à taille fixe puis mis à l'échelle (fenêtre redimensionnable, proportions fixes)
    class Content : public juce::Component
    {
    public:
        explicit Content (AudioLabCompEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void resized() override;
        float labelGap (const juce::String& label) const;
        static constexpr int advRowsH = 34 * advRows;   // hauteur des lignes du panneau avancé (26 + 8 par ligne)
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
    audiolab::SegButtons relSeg { theme, { "Fixed", "Auto" } };
    audiolab::SegButtons tpSeg { theme, { "Off", "On" } };
    audiolab::SegButtons scSrcSeg { theme, { "Internal", "External" } };
    audiolab::SegButtons modeSeg { theme, { "Stereo", "Mid-Side" } };
    audiolab::SegButtons osSeg { theme, { "Off", juce::String::fromUTF8 ("2\xc3\x97"), juce::String::fromUTF8 ("4\xc3\x97") } };
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
