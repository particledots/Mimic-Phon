#include "PluginEditor.h"

namespace
{
    const juce::Colour kBackground (0xff16181b);   // charcoal, as in the approved mockup
    const juce::Colour kAmber      (0xffe8a33d);
    const juce::Colour kText       (0xffe9e6df);
    const juce::Colour kMuted      (0xffa8a59d);

    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Layout, in the fixed 1200 x 800 space (the same 3:2 shape as the styled panel).
    constexpr int kLeft = 24, kLabelWidth = 150, kSliderWidth = 440, kTextBoxWidth = 80;
    constexpr int kRowHeight = 40, kFirstRowY = 92;
    constexpr int kButtonX = 800, kButtonWidth = 370, kButtonHeight = 40, kButtonGap = 8;
    constexpr int kNoteY = 470, kNoteHeight = 150;
}

MimicPhonSliderPanel::MimicPhonSliderPanel (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    // The ten knobs, in panel order (first row, then second row).
    addSlider (MphParams::inputLevel,    "Input Level");
    addSlider (MphParams::mix,           "Mix");
    addSlider (MphParams::repeats,       "Repeats");
    addSlider (MphParams::halo,          "Halo");
    addSlider (MphParams::outputLevel,   "Output Level");
    addSlider (MphParams::color,         "Color");
    addSlider (MphParams::rate,          "Rate");
    addSlider (MphParams::uRate,         "uRate");
    addSlider (MphParams::uRateLfoRate,  "uRate LFO Rate");
    addSlider (MphParams::uRateLfoDepth, "uRate LFO Depth");

    // The four menus.
    addChoice (MphParams::zone,          "Zone");
    addChoice (MphParams::inputGain,     "Input Gain Step");
    addChoice (MphParams::uRateLfoShape, "uRate LFO Shape");
    addChoice (MphParams::clockDivision, "Clock Division");

    // The seven buttons.
    addButton (MphParams::hold,         "HOLD");
    addButton (MphParams::flip,         "FLIP");
    addButton (MphParams::skew,         "SKEW");
    addButton (MphParams::pingPong,     "PING PONG / SWAP");
    addButton (MphParams::clear,        "CLEAR");
    addButton (MphParams::tempoSync,    "TEMPO SYNC");
    addButton (MphParams::uRateLfoSync, "uRATE LFO SYNC");

    // Back to the styled panel
    styledPanelButton.setTooltip ("Switch to the styled panel");
    styledPanelButton.onClick = [this] { if (onSwitchToStyled) onSwitchToStyled(); };
    addAndMakeVisible (styledPanelButton);

    setSize (kWidth, kHeight);
}

MimicPhonSliderPanel::~MimicPhonSliderPanel() = default;

void MimicPhonSliderPanel::addSlider (const char* id, const char* text)
{
    auto row = std::make_unique<SliderRow>();
    row->label.setText (text, juce::dontSendNotification);
    row->label.setFont (font (15.0f));
    row->label.setColour (juce::Label::textColourId, kText);
    row->slider.setTitle (text);
    row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, kTextBoxWidth, 24);
    row->slider.setNumDecimalPlacesToDisplay (3);
    row->slider.setColour (juce::Slider::thumbColourId, kAmber);
    row->slider.setColour (juce::Slider::trackColourId, kAmber.withAlpha (0.55f));
    addAndMakeVisible (row->label);
    addAndMakeVisible (row->slider);
    row->attachment = std::make_unique<SliderAttachment> (state, id, row->slider);
    row->slider.setNumDecimalPlacesToDisplay (3);       // after the attachment, which sets its own
    controlIds.push_back (id);
    sliders.push_back (std::move (row));
}

void MimicPhonSliderPanel::addChoice (const char* id, const char* text)
{
    auto row = std::make_unique<ChoiceRow>();
    row->label.setText (text, juce::dontSendNotification);
    row->label.setFont (font (15.0f));
    row->label.setColour (juce::Label::textColourId, kText);
    row->box.setTitle (text);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        row->box.addItemList (p->choices, 1);
    addAndMakeVisible (row->label);
    addAndMakeVisible (row->box);
    row->attachment = std::make_unique<ComboBoxAttachment> (state, id, row->box);
    controlIds.push_back (id);
    choices.push_back (std::move (row));
}

void MimicPhonSliderPanel::addButton (const char* id, const char* text)
{
    auto item = std::make_unique<ButtonItem>();
    item->button.setButtonText (text);
    item->button.setClickingTogglesState (true);
    item->button.setColour (juce::TextButton::buttonOnColourId, kAmber);
    item->button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (item->button);
    item->attachment = std::make_unique<ButtonAttachment> (state, id, item->button);
    controlIds.push_back (id);
    buttons.push_back (std::move (item));
}

void MimicPhonSliderPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (kText);
    g.setFont (font (26.0f, true));
    g.drawText ("Mimic-Phon", kLeft, 14, 400, 34, juce::Justification::centredLeft);

    g.setColour (kMuted);
    g.setFont (font (13.0f));
    g.drawText ("Plain slider window (the styled panel is one click away, top right)", kLeft, 46, 560, 20, juce::Justification::centredLeft);

    g.setColour (kAmber);
    g.setFont (font (12.0f, true));
    g.drawText ("KNOBS AND MENUS", kLeft, kFirstRowY - 22, 300, 18, juce::Justification::centredLeft);
    g.drawText ("BUTTONS", kButtonX, kFirstRowY - 22, kButtonWidth, 18, juce::Justification::centredLeft);

    // What this version does (and does not do yet), in plain words, so nobody wonders why the knobs are silent.
    const auto note = juce::Rectangle<int> (kButtonX, kNoteY, kButtonWidth, kNoteHeight);
    g.setColour (juce::Colour (0xff1b1e22));
    g.fillRoundedRectangle (note.toFloat(), 6.0f);
    g.setColour (juce::Colour (0xff2a2e33));
    g.drawRoundedRectangle (note.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (kAmber);
    g.setFont (font (12.0f, true));
    g.drawText ("VERSION " JucePlugin_VersionString " - THE REPEATER WITH TEMPO SYNC OF RATE", note.getX() + 14, note.getY() + 10, note.getWidth() - 28, 18,
                juce::Justification::centredLeft);
    g.setColour (kText);
    g.setFont (font (14.0f));
    g.drawFittedText ("Working now: Input Level and gain step, Zone, Rate, Repeats, Mix, Output Level, Clear, Color, Skew, Ping Pong / Swap, uRate and its LFO, Tempo Sync. "
                      "Still silent (built in the next milestones): Halo, "
                      "Flip and Hold. The styled panel shows the same controls.",
                      note.getX() + 14, note.getY() + 34, note.getWidth() - 28, note.getHeight() - 44,
                      juce::Justification::topLeft, 6, 1.0f);

    g.setColour (kMuted);
    g.setFont (font (13.0f));
    g.drawText ("Mimic-Phon v" JucePlugin_VersionString " - an original effect inspired by the feel of a stereo multi-zone repeater; "
                "not affiliated with Make Noise or soundhack",
                kLeft, kHeight - 30, kWidth - 2 * kLeft, 20, juce::Justification::centredLeft);
}

void MimicPhonSliderPanel::resized()
{
    styledPanelButton.setBounds (kButtonX + kButtonWidth - 190, 22, 190, 34);

    int y = kFirstRowY;
    for (auto& row : sliders)
    {
        row->label.setBounds (kLeft, y, kLabelWidth, 30);
        row->slider.setBounds (kLeft + kLabelWidth, y, kSliderWidth + kTextBoxWidth, 30);
        y += kRowHeight;
    }

    y += 10;
    for (auto& row : choices)
    {
        row->label.setBounds (kLeft, y, kLabelWidth, 30);
        row->box.setBounds (kLeft + kLabelWidth, y, 340, 30);
        y += kRowHeight;
    }

    int by = kFirstRowY;
    for (auto& item : buttons)
    {
        item->button.setBounds (kButtonX, by, kButtonWidth, kButtonHeight);
        by += kButtonHeight + kButtonGap;
    }
}

//==============================================================================
namespace
{
    // Window size in Logic: opens at 90%, can be made 70% to 150% of the panel's natural size (3:2 shape kept).
    constexpr int kOpenWidth = 1080, kOpenHeight = 720;
    constexpr int kMinWidth  = 840,  kMinHeight  = 560;
    constexpr int kMaxWidth  = 1800, kMaxHeight  = 1200;

    // Remembered in the saved project (not a control, so it does not touch automation): which window was showing.
    // A project without it (saved before milestone 3) opens with the styled panel.
    const char* const kViewProperty = "editorView";
}

MimicPhonEditor::MimicPhonEditor (MimicPhonProcessor& p)
    : juce::AudioProcessorEditor (p),
      owner (p)
{
    plainLook.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());

    // The panels are laid out in a fixed space and scaled to the window, keeping its shape.
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    if (auto* aspect = getConstrainer())
        aspect->setFixedAspectRatio ((double) kOpenWidth / (double) kOpenHeight);

    const bool plainWasShowing = owner.getValueTreeState().state.getProperty (kViewProperty).toString() == "plain";
    showView (plainWasShowing ? View::plain : View::styled);

    setSize (kOpenWidth, kOpenHeight);
}

MimicPhonEditor::~MimicPhonEditor()
{
    styled.reset();
    plain.reset();
}

void MimicPhonEditor::showView (View view)
{
    styled.reset();
    plain.reset();

    if (view == View::styled)
    {
        styled = std::make_unique<MimicPhonStyledPanel> (owner.getValueTreeState(), owner.getEngine(), JucePlugin_VersionString);
        styled->setLookAndFeel (&styledLook);
        styled->onSwitchToPlain = [this] { requestView (View::plain); };
        addAndMakeVisible (*styled);
    }
    else
    {
        plain = std::make_unique<MimicPhonSliderPanel> (owner.getValueTreeState());
        plain->setLookAndFeel (&plainLook);
        plain->onSwitchToStyled = [this] { requestView (View::styled); };
        addAndMakeVisible (*plain);
    }

    owner.getValueTreeState().state.setProperty (kViewProperty, view == View::plain ? "plain" : "styled", nullptr);
    layOutPanel();
    repaint();
}

void MimicPhonEditor::requestView (View view)
{
    // Not switched straight away: the button that asked is part of the window being replaced.
    juce::Component::SafePointer<MimicPhonEditor> safe (this);
    juce::MessageManager::callAsync ([safe, view]
    {
        if (safe != nullptr)
            safe->showView (view);
    });
}

const std::vector<juce::String>& MimicPhonEditor::controlledParameterIds() const
{
    return styled != nullptr ? styled->controlledParameterIds() : plain->controlledParameterIds();
}

void MimicPhonEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
}

void MimicPhonEditor::layOutPanel()
{
    juce::Component* panel = styled != nullptr ? static_cast<juce::Component*> (styled.get()) : static_cast<juce::Component*> (plain.get());
    if (panel == nullptr)
        return;

    const int naturalWidth  = styled != nullptr ? MimicPhonStyledPanel::kWidth  : MimicPhonSliderPanel::kWidth;
    const int naturalHeight = styled != nullptr ? MimicPhonStyledPanel::kHeight : MimicPhonSliderPanel::kHeight;
    panel->setBounds (0, 0, naturalWidth, naturalHeight);
    panel->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) naturalWidth));
}

void MimicPhonEditor::resized()
{
    layOutPanel();
}
