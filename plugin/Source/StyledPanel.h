// The styled panel (milestone 3), laid out like the approved mockup (MimicPhon_panel_mockup_1.png).
//
// Every control here is attached to the same parameter ids as the plain-slider window (see Parameters.h), so saved
// projects and automation work the same whichever window is showing. The panel only draws and edits control values; it
// makes no sound. It does look at the engine, read-only, for Logic's tempo and for the two blinking lights.
//
// The read-outs of the controls that make sound (milestone 4: Input and Output Level, Mix, Repeats, Zone, Rate) use the engine's own
// functions (core/Gain.h, core/Repeats.h, core/ZoneMap.h), so the number shown is the number heard. For the controls that do not make
// sound yet, the read-out is a first guess at what the position will mean (a percentage, semitones, Hertz); these are tuned together
// with the sound in the later milestones. The control ids never change. Tempo Sync shows free-running times until milestone 8.
//
// The panel is laid out in a fixed 1200 x 800 space; the editor scales it to the window.
// (The look-and-feel, fonts, KnobCard and the chip selector follow PhenomeEon's StyledPanel, copied and renamed.)

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <mph/Engine.h>
#include "PanelLookAndFeel.h"
#include "Parameters.h"

#include <functional>
#include <memory>
#include <vector>

//==============================================================================
// A row of small buttons for a stepped choice, attached to a choice parameter: the input gain step under Input Level
// (-3 / 0 / +3 / +6) and the Sine / Triangle choice of the uRate LFO.
class ChipSelector : public juce::Component
{
public:
    // "labels" replaces the names of the choices on the chips (empty = use the parameter's names without " dB").
    ChipSelector (juce::AudioProcessorValueTreeState&, const char* parameterId, const juce::StringArray& labels = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Choose a chip (what a click does); used by the automatic checks too.
    void select (int index);
    int getSelected() const { return selected; }
    int getNumChips() const { return labels.size(); }
    const char* getParameterId() const { return parameterId; }

private:
    int indexAt (juce::Point<int>) const;
    juce::Rectangle<float> segment (int index) const;

    const char* parameterId;
    juce::AudioParameterChoice* parameter = nullptr;
    juce::StringArray labels;
    int selected = 0;
    int hovered = -1;
    std::unique_ptr<juce::ParameterAttachment> attachment;     // declared last: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChipSelector)
};

//==============================================================================
// One knob with its title, its read-out, and (for some) a few extras: small texts, marks round the dial, a child control.
class KnobCard : public juce::Component
{
public:
    // "format" turns the slider's value (0..1 for the knobs, the Zone number for the Zone knob) into the text under the knob.
    KnobCard (juce::AudioProcessorValueTreeState&, const char* parameterId, const juce::String& title,
              std::function<juce::String (double)> format = {});

    void paint (juce::Graphics&) override;
    void resized() override;

    // A small piece of text drawn inside the card.
    void addNote (const juce::String& text, juce::Rectangle<int> area, juce::Justification,
                  float size = 11.5f, bool useMono = false, juce::Colour colour = MphStyle::muted);

    // A short line outside the dial at a knob position (0..1), for the unity mark of Color, the 1/1 mark of Rate and the
    // eight steps of Zone.
    void addMark (double position01, juce::Colour colour = MphStyle::muted);

    // A child control placed inside the card.
    void addExtra (juce::Component&, juce::Rectangle<int> area);

    // Optional: gives the arc and the read-out a colour that depends on the slider's value (amber when not set).
    void setAccent (std::function<juce::Colour (double)> colourFor);
    juce::Colour getAccentColour() const;

    const char* getParameterId() const { return parameterId; }
    juce::Slider& getSlider() { return slider; }
    juce::String getReadoutText() const { return format (slider.getValue()); }      // what the card shows under the dial

    static constexpr int kDialSize = 104;
    static constexpr int kDialCentreY = 101;

private:
    void applyAccentToSlider();

    struct Note  { juce::String text; juce::Rectangle<int> area; juce::Justification justification; float size; bool useMono; juce::Colour colour; };
    struct Mark  { double position; juce::Colour colour; };
    struct Extra { juce::Component* component; juce::Rectangle<int> area; };

    const char* parameterId;
    juce::String title;
    std::function<juce::String (double)> format;
    std::function<juce::Colour (double)> accent;       // empty = amber
    std::vector<Note> notes;
    std::vector<Mark> marks;
    std::vector<Extra> extras;

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;   // after the slider: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobCard)
};

//==============================================================================
// The uRate LFO card: two small knobs (Rate, Depth), the Sine / Triangle chips and the Sync button.
class LfoCard : public juce::Component
{
public:
    // "engine" is only looked at: Logic's tempo (the synced speed, and so the held-back swing at fast speeds, depend on it).
    LfoCard (juce::AudioProcessorValueTreeState&, const mph::Engine& engine);

    void paint (juce::Graphics&) override;
    void resized() override;

    // What the card shows under its two small knobs (the same functions as the sound; also used by the automatic checks).
    juce::String rateReadout() const;
    juce::String depthReadout() const;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::AudioProcessorValueTreeState& state;
    const mph::Engine& engine;
    juce::Slider rate, depth;
    ChipSelector shape;
    juce::TextButton sync { "SYNC" };
    std::unique_ptr<SliderAttachment> rateAttachment, depthAttachment;     // after the controls: destroyed first
    std::unique_ptr<ButtonAttachment> syncAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoCard)
};

//==============================================================================
class MimicPhonStyledPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth  = 1200;
    static constexpr int kHeight = 800;

    // "engine" is only looked at (read-only): Logic's tempo and the two counters behind the blinking lights.
    MimicPhonStyledPanel (juce::AudioProcessorValueTreeState&, const mph::Engine& engine, const juce::String& versionText);
    ~MimicPhonStyledPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Called when the "Plain sliders" button is pressed; the editor swaps windows.
    std::function<void()> onSwitchToPlain;

    // The ids of all parameters that have a control in this window (used by the check that none is missing).
    const std::vector<juce::String>& controlledParameterIds() const { return controlIds; }
    const LfoCard* getLfoCard() const { return lfoCard.get(); }

    // What the Zone band and the Rate knob show right now (also used by the automatic checks).
    struct Readout
    {
        int zone = 0;
        juce::String delayLeft, delayRight;     // "653 ms"
        juce::String tempo;                     // "TEMPO 120.0 BPM"
        juce::String rateText;                  // the Rate knob's read-out
        bool synced = false;
        bool pingFlashing = false;              // Ping Pong or Swap is working (the Zone window flashes)
        bool skewed = false;                    // the two sides are noticeably different
        juce::String pingLabel;                 // the Ping Pong / Swap button's label: "PING PONG / SWAP" (off), "PING PONG" or "SWAP"
    };
    Readout getReadout() const;
    juce::Colour getZoneWindowColour() const { return MphStyle::zoneColour (shown.zone); }

    // The lights (true while lit), for the automatic checks.
    bool tickLightOn() const { return tickPulse > 0; }
    bool rateLightOn() const { return ratePulse > 0; }
    juce::String pingButtonText() const { return pingButton != nullptr ? pingButton->getButtonText() : juce::String(); }   // "PING PONG / SWAP", "PING PONG" or "SWAP"
    bool zoneWindowBright() const { return flashBright; }       // false for half a second at a time while Ping Pong / Swap is on

    // Runs the timer's work once (the preview tool calls this instead of waiting).
    void refreshNow() { timerCallback(); }

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct ButtonItem { juce::TextButton button; std::unique_ptr<ButtonAttachment> attachment; };

    KnobCard& addKnob (const char* id, const juce::String& title, std::function<juce::String (double)> format = {});
    void addPanelButton (const char* id, const juce::String& text, juce::Colour onColour = MphStyle::amber);
    void timerCallback() override;
    double currentDelaySeconds (float rate) const;       // the delay time a Rate position gives in the current Zone
    juce::String currentSignature (const Readout&) const;

    void paintZoneBand (juce::Graphics&);

    juce::AudioProcessorValueTreeState& state;
    const mph::Engine& engine;
    juce::String versionText;
    std::vector<juce::String> controlIds;

    std::vector<std::unique_ptr<KnobCard>> cards;
    KnobCard* rateCard = nullptr;
    std::unique_ptr<ChipSelector> gainChips;
    KnobCard* uRateCard = nullptr;
    std::unique_ptr<LfoCard> lfoCard;
    std::vector<std::unique_ptr<ButtonItem>> buttons;
    juce::ComboBox clockBox;
    std::unique_ptr<ComboBoxAttachment> clockAttachment;
    juce::TextButton plainSliders { "PLAIN SLIDERS" };
    juce::TextButton* clearButton = nullptr;
    juce::TextButton* pingButton = nullptr;
    int clearGlow = 0;

    // What was last drawn in the Zone band (so it is redrawn only when something changed).
    Readout shown;
    juce::String shownSignature;
    int lastTicks = 0, lastRateTicks = 0;
    int tickPulse = 0, ratePulse = 0;
    int flashCounter = 0;
    bool flashBright = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicPhonStyledPanel)
};
