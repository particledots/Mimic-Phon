// The plugin window. Milestone 2: the plain slider window, with one control for every parameter (all 21), using the
// parameter ids from Parameters.h, so saved projects and automation work the same whatever window comes later.
// Milestone 3 adds the styled panel (the approved mockup) next to it; this window then stays as the fallback.
//
// The window is laid out in a fixed 1200 x 800 space and scaled to the window size, keeping its shape (3:2).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

#include <memory>
#include <vector>

// Everything in the window, laid out in a fixed 1200 x 800 space. The editor scales it to the window size.
class MimicPhonSliderPanel : public juce::Component
{
public:
    static constexpr int kWidth  = 1200;
    static constexpr int kHeight = 800;

    explicit MimicPhonSliderPanel (juce::AudioProcessorValueTreeState&);
    ~MimicPhonSliderPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The ids of all parameters that have a control in this window (used by the check that none is missing).
    const std::vector<juce::String>& controlledParameterIds() const { return controlIds; }

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // In each row the attachment is declared after the control, so it is destroyed first.
    struct SliderRow  { juce::Label label; juce::Slider slider; std::unique_ptr<SliderAttachment> attachment; };
    struct ChoiceRow  { juce::Label label; juce::ComboBox box;  std::unique_ptr<ComboBoxAttachment> attachment; };
    struct ButtonItem { juce::TextButton button;                std::unique_ptr<ButtonAttachment> attachment; };

    void addSlider (const char* id, const char* text);
    void addChoice (const char* id, const char* text);
    void addButton (const char* id, const char* text);

    juce::AudioProcessorValueTreeState& state;
    std::vector<juce::String> controlIds;
    std::vector<std::unique_ptr<SliderRow>>  sliders;
    std::vector<std::unique_ptr<ChoiceRow>>  choices;
    std::vector<std::unique_ptr<ButtonItem>> buttons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicPhonSliderPanel)
};

class MimicPhonEditor : public juce::AudioProcessorEditor
{
public:
    explicit MimicPhonEditor (MimicPhonProcessor&);
    ~MimicPhonEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // For the automatic checks: the ids the window controls.
    const std::vector<juce::String>& controlledParameterIds() const;

private:
    void layOutPanel();

    MimicPhonProcessor& owner;
    juce::LookAndFeel_V4 plainLook;        // declared before the panel, so it outlives it
    std::unique_ptr<MimicPhonSliderPanel> plain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicPhonEditor)
};
