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

    static constexpr int baseW = 920, baseH = 636;

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
    void applyTheme();
    void refreshState();
    void snapClicked (int slot, const juce::ModifierKeys& mods);
    void choosePreset (int index);
    juce::String noteText() const;

    AudioLabCompProcessor& proc;
    audiolab::Theme theme;
    audiolab::ALLookAndFeel lnf { theme };
    juce::TooltipWindow tooltips { nullptr, 600 };
    Content content { *this };

    // En-tête
    audiolab::MixKnob mixKnob;
    juce::ComboBox presetBox;
    std::unique_ptr<audiolab::PillButton> snaps[3];
    audiolab::PillButton resetBtn, bypassBtn, themeBtn;
    int armedSlot = -1, currentSlot = -1;
    juce::uint32 armedAt = 0;
    std::unique_ptr<juce::ParameterAttachment> bypassAtt, listenAtt;

    // Corps
    std::unique_ptr<audiolab::ArcKnob> kThr, kRatio, kKnee, kAtk, kRel, kMk;
    std::unique_ptr<audiolab::SliderRow> rowLook, rowScF, rowScQ;
    audiolab::SegButtons detectSeg { theme, { "Peak", "RMS" } };
    juce::ComboBox scBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scAtt;
    audiolab::PillButton listenBtn;
    audiolab::CompDisplay display;
    audiolab::SegButtons viewSeg { theme, { "Transfert", juce::String::fromUTF8 ("Activité"), juce::String::fromUTF8 ("Mètres") } };
    audiolab::Toast toast { theme };

    juce::Array<juce::Component*> body;
    bool bypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioLabCompEditor)
};
