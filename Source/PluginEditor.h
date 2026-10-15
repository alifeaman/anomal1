#pragma once

#include "PluginProcessor.h"

class AnomalEditor : public juce::AudioProcessorEditor,
                     public juce::ChangeListener,
                     public juce::ComponentListener,
                     public AnomalProcessor::SlotListener
{
public:
    explicit AnomalEditor (AnomalProcessor&);
    ~AnomalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    void slotWillChange (int slot) override;
    void slotChanged (int slot) override;

private:
    static constexpr int headerH = 64;
    static constexpr int footerH = 30;
    static constexpr int deckW = 1100;
    static constexpr int deckH = 660;

    void showTab (int tab);
    void updateSizeForTab();
    juce::Rectangle<int> contentArea() const;

    AnomalProcessor& proc;

    std::array<juce::TextButton, NumSlots + 1> tabButtons;
    juce::TextButton scanButton { "SCAN" }, paramsButton { "PARAMS" };
    juce::ComboBox routeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> routeAttachment;

    std::unique_ptr<juce::Component> deck;
    std::array<std::unique_ptr<juce::AudioProcessorEditor>, NumSlots> childEditors;
    int currentTab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnomalEditor)
};
