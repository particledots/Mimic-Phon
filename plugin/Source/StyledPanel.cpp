#include "StyledPanel.h"
#include <mph/core/Gain.h>
#include <mph/core/HostClock.h>
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

    // ---- read-outs (first guesses, tuned with the sound in the later milestones) ----

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

    // Color: the words of the manual's description, from the darkest oil-can echo to a thin early digital delay line.
    // "Unity" (about 3:00) reads "clean".
    juce::String colorText (double value)
    {
        if (value < 0.17) return "oil can";
        if (value < 0.34) return "warm BBD";
        if (value < 0.50) return "crunch BBD";
        if (value < 0.67) return "tape";
        if (value < 0.85) return "clean";
        return "thin";
    }

    // uRate: plus / minus 12 semitones from the middle position
    juce::String uRateText (double value)
    {
        const double semitones = (value - 0.5) * 24.0;
        const juce::String sign = semitones > 0.05 ? "+" : "";
        return sign + juce::String (semitones, 1) + " st";
    }

    constexpr double kLfoMinHz = 0.1, kLfoMaxHz = 20.0;

    juce::String lfoRateText (double value, bool synced)
    {
        if (synced)
            return "synced";
        const double hz = kLfoMinHz * std::pow (kLfoMaxHz / kLfoMinHz, value);
        return juce::String (hz, hz < 10.0 ? 2 : 1) + " Hz";
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
LfoCard::LfoCard (juce::AudioProcessorValueTreeState& s)
    : state (s), shape (s, MphParams::uRateLfoShape, { "SINE", "TRI" })
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
    g.setFont (sans (12.5f, false, 0.14f));
    g.drawText ("RATE",  juce::Rectangle<int> (14, 137, 80, 18), juce::Justification::centred, false);
    g.drawText ("DEPTH", juce::Rectangle<int> (114, 137, 80, 18), juce::Justification::centred, false);

    g.setColour (amber);
    g.setFont (mono (15.0f));
    g.drawText (lfoRateText (rate.getValue(), sync.getToggleState()), juce::Rectangle<int> (6, 155, 96, 22), juce::Justification::centred, false);
    g.drawText (percentText (depth.getValue()), juce::Rectangle<int> (106, 155, 96, 22), juce::Justification::centred, false);
}

//==============================================================================
MimicPhonStyledPanel::MimicPhonStyledPanel (juce::AudioProcessorValueTreeState& s, const mph::Engine& e, const juce::String& version)
    : state (s), engine (e), versionText (version)
{
    // ---- first row: Input Level, Mix, Repeats, Halo, Output Level ----
    auto& input  = addKnob (MphParams::inputLevel,  "INPUT LEVEL",  levelText);
    addKnob (MphParams::mix, "MIX", mixText);
    auto& repeats = addKnob (MphParams::repeats,    "REPEATS");
    addKnob (MphParams::halo, "HALO");
    addKnob (MphParams::outputLevel, "OUTPUT LEVEL", levelText);

    // Under Input Level: the four-step gain selector and "dB"
    gainChips = std::make_unique<ChipSelector> (state, MphParams::inputGain);
    controlIds.push_back (MphParams::inputGain);
    input.addExtra (*gainChips, { 10, 184, 170, 26 });
    input.addNote ("dB", { 184, 184, 20, 26 }, juce::Justification::centredLeft, 11.5f, true);

    repeats.addNote ("ONE ECHO ... INFINITE ... BEYOND", { 4, 188, 200, 20 }, juce::Justification::centred, 9.5f, true);

    // ---- second row: Zone, Color, Rate, uRate, uRate LFO ----
    auto& zone = addKnob (MphParams::zone, "ZONE", zoneText);
    zone.setAccent ([] (double v) { return zoneColour ((int) std::lround (v)); });
    for (int i = 0; i < mph::kNumZones; ++i)
        zone.addMark ((double) i / (double) (mph::kNumZones - 1));

    auto& color = addKnob (MphParams::color, "COLOR", colorText);
    color.addMark (5.0 / 6.0, teal);                        // unity, about 3:00
    color.addNote ("DARK",  { 10, 188, 60, 20 },  juce::Justification::centredLeft,  11.0f, true);
    color.addNote ("unity", { 74, 188, 60, 20 },  juce::Justification::centred,      11.0f, true, teal);
    color.addNote ("THIN",  { 138, 188, 60, 20 }, juce::Justification::centredRight, 11.0f, true);

    auto& rate = addKnob (MphParams::rate, "RATE", [this] (double) { return getReadout().rateText; });
    rate.addMark (0.5, teal);                               // 1/1 with Tempo Sync, about 12:00
    rate.addNote ("with Skew on: pulls L and R apart", { 6, 186, 196, 24 }, juce::Justification::centred, 11.5f);
    rateCard = &rate;

    auto& uRate = addKnob (MphParams::uRate, muRate ("RATE"), uRateText);
    uRate.addMark (0.5);
    uRate.addNote ("doppler: chorus / vibrato / flange", { 6, 186, 196, 24 }, juce::Justification::centred, 11.5f);

    lfoCard = std::make_unique<LfoCard> (state);
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
    addPanelButton (MphParams::clear,    "CLEAR");
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
double MimicPhonStyledPanel::currentDelaySeconds() const
{
    auto get = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    const int zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (get (MphParams::zone)));
    const float rate = get (MphParams::rate);

    if (get (MphParams::tempoSync) > 0.5f)
    {
        const int division = juce::jlimit (0, mph::core::kNumClockDivisions - 1, (int) std::lround (get (MphParams::clockDivision)));
        double bpm = (double) engine.getHostBpm();
        if (! (bpm > 0.0))
            bpm = 120.0;
        const double period = mph::core::beatsPerTick (division, (double) engine.getHostBeatsPerBar()) * 60.0 / bpm;
        return mph::core::syncDelaySeconds (zone, rate, period);
    }
    return mph::core::freeDelaySeconds (zone, rate);
}

MimicPhonStyledPanel::Readout MimicPhonStyledPanel::getReadout() const
{
    auto get = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    Readout r;
    r.zone = juce::jlimit (0, mph::kNumZones - 1, (int) std::lround (get (MphParams::zone)));
    r.synced = get (MphParams::tempoSync) > 0.5f;
    r.pingFlashing = get (MphParams::pingPong) > 0.5f;

    // With Skew on the Left and Right times will differ (milestone 6); for now both show the time of the Rate knob.
    const double seconds = currentDelaySeconds();
    r.delayLeft = r.delayRight = delayText (seconds);

    const float bpm = engine.getHostBpm();
    r.tempo = bpm > 0.0f ? "TEMPO " + juce::String (bpm, 1) + " BPM" : juce::String ("TEMPO 120.0 (assumed)");

    if (r.synced)
    {
        const int division = juce::jlimit (0, mph::core::kNumClockDivisions - 1, (int) std::lround (get (MphParams::clockDivision)));
        r.rateText = juce::String (mph::core::kSyncNames[mph::core::syncStep (get (MphParams::rate))])
                     + " (tempo " + mph::core::kClockDivisions[division].name + ")";
    }
    else
        r.rateText = delayText (seconds);

    return r;
}

juce::String MimicPhonStyledPanel::currentSignature (const Readout& r) const
{
    return juce::String (r.zone) + "|" + r.delayLeft + "|" + r.delayRight + "|" + r.tempo + "|" + r.rateText + "|"
           + (r.synced ? "s" : "f") + (r.pingFlashing ? "p" : "n") + (flashBright ? "b" : "d")
           + "|" + juce::String (state.getRawParameterValue (MphParams::uRateLfoSync)->load());
}

void MimicPhonStyledPanel::timerCallback()
{
    const auto r = getReadout();
    bool bandChanged = false;

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
    g.setFont (mono (12.0f));
    g.drawText ("ZONES (each one holds the ones above it)", juce::Rectangle<int> (kStairX - 6, 100, 520, 18), juce::Justification::centredLeft, false);

    for (int i = 0; i < mph::kNumZones; ++i)
    {
        const bool selected = i == shown.zone;
        const int y = kStairTop + i * kStairRowHeight;
        const int x = kStairX + i * kStairStep;
        const auto colour = zoneColour (i);

        g.setColour (selected ? text : muted);
        g.setFont (mono (12.0f, selected));
        g.drawText (juce::String (i), juce::Rectangle<int> (kStairX - 22, y - 7, 14, 14), juce::Justification::centredLeft, false);

        g.setColour (selected ? colour : colour.interpolatedWith (card, 0.66f));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) x, (float) y - (float) kStairBarHeight * 0.5f,
                                                        (float) kStairWidths[i], (float) kStairBarHeight), 2.5f);

        // The range, after the bar (the label without its "0: " in front)
        const juce::String range = juce::String (mph::kZones[i].label).fromFirstOccurrenceOf (": ", false, false);
        g.setColour (selected ? text : muted);
        g.setFont (mono (12.0f, selected));
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
        g.setFont (mono (12.0f));
        g.drawText ("DELAY", juce::Rectangle<int> (x + 18, y + 11, 80, 18), juce::Justification::centredLeft, false);
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
        g.setFont (mono (12.5f));
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
    g.setFont (mono (14.5f));
    g.drawText ("particledots / stereo multi-zone colour repeater", juce::Rectangle<int> (kMargin + 2, 56, 640, 22), juce::Justification::centredLeft, false);

    paintZoneBand (g);

    // Footer
    g.setColour (muted);
    g.setFont (mono (12.5f));
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
