#include "StyledPanel.h"
#include <mph/core/Color.h>
#include <mph/core/Gain.h>
#include <mph/core/Flip.h>
#include <mph/core/Halo.h>
#include <mph/core/HostClock.h>
#include <mph/core/Repeats.h>
#include <mph/core/URate.h>
#include <mph/core/ZoneMap.h>
#include <cmath>

using namespace MphStyle;

namespace
{
    float textWidth (const juce::Font& font, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (font, s);
    }

    // The letter mu, as the mockup writes uRate.
    juce::String muRate (const char* rest)
    {
        return juce::String::fromUTF8 ("\xc2\xb5") + rest;
    }

    // ---- read-outs: Input / Output Level, Mix, Repeats and the delay time are the real thing since milestone 4 (same functions as the
    //      engine); the others are still first guesses, tuned with the sound in the later milestones ----

    juce::String percentText (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + " %";
    }

    // Input Level and Output Level: decibels (0.0 dB = no change)
    juce::String levelText (double value)
    {
        const float db = mph::dsp::levelKnobToDb ((float) value);
        if (db <= -100.0f)
            return "off";
        return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::String mixText (double value)
    {
        return juce::String (juce::roundToInt (value * 100.0)) + " % wet";
    }

    // Repeats: what is fed back each time round, from the engine's own law: one echo, "x 0.35" (each echo that much of the last),
    // the flat stretch "infinite", and "inf +12 %" for beyond.
    juce::String repeatsText (double value)
    {
        if (value <= 0.0)
            return "one echo";
        const float feedback = mph::core::repeatsToFeedback ((float) value);
        if (feedback < 0.995f)
            return "x " + juce::String (feedback, 2);
        if (feedback < 1.0005f)
            return "infinite";
        return "inf +" + juce::String (juce::roundToInt ((feedback - 1.0f) * 100.0f)) + " %";
    }

    // Color: the words of the manual's description, from the darkest oil-can echo to a thin early digital delay line; the engine's own
    // function, so the word shown is the sound heard. "Unity" (about 3:00) reads "clean".
    juce::String colorText (double value)
    {
        return mph::core::colorWord ((float) value);
    }

    // Halo: the word for the setting and the percentage, from the engine's own function ("off", "string 25 %", "tight verb 50 %", "wide hall 100 %")
    juce::String haloText (double value)
    {
        const float knob = (float) value;
        if (knob <= mph::core::kHaloDeadZone)
            return "off";
        return juce::String (mph::core::haloWord (knob)) + " " + juce::String (juce::roundToInt (value * 100.0)) + " %";
    }

    // uRate: what the knob does in the current Zone, from the engine's own rules (core/URate.h). Zone 0: pitch, in semitones (one volt
    // per octave, for Karplus-style tones). Zones 1-7: how much shorter or longer the delay is made (doppler when it moves).
    juce::String uRateText (double value, int zone)
    {
        const double u = mph::core::uRateBipolar ((float) value);
        if (zone == 0)
        {
            const double semitones = u * mph::core::kURateKnobMaxSemitones;
            return juce::String (semitones > 0.05 ? "+" : "") + juce::String (semitones, 1) + " st";
        }
        const double ms = std::fabs (u) * mph::core::kURateKnobMaxSeconds * 1000.0;
        if (u == 0.0)
            return "0.0 ms";
        return juce::String (ms, 1) + (u > 0.0 ? " ms shorter" : " ms longer");
    }

    // The LFO's Rate: Hz, or with Sync the note division (the Clock Division menu's own names: "1/4", "1/8T", "2 bars" ...)
    juce::String lfoRateText (double value, bool synced)
    {
        if (synced)
            return mph::core::lfoSyncName ((float) value);
        const double hz = mph::core::lfoFreeHz ((float) value);
        return juce::String (hz, hz < 10.0 ? 2 : 1) + " Hz";
    }

    // The LFO's Depth: the peak swing that is heard, in the current Zone's own unit (milliseconds of delay, or semitones in Zone 0)
    juce::String lfoDepthText (float depth, int zone, double hz)
    {
        if (depth <= 0.0f)
            return "off";
        const juce::String plusMinus = juce::String::fromUTF8 ("\xc2\xb1");
        if (zone == 0)
            return plusMinus + juce::String (mph::core::lfoPeakSemitones (depth), 1) + " st";
        const double ms = mph::core::lfoPeakSeconds (depth, hz) * 1000.0;
        return plusMinus + juce::String (ms, ms < 10.0 ? 1 : 0) + " ms";
    }

    // A delay time: "1.3 ms", "81.6 ms", "653 ms", "1.31 s", "12.5 s"
    juce::String delayText (double seconds)
    {
        if (seconds >= 10.0)
            return juce::String (seconds, 1) + " s";
        if (seconds >= 1.0)
            return juce::String (seconds, 2) + " s";
        if (seconds >= 0.1)
            return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms";
        return juce::String (seconds * 1000.0, 1) + " ms";
    }

    // The Zone knob's read-out: the number and its range of delay times ("4: 326.5 ms - 1.31 s" without the colon)
    juce::String zoneText (double value)
    {
        const int zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (value));
        return juce::String (mph::kZones[zone].label).replace (":", "");
    }

    // ---- layout, in the fixed 1200 x 800 space (measured from the approved mockup) ----
    constexpr int kMargin = 48;
    constexpr int kCardWidth = 208, kCardHeight = 214, kCardStep = 224;
    constexpr int kRow1Top = 264, kRow2Top = 492;
    constexpr int kButtonTop = 720, kButtonHeight = 46;
    const juce::Rectangle<int> kBand        { 48, 88, 1104, 160 };
    const juce::Rectangle<int> kZoneWindow  { 68, 98, 140, 140 };
    const juce::Rectangle<int> kReadoutBox  { 900, 104, 228, 128 };

    // The staircase of Zones (illustrative, not to scale, like the manual's drawing): where each bar starts and how long it is.
    constexpr int kStairX = 250, kStairStep = 24, kStairTop = 128, kStairRowHeight = 15, kStairBarHeight = 11;
    constexpr int kStairWidths[8] = { 110, 145, 179, 212, 246, 280, 314, 348 };
}

//==============================================================================
ChipSelector::ChipSelector (juce::AudioProcessorValueTreeState& state, const char* id, const juce::StringArray& labelsToShow)
    : parameterId (id)
{
    parameter = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
    jassert (parameter != nullptr);
    if (parameter == nullptr)
        return;

    if (labelsToShow.isEmpty())
        for (auto& choice : parameter->choices)
            labels.add (choice.replace (" dB", ""));
    else
        labels = labelsToShow;

    setTitle (parameter->getName (64));
    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float newValue)
    {
        selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), juce::roundToInt (newValue));
        repaint();
    });
    attachment->sendInitialUpdate();
}

juce::Rectangle<float> ChipSelector::segment (int index) const
{
    const float gap = 3.0f;
    const int n = juce::jmax (1, labels.size());
    const float segW = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
    return { (segW + gap) * (float) index, 0.0f, segW, (float) getHeight() };
}

int ChipSelector::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (segment (i).contains (p.toFloat()))
            return i;
    return -1;
}

void ChipSelector::paint (juce::Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = segment (i);
        const bool on = i == selected;
        g.setColour (on ? amber : (i == hovered ? controlHot : control));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? amber.brighter (0.3f) : controlEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (on ? background : text);
        g.setFont (mono (13.5f, true));
        g.drawText (labels[i], r, juce::Justification::centred, false);
    }
}

void ChipSelector::select (int index)
{
    if (index >= 0 && index < labels.size() && attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) index);
}

void ChipSelector::mouseDown (const juce::MouseEvent& e)
{
    select (indexAt (e.getPosition()));
}

void ChipSelector::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hovered)
    {
        hovered = i;
        repaint();
    }
}

void ChipSelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
KnobCard::KnobCard (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& cardTitle,
                    std::function<juce::String (double)> formatter)
    : parameterId (id), title (cardTitle), format (formatter ? std::move (formatter) : percentText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTitle (cardTitle);
    slider.onValueChange = [this] { applyAccentToSlider(); repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
}

void KnobCard::setAccent (std::function<juce::Colour (double)> colourFor)
{
    accent = std::move (colourFor);
    applyAccentToSlider();
    repaint();
}

juce::Colour KnobCard::getAccentColour() const
{
    return accent ? accent (slider.getValue()) : amber;
}

// The dial itself is drawn by the look-and-feel, which reads the arc colour from the slider's properties.
void KnobCard::applyAccentToSlider()
{
    if (accent)
        slider.getProperties().set (arcColourProperty, (juce::int64) accent (slider.getValue()).getARGB());
    else
        slider.getProperties().remove (arcColourProperty);
}

void KnobCard::addNote (const juce::String& noteText, juce::Rectangle<int> area, juce::Justification j,
                        float size, bool useMono, juce::Colour colour)
{
    notes.push_back ({ noteText, area, j, size, useMono, colour });
}

void KnobCard::setNoteText (int index, const juce::String& newText)
{
    if (index >= 0 && index < (int) notes.size() && notes[(size_t) index].text != newText)
    {
        notes[(size_t) index].text = newText;
        repaint();
    }
}

void KnobCard::addMark (double position01, juce::Colour colour)
{
    marks.push_back ({ position01, colour });
}

void KnobCard::addExtra (juce::Component& c, juce::Rectangle<int> area)
{
    addAndMakeVisible (c);
    c.setBounds (area);
    extras.push_back ({ &c, area });
}

void KnobCard::resized()
{
    slider.setBounds (juce::Rectangle<int> (kDialSize, kDialSize).withCentre ({ getWidth() / 2, kDialCentreY }));
    for (auto& e : extras)
        e.component->setBounds (e.area);
}

void KnobCard::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (card);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (b, 8.0f, 1.0f);

    g.setColour (text);
    g.setFont (sans (16.5f, true, 0.12f));
    g.drawText (title, juce::Rectangle<int> (0, 10, getWidth(), 22), juce::Justification::centred, false);

    // Marks just outside the dial (the same angles the dial uses)
    const float cx = (float) getWidth() * 0.5f, cy = (float) kDialCentreY;
    const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
    const float r0 = (float) kDialSize * 0.5f + 5.0f, r1 = r0 + 6.0f;
    for (auto& m : marks)
    {
        const float a = start + (float) m.position * (end - start);
        g.setColour (m.colour);
        g.drawLine (cx + r0 * std::sin (a), cy - r0 * std::cos (a), cx + r1 * std::sin (a), cy - r1 * std::cos (a), 1.8f);
    }

    // Make sure the dial (painted after this card) has the colour for the current position, then draw the read-out in it.
    // A long read-out is made smaller to fit the card.
    applyAccentToSlider();
    g.setColour (getAccentColour());
    const juce::String readout = format (slider.getValue());
    float size = 18.0f;
    while (size > 12.0f && textWidth (mono (size), readout) > (float) getWidth() - 14.0f)
        size -= 0.5f;
    g.setFont (mono (size));
    g.drawText (readout, juce::Rectangle<int> (0, 156, getWidth(), 26), juce::Justification::centred, false);

    for (auto& n : notes)
    {
        g.setColour (n.colour);
        g.setFont (n.useMono ? mono (n.size) : sans (n.size, false, 0.02f));
        g.drawFittedText (n.text, n.area, n.justification, 2, 1.0f);
    }
}

//==============================================================================
LfoCard::LfoCard (juce::AudioProcessorValueTreeState& s, const mph::Engine& e)
    : state (s), engine (e), shape (s, MphParams::uRateLfoShape, { "SINE", "TRI" })
{
    for (auto* knob : { &rate, &depth })
    {
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob->onValueChange = [this] { repaint(); };
        addAndMakeVisible (*knob);
    }
    rate.setTitle ("uRate LFO Rate");
    depth.setTitle ("uRate LFO Depth");
    rateAttachment  = std::make_unique<SliderAttachment> (state, MphParams::uRateLfoRate, rate);
    depthAttachment = std::make_unique<SliderAttachment> (state, MphParams::uRateLfoDepth, depth);

    addAndMakeVisible (shape);

    sync.setClickingTogglesState (true);
    sync.onStateChange = [this] { repaint(); };
    addAndMakeVisible (sync);
    syncAttachment = std::make_unique<ButtonAttachment> (state, MphParams::uRateLfoSync, sync);
}

juce::String LfoCard::rateReadout() const
{
    return lfoRateText (rate.getValue(), sync.getToggleState());
}

juce::String LfoCard::depthReadout() const
{
    const int zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (state.getRawParameterValue (MphParams::zone)->load()));
    const double hz = mph::core::lfoHz ((float) rate.getValue(), sync.getToggleState(), (double) engine.getHostBpm(), (double) engine.getHostBeatsPerBar());
    return lfoDepthText ((float) depth.getValue(), zone, hz);
}

void LfoCard::resized()
{
    constexpr int d = 68;
    rate.setBounds (juce::Rectangle<int> (d, d).withCentre ({ 54, 94 }));
    depth.setBounds (juce::Rectangle<int> (d, d).withCentre ({ 154, 94 }));
    shape.setBounds (12, 184, 104, 26);
    sync.setBounds (146, 184, 50, 26);
}

void LfoCard::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (card);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (b, 8.0f, 1.0f);

    g.setColour (text);
    g.setFont (sans (16.5f, true, 0.12f));
    g.drawText (muRate ("RATE LFO"), juce::Rectangle<int> (0, 10, getWidth(), 22), juce::Justification::centred, false);

    g.setColour (muted);
    g.setFont (sans (14.5f, false, 0.14f));
    g.drawText ("RATE",  juce::Rectangle<int> (14, 137, 80, 18), juce::Justification::centred, false);
    g.drawText ("DEPTH", juce::Rectangle<int> (114, 137, 80, 18), juce::Justification::centred, false);

    g.setColour (amber);
    g.setFont (mono (15.0f));
    g.drawText (rateReadout(),  juce::Rectangle<int> (6, 155, 96, 22), juce::Justification::centred, false);
    g.drawText (depthReadout(), juce::Rectangle<int> (106, 155, 96, 22), juce::Justification::centred, false);
}

//==============================================================================
MimicPhonStyledPanel::MimicPhonStyledPanel (juce::AudioProcessorValueTreeState& s, const mph::Engine& e, const juce::String& version)
    : state (s), engine (e), versionText (version)
{
    // ---- first row: Input Level, Mix, Repeats, Halo, Output Level ----
    auto& input  = addKnob (MphParams::inputLevel,  "INPUT LEVEL",  levelText);
    addKnob (MphParams::mix, "MIX", mixText);
    auto& repeats = addKnob (MphParams::repeats,    "REPEATS", [this] (double value) { return repeatsReadout (value); });
    repeatsCard = &repeats;
    auto& halo = addKnob (MphParams::halo, "HALO", haloText);
    halo.addNote ("OFF ... TIGHT ... WIDE", { 4, 186, 200, 24 }, juce::Justification::centred, 13.5f, false);
    addKnob (MphParams::outputLevel, "OUTPUT LEVEL", levelText);

    // Under Input Level: the four-step gain selector and "dB"
    gainChips = std::make_unique<ChipSelector> (state, MphParams::inputGain);
    controlIds.push_back (MphParams::inputGain);
    input.addExtra (*gainChips, { 10, 184, 170, 26 });
    input.addNote ("dB", { 184, 184, 20, 26 }, juce::Justification::centredLeft, 14.0f, true);

    repeats.addNote ("ONE ECHO ... INFINITE+", { 4, 186, 200, 24 }, juce::Justification::centred, 13.5f, false);

    // ---- second row: Zone, Color, Rate, uRate, uRate LFO ----
    auto& zone = addKnob (MphParams::zone, "ZONE", zoneText);
    zone.setAccent ([] (double v) { return zoneColour ((int) std::lround (v)); });
    for (int i = 0; i < mph::kNumZones; ++i)
        zone.addMark ((double) i / (double) (mph::kNumZones - 1));

    auto& color = addKnob (MphParams::color, "COLOR", colorText);
    color.addMark ((double) mph::core::kColorUnity, teal);   // unity, about 3:00 (the engine's own position)
    color.addNote ("DARK",  { 10, 188, 60, 20 },  juce::Justification::centredLeft,  13.5f, true);
    color.addNote ("unity", { 74, 188, 60, 20 },  juce::Justification::centred,      13.5f, true, teal);
    color.addNote ("THIN",  { 138, 188, 60, 20 }, juce::Justification::centredRight, 13.5f, true);

    auto& rate = addKnob (MphParams::rate, "RATE", [this] (double) { return getReadout().rateText; });
    rate.addMark (0.5, teal);                               // 1/1 with Tempo Sync, about 12:00
    rate.addNote ("with Skew on: pulls L and R apart", { 6, 186, 196, 24 }, juce::Justification::centred, 13.5f);
    rateCard = &rate;

    auto& uRate = addKnob (MphParams::uRate, muRate ("RATE"), [this] (double value)
    {
        const int zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (state.getRawParameterValue (MphParams::zone)->load()));
        return uRateText (value, zone);
    });
    uRate.addMark (0.5);
    uRate.addNote ("doppler: chorus / vibrato / flange", { 6, 186, 196, 24 }, juce::Justification::centred, 13.5f);
    uRateCard = &uRate;

    lfoCard = std::make_unique<LfoCard> (state, engine);
    addAndMakeVisible (*lfoCard);
    controlIds.push_back (MphParams::uRateLfoRate);
    controlIds.push_back (MphParams::uRateLfoDepth);
    controlIds.push_back (MphParams::uRateLfoShape);
    controlIds.push_back (MphParams::uRateLfoSync);

    // ---- the button row ----
    addPanelButton (MphParams::hold,     "HOLD");
    addPanelButton (MphParams::flip,     "FLIP");
    addPanelButton (MphParams::skew,     "SKEW");
    addPanelButton (MphParams::pingPong, "PING PONG / SWAP");
    pingButton = &buttons.back()->button;      // its label says which one is working: Ping Pong (sides together) or Swap (skewed)
    addPanelButton (MphParams::clear,    "CLEAR");
    // Clear switches itself off at once (the engine has done its job), so the button would never be seen lit.
    // Show it lit for about a third of a second so a click is visible.
    clearButton = &buttons.back()->button;
    clearButton->onClick = [this] { clearGlow = 10; clearButton->getProperties().set ("glow", true); clearButton->repaint(); };
    addPanelButton (MphParams::tempoSync, "TEMPO SYNC", teal);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (MphParams::clockDivision)))
        clockBox.addItemList (p->choices, 1);
    clockBox.setTitle ("Clock Division");
    clockBox.setColour (juce::ComboBox::textColourId, amber);
    clockAttachment = std::make_unique<ComboBoxAttachment> (state, MphParams::clockDivision, clockBox);
    controlIds.push_back (MphParams::clockDivision);
    addAndMakeVisible (clockBox);

    plainSliders.setTooltip ("Switch to the plain slider window");
    plainSliders.onClick = [this] { if (onSwitchToPlain) onSwitchToPlain(); };
    addAndMakeVisible (plainSliders);

    shown = getReadout();
    shownSignature = currentSignature (shown);
    startTimerHz (30);
}

MimicPhonStyledPanel::~MimicPhonStyledPanel()
{
    stopTimer();
}

KnobCard& MimicPhonStyledPanel::addKnob (const char* id, const juce::String& title, std::function<juce::String (double)> format)
{
    auto card = std::make_unique<KnobCard> (state, id, title, std::move (format));
    addAndMakeVisible (*card);
    controlIds.push_back (id);
    cards.push_back (std::move (card));
    return *cards.back();
}

void MimicPhonStyledPanel::addPanelButton (const char* id, const juce::String& label, juce::Colour onColour)
{
    auto item = std::make_unique<ButtonItem>();
    item->button.setButtonText (label);
    item->button.setClickingTogglesState (true);
    if (onColour != amber)
        item->button.getProperties().set ("onColour", (juce::int64) onColour.getARGB());
    addAndMakeVisible (item->button);
    item->attachment = std::make_unique<ButtonAttachment> (state, id, item->button);
    controlIds.push_back (id);
    buttons.push_back (std::move (item));
}

//==============================================================================
double MimicPhonStyledPanel::currentDelaySeconds (float rate) const
{
    auto get = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    const int zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (get (MphParams::zone)));

    // With Tempo Sync on, the same function and the same clock as the engine's (Logic's tempo, or 120 BPM when Logic gives none)
    if (mph::kTempoSyncAudible && get (MphParams::tempoSync) > 0.5f)
    {
        const int division = juce::jlimit (0, mph::core::kNumClockDivisions - 1, (int) std::lround (get (MphParams::clockDivision)));
        const double period = mph::core::clockPeriodSeconds (division, (double) engine.getHostBeatsPerBar(), (double) engine.getHostBpm());
        return mph::core::syncDelaySeconds (zone, rate, period);
    }
    return mph::core::freeDelaySeconds (zone, rate);
}

// Repeats: the echoes (as before), or with Hold on the start of the loop inside the Zone, from the engine's own function (core/Hold.h)
juce::String MimicPhonStyledPanel::repeatsReadout (double value) const
{
    const auto r = getReadout();
    if (! r.holdOn)
        return repeatsText (value);
    const double start = mph::core::holdStartSeconds ((float) value, r.zone, r.holdLoopLeft);
    return start < 0.0005 ? juce::String ("newest") : "start " + delayText (start);
}

MimicPhonStyledPanel::Readout MimicPhonStyledPanel::getReadout() const
{
    auto get = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    Readout r;
    r.zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (get (MphParams::zone)));
    r.synced = mph::kTempoSyncAudible && get (MphParams::tempoSync) > 0.5f;

    // Skew and Ping Pong / Swap: the same rules and the same functions as the sound (core/Skew.h), so the read-out is what is heard.
    const float rate = get (MphParams::rate);
    const bool stereo = engine.isStereo();
    const auto skewState = engine.getSkewState();
    const auto mode = mph::core::pingMode (get (MphParams::pingPong) > 0.5f, stereo, skewState);
    r.pingFlashing = mode != mph::core::PingMode::off;
    r.pingLabel = mph::core::pingLabel (mode);
    r.skewed = stereo && skewState.skewed;

    float rateLeft = rate, rateRight = rate;
    if (stereo)
    {
        const auto sides = skewState.ratesNow (rate);
        rateLeft = sides.left;
        rateRight = sides.right;
    }
    const double hop = mode == mph::core::PingMode::pingPong ? mph::core::kPingPongHop : 1.0;     // Ping Pong: each bounce is half the time
    // uRate's steady offset is in the delay that is heard (Zones 1-7: seconds, opposite on the right when Skew has split the sides;
    // Zone 0: pitch). The LFO swings around this and is not in the read-out.
    const float uRateKnob = get (MphParams::uRate);
    const bool splitSides = stereo && skewState.skewed;
    r.flipOn = get (MphParams::flip) > 0.5f;
    r.holdOn = get (MphParams::hold) > 0.5f;
    if (r.holdOn)
    {
        // Hold: the loop is as long as the delay time (core/Hold.h); in Zone 0 uRate sets its pitch, so its length; the other Zones keep the
        // length and uRate only bends the pitch. The start point is worked out from the left side's loop.
        const double leftBase = currentDelaySeconds (rateLeft) * hop, rightBase = currentDelaySeconds (rateRight) * hop;
        r.holdLoopLeft = mph::core::holdLoopSeconds (leftBase);
        r.delayLeft = delayText (mph::core::holdLoopSeconds (r.zone == 0 ? mph::core::uRateStaticDelay (0, leftBase, uRateKnob, 0, false) : leftBase));
        r.delayRight = delayText (mph::core::holdLoopSeconds (r.zone == 0 ? mph::core::uRateStaticDelay (0, rightBase, uRateKnob, 1, false) : rightBase));
    }
    else if (r.flipOn)
    {
        // Flip: what is heard is the memory read backwards in chunks as long as the delay time (core/Flip.h). uRate only bends the pitch
        // (Zones 1-7), so it is not in the figure; in Zone 0 it sets the pitch of the loop and the chunk. Long chunks are held to what fits.
        r.delayLeft = delayText (mph::core::flipChunkSeconds (r.zone, currentDelaySeconds (rateLeft) * hop, uRateKnob));
        r.delayRight = delayText (mph::core::flipChunkSeconds (r.zone, currentDelaySeconds (rateRight) * hop, uRateKnob));
    }
    else
    {
        r.delayLeft = delayText (mph::core::uRateStaticDelay (r.zone, currentDelaySeconds (rateLeft) * hop, uRateKnob, 0, splitSides));
        r.delayRight = delayText (mph::core::uRateStaticDelay (r.zone, currentDelaySeconds (rateRight) * hop, uRateKnob, 1, splitSides));
    }

    const float bpm = engine.getHostBpm();
    r.tempo = bpm > 0.0f ? "TEMPO " + juce::String (bpm, 1) + " BPM" : juce::String ("TEMPO 120.0 (assumed)");

    if (r.skewed && r.synced)
    {
        // Tempo Sync with the sides split: each side sits on its own step of the clock (two multiples at once)
        r.rateText = "L " + juce::String (mph::core::kSyncNames[mph::core::syncStep (rateLeft)])
                     + "  R " + juce::String (mph::core::kSyncNames[mph::core::syncStep (rateRight)]);
    }
    else if (r.skewed)
    {
        // The Rate knob is pulling the two sides apart (or has left a split behind): say how far, and which side is faster
        const float skew = skewState.skewNow (rate);
        const int percent = juce::roundToInt (std::fabs (skew) / (0.5f * mph::core::kSkewSwing) * 100.0f);
        r.rateText = juce::String (skew > 0.0f ? "L" : "R") + " faster " + juce::String (percent) + " %";
    }
    else if (r.synced)
    {
        const int division = juce::jlimit (0, mph::core::kNumClockDivisions - 1, (int) std::lround (get (MphParams::clockDivision)));
        r.rateText = juce::String (mph::core::kSyncNames[mph::core::syncStep (get (MphParams::rate))])
                     + " (tempo " + mph::core::kClockDivisions[division].name + ")";
    }
    else
        r.rateText = delayText (currentDelaySeconds (stereo ? skewState.baseNow (rate) : rate));

    return r;
}

juce::String MimicPhonStyledPanel::currentSignature (const Readout& r) const
{
    return juce::String (r.zone) + "|" + r.delayLeft + "|" + r.delayRight + "|" + r.tempo + "|" + r.rateText + "|"
           + (r.synced ? "s" : "f") + (r.pingFlashing ? "p" : "n") + (flashBright ? "b" : "d") + (r.flipOn ? "F" : "-") + (r.holdOn ? "H" : "-") + "|" + r.pingLabel
           + "|" + juce::String (state.getRawParameterValue (MphParams::uRateLfoSync)->load());
}

void MimicPhonStyledPanel::timerCallback()
{
    const auto r = getReadout();
    bool bandChanged = false;

    // The Ping Pong / Swap button says which of the two is working
    if (pingButton != nullptr && pingButton->getButtonText() != r.pingLabel)
        pingButton->setButtonText (r.pingLabel);

    if (clearGlow > 0 && --clearGlow == 0 && clearButton != nullptr)
    {
        clearButton->getProperties().set ("glow", false);
        clearButton->repaint();
    }

    // The two lights: lit for about 130 ms after each tick
    const bool tickWas = tickPulse > 0, rateWas = ratePulse > 0;
    const int ticks = engine.getClockTickCount(), rateTicks = engine.getRateTickCount();
    if (ticks != lastTicks)      { lastTicks = ticks;           tickPulse = 4; }  else if (tickPulse > 0) --tickPulse;
    if (rateTicks != lastRateTicks) { lastRateTicks = rateTicks; ratePulse = 4; }  else if (ratePulse > 0) --ratePulse;
    bandChanged = (tickWas != (tickPulse > 0)) || (rateWas != (ratePulse > 0));

    // The Zone window flashes while Ping Pong / Swap is on
    if (r.pingFlashing)
    {
        if (++flashCounter >= 15)
        {
            flashCounter = 0;
            flashBright = ! flashBright;
        }
    }
    else if (! flashBright)
    {
        flashBright = true;
        flashCounter = 0;
    }

    const auto signature = currentSignature (r);
    if (signature != shownSignature)
    {
        shownSignature = signature;
        shown = r;
        bandChanged = true;
        if (rateCard != nullptr)
            rateCard->repaint();
        if (repeatsCard != nullptr)
        {
            if (r.holdOn != lastHoldShown)
            {
                lastHoldShown = r.holdOn;
                repeatsCard->setNoteText (0, r.holdOn ? "NEWEST ... START POINT ... OLDEST" : "ONE ECHO ... INFINITE+");
            }
            repeatsCard->repaint();         // with Hold on its read-out depends on the Zone and Rate too
        }
        if (uRateCard != nullptr)
            uRateCard->repaint();           // its read-out depends on the Zone
        if (lfoCard != nullptr)
            lfoCard->repaint();
    }

    if (bandChanged)
        repaint (kBand);
}

//==============================================================================
void MimicPhonStyledPanel::paintZoneBand (juce::Graphics& g)
{
    // The box
    g.setColour (card);
    g.fillRoundedRectangle (kBand.toFloat(), 8.0f);
    g.setColour (cardEdge);
    g.drawRoundedRectangle (kBand.toFloat().reduced (0.5f), 8.0f, 1.0f);

    // The big Zone window (it dims and brightens while Ping Pong / Swap is on)
    {
        const auto colour = zoneColour (shown.zone);
        const auto win = kZoneWindow.toFloat();
        g.setColour (colour.interpolatedWith (background, 0.62f));
        g.fillRoundedRectangle (win, 16.0f);
        g.setColour (flashBright ? colour : colour.interpolatedWith (background, 0.65f));
        g.fillRoundedRectangle (win.reduced (6.0f), 11.0f);

        g.setColour (background);
        g.setFont (sans (15.0f, true, 0.32f));
        g.drawText ("ZONE", kZoneWindow.withTrimmedTop (14).withHeight (22), juce::Justification::centred, false);
        g.setFont (sans (86.0f, true));
        g.drawText (juce::String (shown.zone), kZoneWindow.withTrimmedTop (36).withHeight (96), juce::Justification::centred, false);
    }

    // The staircase: each Zone holds the ones above it
    g.setColour (muted);
    g.setFont (mono (14.0f));
    g.drawText ("ZONES (each one holds the ones above it)", juce::Rectangle<int> (kStairX - 6, 100, 520, 18), juce::Justification::centredLeft, false);

    for (int i = 0; i < mph::kNumZones; ++i)
    {
        const bool selected = i == shown.zone;
        const int y = kStairTop + i * kStairRowHeight;
        const int x = kStairX + i * kStairStep;
        const auto colour = zoneColour (i);

        g.setColour (selected ? text : muted);
        g.setFont (mono (14.0f, selected));
        g.drawText (juce::String (i), juce::Rectangle<int> (kStairX - 22, y - 7, 14, 14), juce::Justification::centredLeft, false);

        g.setColour (selected ? colour : colour.interpolatedWith (card, 0.66f));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) x, (float) y - (float) kStairBarHeight * 0.5f,
                                                        (float) kStairWidths[i], (float) kStairBarHeight), 2.5f);

        // The range, after the bar (the label without its "0: " in front)
        const juce::String range = juce::String (mph::kZones[i].label).fromFirstOccurrenceOf (": ", false, false);
        g.setColour (selected ? text : muted);
        g.setFont (mono (14.0f, selected));
        g.drawText (range, juce::Rectangle<int> (x + kStairWidths[i] + 8, y - 8, 190, 16), juce::Justification::centredLeft, false);
    }

    // The delay and tempo read-out, with the two lights
    {
        g.setColour (well);
        g.fillRoundedRectangle (kReadoutBox.toFloat(), 8.0f);
        g.setColour (cardEdge);
        g.drawRoundedRectangle (kReadoutBox.toFloat().reduced (0.5f), 8.0f, 1.0f);

        const int x = kReadoutBox.getX(), y = kReadoutBox.getY();
        g.setColour (muted);
        g.setFont (mono (14.0f));
        g.drawText (shown.holdOn ? (shown.flipOn ? "DELAY HOLD FLIP" : "DELAY  HOLD") : shown.flipOn ? "DELAY  FLIP" : "DELAY", juce::Rectangle<int> (x + 18, y + 11, 130, 18), juce::Justification::centredLeft, false);
        g.drawText ("TICK", juce::Rectangle<int> (x + 158, y + 11, 40, 18), juce::Justification::centredRight, false);
        g.drawText ("RATE", juce::Rectangle<int> (x + 158, y + 34, 40, 18), juce::Justification::centredRight, false);

        auto light = [&g, x, y] (int dy, juce::Colour on, bool lit)
        {
            const auto dot = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) x + 212.0f, (float) y + (float) dy });
            g.setColour (lit ? on : on.interpolatedWith (well, 0.78f));
            g.fillEllipse (dot);
        };
        light (20, teal, tickPulse > 0);
        light (43, amber, ratePulse > 0);

        g.setColour (amber);
        g.setFont (mono (22.0f));
        g.drawText ("L  " + shown.delayLeft,  juce::Rectangle<int> (x + 18, y + 34, 200, 28), juce::Justification::centredLeft, false);
        g.drawText ("R  " + shown.delayRight, juce::Rectangle<int> (x + 18, y + 62, 200, 28), juce::Justification::centredLeft, false);

        g.setColour (text);
        g.setFont (mono (14.0f));
        g.drawText (shown.tempo, juce::Rectangle<int> (x + 18, y + 96, 200, 20), juce::Justification::centredLeft, false);
    }
}

void MimicPhonStyledPanel::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Title
    g.setColour (text);
    g.setFont (sans (46.0f, true, 0.12f));
    g.drawText ("MIMIC-PHON", juce::Rectangle<int> (kMargin, 16, 560, 48), juce::Justification::centredLeft, false);
    g.setColour (muted);
    g.setFont (mono (16.0f));
    g.drawText ("particledots / stereo multi-zone colour repeater", juce::Rectangle<int> (kMargin + 2, 56, 640, 22), juce::Justification::centredLeft, false);

    paintZoneBand (g);

    // Footer
    g.setColour (muted);
    g.setFont (mono (14.0f));
    g.drawText ("Mimic-Phon v" + versionText + " - an original effect inspired by the feel of a stereo multi-zone repeater; "
                "not affiliated with Make Noise or soundhack",
                juce::Rectangle<int> (kMargin, 772, kWidth - 2 * kMargin, 20), juce::Justification::centredLeft, false);
}

void MimicPhonStyledPanel::resized()
{
    // Nine knob cards: five in the first row, four in the second (the uRate LFO card is the fifth of the second row)
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const int row = i < 5 ? 0 : 1;
        const int col = i < 5 ? (int) i : (int) i - 5;
        cards[i]->setBounds (kMargin + col * kCardStep, row == 0 ? kRow1Top : kRow2Top, kCardWidth, kCardHeight);
    }
    lfoCard->setBounds (kMargin + 4 * kCardStep, kRow2Top, kCardWidth, kCardHeight);

    const int xs[6] = { 48, 180, 312, 444, 656, 890 };
    const int ws[6] = { 120, 120, 120, 200, 120, 130 };
    for (size_t i = 0; i < buttons.size() && i < 6; ++i)
        buttons[i]->button.setBounds (xs[i], kButtonTop, ws[i], kButtonHeight);
    clockBox.setBounds (1032, kButtonTop, 120, kButtonHeight);

    plainSliders.setBounds (960, 30, 192, 35);
}
