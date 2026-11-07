// Offline preview and control checks (not part of the plugin itself; only built with -DMPH_BUILD_PREVIEW=ON).
//
// It does these things without needing Logic:
//   1. draws the styled panel and the plain slider window to picture files;
//   2. checks, in BOTH windows, that the 21 control ids are exactly the frozen list, that every control changes exactly one
//      parameter, that every parameter has exactly one control, and that host automation moves the controls;
//   3. checks the switch between the two windows (the PLAIN SLIDERS and STYLED PANEL buttons), that the choice is remembered in
//      the saved project, and that a project saved before milestone 3 opens with the styled panel;
//   4. checks what the styled panel shows: the Zone window colour, the delay read-out, the tempo read-out, the Rate knob's
//      read-out with Tempo Sync, the flashing Zone window with Ping Pong, and the two blinking lights;
//   5. drives the real processor with audio the way Logic does: with Mix fully left the live signal comes out unchanged, bit for bit
//      (mono and stereo, several block sizes); with Mix fully right the echo lands at the delay time the window shows, in every
//      Zone; the repeats stay sane with every control at an extreme; Clear silences the repeats; the host is told the tail;
//   6. checks saving and reloading, the Clear button switching itself off, and opening and closing the window while audio runs;
//   7. (milestone 9) checks Halo: the card's words follow the engine's, and through the real processor Halo makes a tail with Repeats fully
//      left and Mix fully right (Filter-Verb), is silent when fully left, and stays finite; picture styled_panel_halo.png.
//   8. (milestone 10) checks Flip: the Delay box shows the length of the reversed chunk (and says FLIP), and through the real processor the echo
//      is heard backwards (a click lands mirrored round the chunk start); picture styled_panel_flip.png.
//   9. (milestone 11) checks Hold: the Delay box shows the loop length, Repeats reads "newest" / "start ...", and through the real
//      processor the loop keeps sounding with silence or new sound at the input;
//
// Usage: MimicPhonPreview <folder for the pictures>

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <mph/core/Color.h>
#include <mph/core/Flip.h>
#include <mph/core/Halo.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <set>

namespace
{
    int checks = 0, failures = 0;

    void expect (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::cout << "FAIL: " << what << std::endl;
        }
    }

    template <typename T>
    void collect (juce::Component& c, std::vector<T*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (child))
                out.push_back (t);
            collect<T> (*child, out);
        }
    }

    std::map<juce::String, float> snapshotParams (juce::AudioProcessor& p)
    {
        std::map<juce::String, float> m;
        for (auto* param : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                m[withId->paramID] = withId->getValue();
        return m;
    }

    std::vector<juce::String> changedBetween (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
    {
        std::vector<juce::String> changed;
        for (auto& [id, v] : a)
            if (std::abs (b.at (id) - v) > 1.0e-6f)
                changed.push_back (id);
        return changed;
    }

    void resetAll (juce::AudioProcessor& p)
    {
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    void save (juce::Component& c, const juce::File& file, float scale)
    {
        auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, out);
        }
        expect (file.existsAsFile() && file.getSize() > 1000, "picture written: " + file.getFileName());
        std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")" << std::endl;
    }

    void setParameter (MimicPhonProcessor& proc, const char* id, float value01)
    {
        if (auto* p = proc.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (value01);
    }

    float parameterValue (MimicPhonProcessor& proc, const char* id)
    {
        return proc.getValueTreeState().getParameter (id)->getValue();
    }

    void letWindowThreadRun (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    // The frozen list of ids: if this check ever fails, an id was renamed, removed or added by mistake.
    const char* const kFrozenIds[] = {
        "inputLevel", "mix", "repeats", "halo", "outputLevel", "color", "rate", "uRate", "uRateLfoRate", "uRateLfoDepth",
        "zone", "inputGain", "uRateLfoShape", "clockDivision",
        "hold", "flip", "skew", "pingPong", "clear", "tempoSync", "uRateLfoSync"
    };

    // Feeds blocks of noise through the real processor and returns true if every sample came out exactly as it went in.
    bool passesThroughUnchanged (MimicPhonProcessor& proc, int channels, int blockSize, int numBlocks)
    {
        juce::AudioBuffer<float> buffer (channels, blockSize);
        juce::AudioBuffer<float> original (channels, blockSize);
        juce::MidiBuffer midi;
        juce::Random random (99);
        bool same = true;
        for (int b = 0; b < numBlocks; ++b)
        {
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (c, i, random.nextFloat() * 2.0f - 1.0f);
            original.makeCopyOf (buffer);
            proc.processBlock (buffer, midi);
            for (int c = 0; c < channels; ++c)
            {
                const bool equal = std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), sizeof (float) * (size_t) blockSize) == 0;
                if (! equal && same)
                {
                    int first = 0;
                    while (first < blockSize && buffer.getSample (c, first) == original.getSample (c, first))
                        ++first;
                    std::cout << "  (first difference: block " << b << ", channel " << c << ", sample " << first << ": in "
                              << original.getSample (c, first) << " out " << buffer.getSample (c, first) << ")" << std::endl;
                }
                same = same && equal;
            }
        }
        return same;
    }

    // How many controls of each kind a window has.
    struct Expected { int sliders, boxes, chips, toggles; };

    // Moves every control in the window and checks the result.
    void checkControls (MimicPhonProcessor& proc, MimicPhonEditor& editor, const Expected& expected, const juce::String& which)
    {
        resetAll (proc);

        std::set<juce::String> allIds;
        for (auto& kv : snapshotParams (proc))
            allIds.insert (kv.first);

        std::set<juce::String> frozen;
        for (auto* id : kFrozenIds)
            frozen.insert (id);
        expect (frozen.size() == (size_t) MphParams::kNumParameters, "the frozen list has 21 ids");
        expect (allIds == frozen, "the plugin's parameter ids are exactly the frozen list");
        expect (allIds.size() == 21, "21 parameters");

        const auto& ids = editor.controlledParameterIds();
        std::set<juce::String> controlled (ids.begin(), ids.end());
        expect (ids.size() == controlled.size(), which + ": no parameter has two controls");
        expect (controlled == allIds, which + ": every parameter has a control");

        std::set<juce::String> moved;
        auto moveAndCheck = [&] (const juce::String& what, const std::function<void()>& action)
        {
            const auto before = snapshotParams (proc);
            action();
            const auto changed = changedBetween (before, snapshotParams (proc));
            expect (changed.size() == 1, which + ": " + what + " changes exactly one parameter (changed " + juce::String ((int) changed.size()) + ")");
            if (changed.size() == 1)
            {
                expect (moved.insert (changed[0]).second, which + ": " + what + " is the only control for " + changed[0]);
                std::cout << "  " << which << ": " << what << " -> " << changed[0] << std::endl;
            }
        };

        std::vector<juce::Slider*> sliders;
        collect (editor, sliders);
        expect ((int) sliders.size() == expected.sliders, which + ": " + juce::String (expected.sliders) + " knobs / sliders (found " + juce::String ((int) sliders.size()) + ")");
        for (auto* s : sliders)
            moveAndCheck ("slider '" + s->getTitle() + "'", [s] { s->setValue (0.9, juce::sendNotificationSync); });

        std::vector<juce::ComboBox*> boxes;
        collect (editor, boxes);
        expect ((int) boxes.size() == expected.boxes, which + ": " + juce::String (expected.boxes) + " menus (found " + juce::String ((int) boxes.size()) + ")");
        for (auto* b : boxes)
            moveAndCheck ("menu '" + b->getTitle() + "'", [b]
            {
                b->setSelectedItemIndex (b->getSelectedItemIndex() == 1 ? 2 : 1, juce::sendNotificationSync);
            });

        std::vector<ChipSelector*> chips;
        collect (editor, chips);
        expect ((int) chips.size() == expected.chips, which + ": " + juce::String (expected.chips) + " chip selectors (found " + juce::String ((int) chips.size()) + ")");
        for (auto* c : chips)
            moveAndCheck ("chips '" + c->getTitle() + "'", [c] { c->select (c->getSelected() == 0 ? 1 : 0); });

        std::vector<juce::TextButton*> buttons;
        collect (editor, buttons);
        int toggles = 0;
        for (auto* b : buttons)
        {
            if (! b->getClickingTogglesState())
                continue;
            ++toggles;
            moveAndCheck ("button '" + b->getButtonText() + "'", [b] { b->setToggleState (! b->getToggleState(), juce::sendNotificationSync); });
        }
        expect (toggles == expected.toggles, which + ": " + juce::String (expected.toggles) + " on/off buttons (found " + juce::String (toggles) + ")");

        expect (moved == allIds, which + ": moving the controls reached all 21 parameters");

        // And the other way: a parameter change shows up in the control (host automation -> window)
        if (! sliders.empty())
        {
            resetAll (proc);
            proc.getParameters()[0]->setValueNotifyingHost (0.25f);
            letWindowThreadRun (50);
            expect (std::abs (sliders.front()->getValue() - 0.25) < 1.0e-3, which + ": host automation moves the first knob");
        }

        resetAll (proc);
    }

    // Clicks a button the way a mouse would, then lets the window thread finish the switch.
    template <typename Panel>
    juce::TextButton* findButton (Panel& panel, const juce::String& text)
    {
        std::vector<juce::TextButton*> buttons;
        collect (panel, buttons);
        for (auto* b : buttons)
            if (b->getButtonText() == text)
                return b;
        return nullptr;
    }

    void processSeconds (MimicPhonProcessor& proc, double seconds, int block, const std::function<void()>& afterEachBlock = {})
    {
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        const int blocks = (int) (seconds * 48000.0 / block);
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            if (afterEachBlock)
                afterEachBlock();
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    MimicPhonProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    expect (proc.getName() == "MimicPhon", "the plugin is called MimicPhon");
    expect (proc.getParameters().size() == 21, "the plugin exposes 21 parameters to the host");

    // ---------------- control checks: the styled panel (the default) and the plain window ----------------
    {
        MimicPhonEditor editor (proc);
        editor.setSize (1080, 720);
        expect (editor.getView() == MimicPhonEditor::View::styled, "a new window opens with the styled panel");
        checkControls (proc, editor, { 11, 1, 2, 7 }, "styled");

        editor.showView (MimicPhonEditor::View::plain);
        expect (editor.getView() == MimicPhonEditor::View::plain, "the editor can show the plain window");
        checkControls (proc, editor, { 10, 4, 0, 7 }, "plain");

        // Control values survive a save and reload
        resetAll (proc);
        setParameter (proc, MphParams::mix, 0.8f);
        setParameter (proc, MphParams::zone, 3.0f / 7.0f);
        setParameter (proc, MphParams::skew, 1.0f);
        juce::MemoryBlock saved;
        proc.getStateInformation (saved);
        MimicPhonProcessor proc2;
        proc2.setStateInformation (saved.getData(), (int) saved.getSize());
        expect (std::abs (parameterValue (proc2, MphParams::mix) - 0.8f) < 1.0e-4f, "Mix survives save and reload");
        expect (std::abs (parameterValue (proc2, MphParams::zone) - 3.0f / 7.0f) < 1.0e-4f, "Zone survives save and reload");
        expect (parameterValue (proc2, MphParams::skew) > 0.5f, "Skew survives save and reload");
        resetAll (proc);
    }

    // ---------------- switching windows, and the choice remembered in the saved project ----------------
    {
        MimicPhonProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        {
            MimicPhonEditor editor (p);
            editor.setSize (1080, 720);
            expect (editor.getView() == MimicPhonEditor::View::styled, "switching: starts on the styled panel");

            // Click PLAIN SLIDERS
            auto* toPlain = findButton (editor, "PLAIN SLIDERS");
            expect (toPlain != nullptr && ! toPlain->getClickingTogglesState(), "the styled panel has a PLAIN SLIDERS button (not an on/off one)");
            if (toPlain != nullptr)
                toPlain->triggerClick();
            letWindowThreadRun (100);
            expect (editor.getView() == MimicPhonEditor::View::plain, "PLAIN SLIDERS switches to the plain window");
            expect (editor.getPlainPanel() != nullptr && editor.getStyledPanel() == nullptr, "only the plain window exists after the switch");

            // The choice is in the saved project
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            MimicPhonProcessor reopened;
            reopened.setStateInformation (saved.getData(), (int) saved.getSize());
            {
                MimicPhonEditor again (reopened);
                again.setSize (1080, 720);
                expect (again.getView() == MimicPhonEditor::View::plain, "a project saved with the plain window reopens with the plain window");
            }

            // Click STYLED PANEL
            auto* toStyled = findButton (editor, "Styled panel");
            expect (toStyled != nullptr && ! toStyled->getClickingTogglesState(), "the plain window has a Styled panel button (not an on/off one)");
            if (toStyled != nullptr)
                toStyled->triggerClick();
            letWindowThreadRun (100);
            expect (editor.getView() == MimicPhonEditor::View::styled, "Styled panel switches back to the styled panel");

            juce::MemoryBlock savedStyled;
            p.getStateInformation (savedStyled);
            MimicPhonProcessor reopenedStyled;
            reopenedStyled.setStateInformation (savedStyled.getData(), (int) savedStyled.getSize());
            {
                MimicPhonEditor again (reopenedStyled);
                expect (again.getView() == MimicPhonEditor::View::styled, "a project saved with the styled panel reopens with the styled panel");
            }

            // Switching must not move any control
            resetAll (p);
            setParameter (p, MphParams::zone, 2.0f / 7.0f);
            setParameter (p, MphParams::mix, 0.3f);
            const auto before = snapshotParams (p);
            for (int i = 0; i < 6; ++i)
            {
                editor.showView (i % 2 == 0 ? MimicPhonEditor::View::plain : MimicPhonEditor::View::styled);
                letWindowThreadRun (10);
            }
            expect (changedBetween (before, snapshotParams (p)).empty(), "switching windows six times moves no control");
        }

        // A project saved before milestone 3 (no remembered window) opens with the styled panel
        {
            MimicPhonProcessor old;                       // no window has been opened on it, so it has no remembered window
            juce::MemoryBlock block;
            old.getStateInformation (block);
            MimicPhonProcessor opened;
            opened.setStateInformation (block.getData(), (int) block.getSize());
            MimicPhonEditor editor (opened);
            expect (editor.getView() == MimicPhonEditor::View::styled, "a project without a remembered window opens with the styled panel");
        }
    }

    // ---------------- what the styled panel shows ----------------
    {
        MimicPhonProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        resetAll (p);
        MimicPhonEditor editor (p);
        editor.setSize (1200, 800);
        auto* panel = editor.getStyledPanel();
        expect (panel != nullptr, "the styled panel exists");
        if (panel != nullptr)
        {
            // Zone window colours: eight different colours, as in the mockup (0 green ... 7 blue)
            std::set<juce::uint32> colours;
            for (int z = 0; z < 8; ++z)
                colours.insert (MphStyle::zoneColour (z).getARGB());
            expect (colours.size() == 8, "the eight Zones have eight different colours");
            expect (MphStyle::zoneColour (-3) == MphStyle::zoneColour (0) && MphStyle::zoneColour (12) == MphStyle::zoneColour (7), "a Zone outside 0..7 is clamped");
            expect (MphStyle::zoneColour (0).getHue() > 0.25f && MphStyle::zoneColour (0).getHue() < 0.40f, "Zone 0 is green");
            expect (MphStyle::zoneColour (4).getHue() > 0.96f || MphStyle::zoneColour (4).getHue() < 0.03f, "Zone 4 is red");
            expect (MphStyle::zoneColour (7).getHue() > 0.55f && MphStyle::zoneColour (7).getHue() < 0.65f, "Zone 7 is blue");

            // Defaults: Zone 4, Rate in the middle, no tempo from Logic yet
            panel->refreshNow();
            auto r = panel->getReadout();
            expect (r.zone == 4 && panel->getZoneWindowColour() == MphStyle::zoneColour (4), "defaults: Zone 4 and the red window");
            expect (r.delayLeft == "653 ms" && r.delayRight == "653 ms", "defaults: the delay read-out says 653 ms (Zone 4, Rate in the middle)");
            expect (r.rateText == "653 ms" && ! r.synced, "defaults: the Rate knob says 653 ms");
            expect (r.tempo == "TEMPO 120.0 (assumed)", "no tempo from Logic yet: the read-out says 120 is assumed");

            // Every Zone: the window follows the Zone menu, and the delay stays inside the Zone's range
            for (int z = 0; z < 8; ++z)
            {
                setParameter (p, MphParams::zone, (float) z / 7.0f);
                panel->refreshNow();
                r = panel->getReadout();
                expect (r.zone == z && panel->getZoneWindowColour() == MphStyle::zoneColour (z), "Zone " + juce::String (z) + ": the window shows its colour");
            }
            setParameter (p, MphParams::zone, 0.0f);
            setParameter (p, MphParams::rate, 0.0f);
            expect (panel->getReadout().delayLeft == "1.3 ms", "Zone 0, Rate fully left: 1.3 ms");
            setParameter (p, MphParams::rate, 1.0f);
            expect (panel->getReadout().delayLeft == "20.4 ms", "Zone 0, Rate fully right: 20.4 ms");
            setParameter (p, MphParams::zone, 1.0f);                       // the top of the Zone menu = Zone 7
            expect (panel->getReadout().delayLeft == "41.8 s", "Zone 7, Rate fully right: 41.8 s (the longest delay time)");

            // Tempo Sync (milestone 8): the Rate card names the step and the clock, the delay read-out is the clocked time
            resetAll (p);
            setParameter (p, MphParams::tempoSync, 1.0f);
            panel->refreshNow();
            r = panel->getReadout();
            expect (r.synced && r.rateText == "1/1 (tempo 1/4)", "Tempo Sync on: Rate in the middle reads 1/1 (tempo 1/4)");
            expect (r.delayLeft == "500 ms" && r.delayRight == "500 ms", "Tempo Sync on: the delay read-out is 500 ms (a quarter note at 120 BPM, Zone 4)");
            setParameter (p, MphParams::rate, 1.0f);
            r = panel->getReadout();
            expect (r.rateText == "/2 (tempo 1/4)" && r.delayLeft == "1.00 s", "Tempo Sync on: Rate fully right reads /2 (tempo 1/4), 1.00 s");
            setParameter (p, MphParams::rate, 0.0f);
            r = panel->getReadout();
            expect (r.rateText == "x2 (tempo 1/4)" && r.delayLeft == "250 ms", "Tempo Sync on: Rate fully left reads x2 (tempo 1/4), 250 ms");
            setParameter (p, MphParams::rate, 0.5f);
            setParameter (p, MphParams::clockDivision, 4.0f / 9.0f);
            r = panel->getReadout();
            expect (r.rateText == "1/1 (tempo 1/4T)" && r.delayLeft == "667 ms", "Tempo Sync on, Clock Division 1/4T: 1/1 (tempo 1/4T), 667 ms (Zone 4 doubles the 333 ms that fits Zone 3)");
            setParameter (p, MphParams::zone, 3.0f / 7.0f);
            expect (panel->getReadout().delayLeft == "333 ms", "Tempo Sync on, Zone 3: the time halves (333 ms)");
            setParameter (p, MphParams::tempoSync, 0.0f);
            panel->refreshNow();
            r = panel->getReadout();
            expect (! r.synced && r.delayLeft == r.rateText, "Tempo Sync off again: the Rate card reads the free-running time and agrees with the delay read-out");

            // The read-outs of the controls that make sound say what is heard
            resetAll (p);
            std::vector<KnobCard*> knobCards;
            collect (*panel, knobCards);
            auto cardText = [&] (const char* id) -> juce::String
            {
                for (auto* card : knobCards)
                    if (juce::String (card->getParameterId()) == id)
                        return card->getReadoutText();
                return "(no such card)";
            };
            expect (cardText (MphParams::inputLevel) == "0.0 dB" && cardText (MphParams::outputLevel) == "0.0 dB", "defaults: Input Level and Output Level read 0.0 dB");
            expect (cardText (MphParams::mix) == "50 % wet", "defaults: Mix reads 50 % wet");
            expect (cardText (MphParams::repeats) == "x 0.35", "defaults: Repeats reads x 0.35 (what is fed back each time round)");
            setParameter (p, MphParams::repeats, 0.0f);
            expect (cardText (MphParams::repeats) == "one echo", "Repeats fully left reads \"one echo\"");
            setParameter (p, MphParams::repeats, 0.83f);
            expect (cardText (MphParams::repeats) == "infinite", "Repeats at 0.83 reads \"infinite\"");
            setParameter (p, MphParams::repeats, 1.0f);
            expect (cardText (MphParams::repeats) == "inf +12 %", "Repeats fully right reads \"inf +12 %\" (beyond infinite)");
            // Color reads the engine's own words, and its teal mark sits on the engine's unity position
            expect (cardText (MphParams::color) == "clean" && mph::kDefaultColor == mph::core::kColorUnity, "defaults: Color reads \"clean\" (unity, about 3:00)");
            setParameter (p, MphParams::color, 0.0f);
            expect (cardText (MphParams::color) == "oil can", "Color fully left reads \"oil can\"");
            setParameter (p, MphParams::color, 0.4f);
            expect (cardText (MphParams::color) == "crunch BBD", "Color at 0.4 reads \"crunch BBD\"");
            setParameter (p, MphParams::color, 0.6f);
            expect (cardText (MphParams::color) == "tape", "Color at 0.6 reads \"tape\"");
            setParameter (p, MphParams::color, 1.0f);

            // Halo (milestone 9) reads the engine's own word and the percentage
            expect (cardText (MphParams::halo) == "off" && mph::kDefaultHalo == 0.0f, "defaults: Halo reads \"off\"");
            setParameter (p, MphParams::halo, 0.1f);
            expect (cardText (MphParams::halo) == "glint 10 %", "Halo at 0.1 reads \"glint 10 %\"");
            setParameter (p, MphParams::halo, 0.25f);
            expect (cardText (MphParams::halo) == "string 25 %", "Halo at 0.25 reads \"string 25 %\"");
            setParameter (p, MphParams::halo, 0.5f);
            expect (cardText (MphParams::halo) == "tight verb 50 %", "Halo in the middle reads \"tight verb 50 %\" (the module's tight Karplus-like reverb)");
            setParameter (p, MphParams::halo, 0.7f);
            expect (cardText (MphParams::halo) == "room 70 %", "Halo at 0.7 reads \"room 70 %\"");
            setParameter (p, MphParams::halo, 1.0f);
            expect (cardText (MphParams::halo) == "wide hall 100 %", "Halo fully right reads \"wide hall 100 %\"");
            setParameter (p, MphParams::halo, 0.0f);
            expect (cardText (MphParams::halo) == "off", "Halo fully left reads \"off\" again");
            expect (cardText (MphParams::color) == "thin", "Color fully right reads \"thin\"");
            setParameter (p, MphParams::color, mph::core::kColorUnity);
            expect (cardText (MphParams::color) == juce::String (mph::core::colorWord (mph::core::kColorUnity)), "Color on unity reads the engine's word for unity");
            setParameter (p, MphParams::color, mph::kDefaultColor);
            setParameter (p, MphParams::inputLevel, 1.0f);
            setParameter (p, MphParams::outputLevel, 0.0f);
            expect (cardText (MphParams::inputLevel) == "+6.0 dB" && cardText (MphParams::outputLevel) == "off", "Input Level at the top reads +6.0 dB; Output Level at 0 reads off");
            resetAll (p);

            // Ping Pong makes the Zone window flash (half a second dim, half a second bright); without it, always bright
            resetAll (p);
            panel->refreshNow();
            expect (panel->zoneWindowBright(), "Zone window is steady when Ping Pong is off");
            setParameter (p, MphParams::pingPong, 1.0f);
            for (int i = 0; i < 15; ++i)
                panel->refreshNow();
            expect (! panel->zoneWindowBright(), "Ping Pong on: the Zone window dims after half a second");
            for (int i = 0; i < 15; ++i)
                panel->refreshNow();
            expect (panel->zoneWindowBright(), "Ping Pong on: ... and brightens again");
            setParameter (p, MphParams::pingPong, 0.0f);
            for (int i = 0; i < 20; ++i)
                panel->refreshNow();
            expect (panel->zoneWindowBright(), "Ping Pong off again: the Zone window is steady and bright");
        }
    }

    // ---------------- the two blinking lights, driven by the real processor ----------------
    {
        MimicPhonProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        resetAll (p);
        MimicPhonEditor editor (p);
        editor.setSize (1200, 800);
        auto* panel = editor.getStyledPanel();
        if (panel != nullptr)
        {
            // Zone 4, Rate in the middle: the delay is 653 ms and the clock (1/4 at the assumed 120 BPM) ticks every 500 ms
            bool tickSeen = false, tickOff = false, rateSeen = false, rateOff = false;
            processSeconds (p, 4.0, 1440, [&]       // 30 ms of audio between looks, about as often as the window looks
            {
                panel->refreshNow();
                tickSeen = tickSeen || panel->tickLightOn();
                tickOff  = tickOff  || ! panel->tickLightOn();
                rateSeen = rateSeen || panel->rateLightOn();
                rateOff  = rateOff  || ! panel->rateLightOn();
            });
            expect (tickSeen && tickOff, "the TICK light blinks (on and off) while audio runs at 120 BPM");
            expect (rateSeen && rateOff, "the RATE light blinks (on and off) while audio runs (Zone 4, 653 ms)");

            // Zone 0 at its shortest delay: the RATE light is lit almost all the time (the manual: it stays high)
            setParameter (p, MphParams::zone, 0.0f);
            setParameter (p, MphParams::rate, 0.0f);
            int lit = 0, looks = 0;
            processSeconds (p, 2.0, 1440, [&]
            {
                panel->refreshNow();
                ++looks;
                lit += panel->rateLightOn() ? 1 : 0;
            });
            expect (looks > 0 && lit * 10 >= looks * 9, "Zone 0 at the shortest delay: the RATE light stays lit");

            // Stopped audio: the lights go out
            for (int i = 0; i < 10; ++i)
                panel->refreshNow();
            expect (! panel->tickLightOn() && ! panel->rateLightOn(), "no audio running: both lights are off");
        }
    }

    // ---------------- Skew, Ping Pong / Swap: the read-outs say what the sound does ----------------
    {
        // Runs a click on both inputs through a real processor (Mix fully wet, one echo) and returns when the echo is heard on each side.
        auto echoTimes = [] (MimicPhonProcessor& p, double& left, double& right)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            float bestL = 0.0f, bestR = 0.0f;
            int indexL = -1, indexR = -1;
            for (int block = 0; block < 190; ++block)       // about 2 s
            {
                buffer.clear();
                if (block == 0)
                {
                    buffer.setSample (0, 100, 0.5f);
                    buffer.setSample (1, 100, 0.5f);
                }
                p.processBlock (buffer, midi);
                for (int i = 0; i < 512; ++i)
                {
                    const float a = std::abs (buffer.getSample (0, i)), b = std::abs (buffer.getSample (1, i));
                    if (a > bestL) { bestL = a; indexL = block * 512 + i; }
                    if (b > bestR) { bestR = b; indexR = block * 512 + i; }
                }
            }
            left = bestL > 0.01f ? (indexL - 100) / 48000.0 : -1.0;
            right = bestR > 0.01f ? (indexR - 100) / 48000.0 : -1.0;
        };

        // "431 ms" or "1.31 s" back into seconds
        auto seconds = [] (const juce::String& text)
        {
            return text.endsWith (" ms") ? text.upToFirstOccurrenceOf (" ", false, false).getDoubleValue() / 1000.0
                                         : text.upToFirstOccurrenceOf (" ", false, false).getDoubleValue();
        };

        auto fresh = [] (MimicPhonProcessor& p)
        {
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            resetAll (p);
            setParameter (p, MphParams::mix, 1.0f);          // only the repeats are heard
            setParameter (p, MphParams::repeats, 0.0f);      // one echo
        };

        // Skew off: both sides read the same as before
        {
            MimicPhonProcessor p;
            fresh (p);
            MimicPhonEditor editor (p);
            editor.setSize (1200, 800);
            auto* panel = editor.getStyledPanel();
            if (panel != nullptr)
            {
                processSeconds (p, 0.1, 480);
                panel->refreshNow();
                auto r = panel->getReadout();
                expect (r.delayLeft == "653 ms" && r.delayRight == "653 ms" && r.rateText == "653 ms" && ! r.skewed,
                        "Skew off: L and R both read 653 ms, as before");
                expect (panel->pingButtonText() == "PING PONG / SWAP", "Ping Pong off: the button says PING PONG / SWAP");
            }
        }

        // Skew on at 12:00 (no split), then Rate clockwise: L faster, R slower; read-outs equal the measured echoes
        {
            MimicPhonProcessor p;
            fresh (p);
            setParameter (p, MphParams::skew, 1.0f);
            processSeconds (p, 0.1, 480);                    // Skew goes on with Rate at 12:00
            MimicPhonEditor editor (p);
            editor.setSize (1200, 800);
            auto* panel = editor.getStyledPanel();
            if (panel != nullptr)
            {
                panel->refreshNow();
                auto r = panel->getReadout();
                expect (r.delayLeft == "653 ms" && r.delayRight == "653 ms" && ! r.skewed, "Skew on at 12:00: no split, L and R both 653 ms");

                setParameter (p, MphParams::rate, 0.8f);
                processSeconds (p, 0.1, 480);
                panel->refreshNow();
                r = panel->getReadout();
                expect (r.skewed && r.rateText.startsWith ("L faster"), "Skew on, Rate clockwise: the Rate knob says L faster");
                expect (seconds (r.delayLeft) < seconds (r.delayRight), "Skew on, Rate clockwise: the read-out has L shorter than R");
                expect (r.delayLeft == "431 ms" && r.delayRight == "990 ms", "Skew on, Rate at 0.8 in Zone 4: L 431 ms, R 990 ms");
                expect (panel->pingButtonText() == "PING PONG / SWAP", "Skew on, Ping Pong off: the button still says PING PONG / SWAP");

                // the sound: a fresh run with the same settings, echo times heard on each side
                MimicPhonProcessor heard;
                fresh (heard);
                setParameter (heard, MphParams::skew, 1.0f);
                processSeconds (heard, 0.1, 480);                  // Skew goes on with Rate at 12:00 ...
                setParameter (heard, MphParams::rate, 0.8f);       // ... then Rate is turned
                processSeconds (heard, 1.5, 480);                  // and given time to settle (the glide)
                double left = -1, right = -1;
                echoTimes (heard, left, right);
                expect (std::abs (left - seconds (r.delayLeft)) < 0.003 && std::abs (right - seconds (r.delayRight)) < 0.003,
                        "Skew: the echo times heard on L and R match the read-out");
            }
        }

        // Ping Pong (not skewed): the read-out shows the hop (half the time) and the button says PING PONG; the first echo is half the time
        {
            MimicPhonProcessor p;
            fresh (p);
            setParameter (p, MphParams::pingPong, 1.0f);
            processSeconds (p, 0.1, 480);
            MimicPhonEditor editor (p);
            editor.setSize (1200, 800);
            auto* panel = editor.getStyledPanel();
            if (panel != nullptr)
            {
                panel->refreshNow();
                const auto r = panel->getReadout();
                expect (std::abs (seconds (r.delayLeft) - 0.3265) < 0.001 && r.delayLeft == r.delayRight, "Ping Pong: each hop reads half of 653 ms");
                expect (r.rateText == "653 ms", "Ping Pong: the Rate knob still reads the delay time it sets (653 ms)");
                expect (panel->pingButtonText() == "PING PONG", "Ping Pong on, not skewed: the button says PING PONG");
                expect (r.pingFlashing, "Ping Pong on: the Zone window flashes");
                MimicPhonProcessor heard;
                fresh (heard);
                setParameter (heard, MphParams::pingPong, 1.0f);
                double left = -1, right = -1;
                echoTimes (heard, left, right);
                expect (std::abs (left - 0.3265) < 0.003 && right < 0.0, "Ping Pong: the first echo is heard on the left after the hop (327 ms), the right is still silent");
            }
        }

        // Swap (skewed): the button says SWAP, the times are the Skew times (no hop)
        {
            MimicPhonProcessor p;
            fresh (p);
            setParameter (p, MphParams::skew, 1.0f);
            processSeconds (p, 0.1, 480);
            setParameter (p, MphParams::rate, 0.8f);
            setParameter (p, MphParams::pingPong, 1.0f);
            processSeconds (p, 0.1, 480);
            MimicPhonEditor editor (p);
            editor.setSize (1200, 800);
            auto* panel = editor.getStyledPanel();
            if (panel != nullptr)
            {
                panel->refreshNow();
                const auto r = panel->getReadout();
                expect (panel->pingButtonText() == "SWAP", "Skew on and the button on: it says SWAP");
                expect (r.delayLeft == "431 ms" && r.delayRight == "990 ms", "Swap: L and R read the Skew times (no hop)");
                expect (r.pingFlashing, "Swap: the Zone window flashes");
            }
        }

        // Skew off after turning Rate: the split stays in the read-out, and the Rate knob moves both sides
        {
            MimicPhonProcessor p;
            fresh (p);
            setParameter (p, MphParams::skew, 1.0f);
            processSeconds (p, 0.1, 480);
            setParameter (p, MphParams::rate, 0.7f);
            processSeconds (p, 0.1, 480);
            setParameter (p, MphParams::skew, 0.0f);
            processSeconds (p, 0.1, 480);
            MimicPhonEditor editor (p);
            editor.setSize (1200, 800);
            auto* panel = editor.getStyledPanel();
            if (panel != nullptr)
            {
                panel->refreshNow();
                const auto before = panel->getReadout();
                expect (before.skewed && seconds (before.delayLeft) < seconds (before.delayRight), "Skew off after turning Rate: the split is still in the read-out");
                setParameter (p, MphParams::rate, 0.6f);
                processSeconds (p, 0.1, 480);
                panel->refreshNow();
                const auto after = panel->getReadout();
                expect (seconds (after.delayLeft) < seconds (before.delayLeft) && seconds (after.delayRight) < seconds (before.delayRight),
                        "Skew off: Rate counter-clockwise shortens both sides");
                const double ratioBefore = seconds (before.delayRight) / seconds (before.delayLeft);
                const double ratioAfter = seconds (after.delayRight) / seconds (after.delayLeft);
                expect (std::abs (ratioBefore - ratioAfter) < 0.05, "Skew off: the split (the ratio between the sides) stays when Rate moves");
                // on and off once with Rate untouched clears it
                setParameter (p, MphParams::skew, 1.0f);
                processSeconds (p, 0.1, 480);
                setParameter (p, MphParams::skew, 0.0f);
                processSeconds (p, 0.1, 480);
                panel->refreshNow();
                const auto cleared = panel->getReadout();
                expect (! cleared.skewed && cleared.delayLeft == cleared.delayRight, "Skew on and off with Rate untouched: the split is cleared");
            }
        }

        // ---- uRate and its LFO (milestone 7): the read-outs say what the sound does ----
        {
            const juce::String plusMinus = juce::String::fromUTF8 ("\xc2\xb1");
            MimicPhonProcessor p;
            fresh (p);
            MimicPhonEditor editor (p);
            editor.setSize (1200, 800);
            auto* panel = editor.getStyledPanel();
            if (panel != nullptr)
            {
                std::vector<KnobCard*> knobCards;
                collect (*panel, knobCards);
                auto cardText = [&] (const char* id) -> juce::String
                {
                    for (auto* card : knobCards)
                        if (juce::String (card->getParameterId()) == id)
                            return card->getReadoutText();
                    return "(no such card)";
                };
                const auto* lfo = panel->getLfoCard();
                expect (lfo != nullptr, "the styled panel has the uRate LFO card");
                if (lfo != nullptr)
                {
                    // defaults: Zone 4, uRate in the middle, LFO depth at zero
                    expect (cardText (MphParams::uRate) == "0.0 ms", "defaults: uRate reads 0.0 ms in Zones 1-7");
                    expect (lfo->rateReadout() == "0.49 Hz" && lfo->depthReadout() == "off", "defaults: LFO Rate 0.49 Hz, Depth off");

                    setParameter (p, MphParams::uRate, 1.0f);
                    expect (cardText (MphParams::uRate) == "6.0 ms shorter", "uRate fully clockwise, Zone 4: 6.0 ms shorter");
                    setParameter (p, MphParams::uRate, 0.0f);
                    expect (cardText (MphParams::uRate) == "6.0 ms longer", "uRate fully counter-clockwise, Zone 4: 6.0 ms longer");
                    setParameter (p, MphParams::zone, 0.0f);
                    expect (cardText (MphParams::uRate) == "-12.0 st", "Zone 0: uRate fully counter-clockwise reads -12.0 st (pitch)");
                    setParameter (p, MphParams::uRate, 1.0f);
                    expect (cardText (MphParams::uRate) == "+12.0 st", "Zone 0: uRate fully clockwise reads +12.0 st");
                    setParameter (p, MphParams::uRate, 0.5f);
                    expect (cardText (MphParams::uRate) == "0.0 st", "Zone 0: uRate in the middle reads 0.0 st");
                    setParameter (p, MphParams::zone, 4.0f / 7.0f);

                    // LFO Depth: the peak swing that is heard, in the Zone's own unit
                    setParameter (p, MphParams::uRateLfoDepth, 1.0f);
                    expect (lfo->depthReadout() == plusMinus + "12 ms", "LFO Depth full, Zone 4, 0.49 Hz: +-12 ms");
                    setParameter (p, MphParams::uRateLfoDepth, 0.5f);
                    expect (lfo->depthReadout() == plusMinus + "3.0 ms", "LFO Depth half: +-3.0 ms (squared curve)");
                    setParameter (p, MphParams::uRateLfoDepth, 1.0f);
                    setParameter (p, MphParams::uRateLfoRate, 1.0f);
                    expect (lfo->rateReadout() == "20.0 Hz" && lfo->depthReadout() == plusMinus + "4.8 ms", "LFO at 20 Hz: the swing is held back (+-4.8 ms)");
                    setParameter (p, MphParams::zone, 0.0f);
                    setParameter (p, MphParams::uRateLfoDepth, 0.5f);
                    expect (lfo->depthReadout() == plusMinus + "3.0 st", "LFO Depth half in Zone 0: +-3.0 st");
                    setParameter (p, MphParams::zone, 4.0f / 7.0f);

                    // Sync: the Rate knob names a note division
                    setParameter (p, MphParams::uRateLfoSync, 1.0f);
                    setParameter (p, MphParams::uRateLfoRate, 0.35f);
                    expect (lfo->rateReadout() == "1/4", "LFO Sync: Rate at 0.35 reads 1/4");
                    setParameter (p, MphParams::uRateLfoRate, 1.0f);
                    expect (lfo->rateReadout() == "1/32", "LFO Sync: Rate fully right reads 1/32");
                    setParameter (p, MphParams::uRateLfoRate, 0.0f);
                    expect (lfo->rateReadout() == "2 bars", "LFO Sync: Rate fully left reads 2 bars");
                    setParameter (p, MphParams::uRateLfoSync, 0.0f);
                }
                resetAll (p);
                setParameter (p, MphParams::mix, 1.0f);
                setParameter (p, MphParams::repeats, 0.0f);
            }

            // The Delay box and the echoes heard: uRate's steady offset is in both (the LFO is not)
            for (int zoneIndex : { 4, 2, 0 })
            {
                MimicPhonProcessor q;
                fresh (q);
                setParameter (q, MphParams::zone, (float) zoneIndex / 7.0f);
                setParameter (q, MphParams::rate, zoneIndex == 0 ? 0.6f : 0.5f);
                setParameter (q, MphParams::uRate, 1.0f);
                processSeconds (q, 0.1, 480);
                MimicPhonEditor win (q);
                win.setSize (1200, 800);
                auto* pn = win.getStyledPanel();
                if (pn != nullptr)
                {
                    pn->refreshNow();
                    const auto r = pn->getReadout();
                    MimicPhonProcessor heard;
                    fresh (heard);
                    setParameter (heard, MphParams::zone, (float) zoneIndex / 7.0f);
                    setParameter (heard, MphParams::rate, zoneIndex == 0 ? 0.6f : 0.5f);
                    setParameter (heard, MphParams::uRate, 1.0f);
                    double left = -1, right = -1;
                    echoTimes (heard, left, right);
                    const double shown = seconds (r.delayLeft);
                    expect (std::abs (left - shown) < (zoneIndex == 0 ? 0.0004 : 0.003) && std::abs (right - shown) < (zoneIndex == 0 ? 0.0004 : 0.003),
                            zoneIndex == 0 ? "uRate in Zone 0: the echo heard matches the Delay read-out (pitch)"
                                           : "uRate in Zones 1-7: the echo heard matches the Delay read-out");
                }
            }

            // With Skew splitting the sides, Zones 1-7: the right side is opposite, in the read-out and in the sound
            {
                MimicPhonProcessor q;
                fresh (q);
                setParameter (q, MphParams::skew, 1.0f);
                processSeconds (q, 0.1, 480);
                setParameter (q, MphParams::rate, 0.8f);
                setParameter (q, MphParams::uRate, 1.0f);
                processSeconds (q, 1.5, 480);
                MimicPhonEditor win (q);
                win.setSize (1200, 800);
                auto* pn = win.getStyledPanel();
                if (pn != nullptr)
                {
                    pn->refreshNow();
                    const auto r = pn->getReadout();
                    MimicPhonProcessor heard;
                    fresh (heard);
                    setParameter (heard, MphParams::skew, 1.0f);
                    processSeconds (heard, 0.1, 480);
                    setParameter (heard, MphParams::rate, 0.8f);
                    setParameter (heard, MphParams::uRate, 1.0f);
                    processSeconds (heard, 1.5, 480);
                    double left = -1, right = -1;
                    echoTimes (heard, left, right);
                    expect (std::abs (left - seconds (r.delayLeft)) < 0.003 && std::abs (right - seconds (r.delayRight)) < 0.003,
                            "uRate with Skew: the echoes heard on L and R match the Delay read-out");
                    expect (r.delayLeft == "425 ms" && r.delayRight == "996 ms", "uRate fully clockwise with Skew at Rate 0.8 in Zone 4: L 425 ms (6 ms shorter), R 996 ms (6 ms longer)");
                }
            }
        }

        // ---- Halo (milestone 9) through the real processor: Filter-Verb (Repeats fully left, Mix fully right) ----
        {
            auto tailEnergy = [] (float halo, float color, double& peakLeftRight, bool& finite, int from = 800)
            {
                MimicPhonProcessor q;
                q.setPlayConfigDetails (2, 2, 48000.0, 512);
                q.prepareToPlay (48000.0, 512);
                resetAll (q);
                setParameter (q, MphParams::zone, 0.0f);          // the shortest delay: the echo is almost at once
                setParameter (q, MphParams::rate, 0.0f);
                setParameter (q, MphParams::mix, 1.0f);           // only the repeats
                setParameter (q, MphParams::repeats, 0.0f);       // one echo
                setParameter (q, MphParams::halo, halo);
                setParameter (q, MphParams::color, color);
                juce::AudioBuffer<float> buffer (2, 512);
                juce::MidiBuffer midi;
                double energy = 0.0;
                float peakL = 0.0f, peakR = 0.0f;
                finite = true;
                for (int block = 0; block < 190; ++block)         // about 2 s
                {
                    buffer.clear();
                    if (block == 0)
                    {
                        buffer.setSample (0, 100, 0.5f);
                        buffer.setSample (1, 100, 0.5f);
                    }
                    q.processBlock (buffer, midi);
                    for (int i = 0; i < 512; ++i)
                    {
                        const float a = buffer.getSample (0, i), b = buffer.getSample (1, i);
                        finite = finite && std::isfinite (a) && std::isfinite (b);
                        if (block * 512 + i > from)
                        {
                            energy += (double) a * a;
                            peakL = std::max (peakL, std::abs (a));
                            peakR = std::max (peakR, std::abs (b));
                        }
                    }
                }
                peakLeftRight = std::min (peakL, peakR);
                return energy;
            };
            double sidePeak = 0.0;
            bool finite = true;
            const double none = tailEnergy (0.0f, mph::kDefaultColor, sidePeak, finite);
            expect (none < 1.0e-12, "Halo fully left, Repeats fully left, Mix fully right: one echo and then silence");
            const double tight = tailEnergy (0.5f, mph::kDefaultColor, sidePeak, finite);
            expect (tight > 1.0e-4 && sidePeak > 1.0e-3 && finite, "Halo in the middle with Repeats fully left and Mix fully right (Filter-Verb): a tail on both sides");
            const double tightLate = tailEnergy (0.5f, mph::kDefaultColor, sidePeak, finite, 24000);
            const double wideLate = tailEnergy (1.0f, mph::kDefaultColor, sidePeak, finite, 24000);
            expect (wideLate > 10.0 * tightLate && finite, "Halo fully right is still ringing after half a second, much more than Halo in the middle");
            const double dark = tailEnergy (0.8f, 0.0f, sidePeak, finite);
            const double thin = tailEnergy (0.8f, 1.0f, sidePeak, finite);
            expect (dark > 1.0e-4 && thin > 1.0e-5 && finite, "Halo works with the darkest and the thinnest Color");
        }

        // ---- Flip (milestone 10): the Delay box shows the reversed chunk, and the echo is heard backwards ----
        {
            // A click at the start, Mix fully right, one echo: with Flip it comes back at twice the chunk (less twice the click's own position)
            {
                MimicPhonProcessor q;
                fresh (q);
                setParameter (q, MphParams::flip, 1.0f);
                processSeconds (q, 0.1, 480);
                MimicPhonEditor win (q);
                win.setSize (1200, 800);
                auto* pn = win.getStyledPanel();
                if (pn != nullptr)
                {
                    pn->refreshNow();
                    const auto r = pn->getReadout();
                    expect (r.flipOn && r.delayLeft == "653 ms" && r.delayRight == "653 ms", "Flip on: the Delay box reads the chunk, 653 ms, and knows Flip is on");
                    double left = -1, right = -1;
                    echoTimes (q, left, right);
                    // the click is at 0.1 s + 100 samples of the engine's own time, inside the first chunk: it is heard at twice (chunk - click time) after it
                    const double want = 2.0 * (seconds (r.delayLeft) - 0.1 - 100.0 / 48000.0);
                    expect (std::abs (left - want) < 0.003 && std::abs (right - want) < 0.003,
                            "Flip: the click is heard backwards: at twice the time from the click to the end of the chunk the Delay box shows");
                    setParameter (q, MphParams::flip, 0.0f);
                    pn->refreshNow();
                    expect (! pn->getReadout().flipOn, "Flip off: the read-out says so");
                }
            }

            // The box shows the chunk that is heard: uRate does not shorten it in Zones 1-7, the top of Zone 7 is held to 20.8 s,
            // Zone 0's uRate is a pitch (so it halves the chunk), Ping Pong's hop halves it
            {
                auto box = [] (const std::function<void (MimicPhonProcessor&)>& setup, bool flip)
                {
                    MimicPhonProcessor q;
                    q.setPlayConfigDetails (2, 2, 48000.0, 512);
                    q.prepareToPlay (48000.0, 512);
                    resetAll (q);
                    setup (q);
                    setParameter (q, MphParams::flip, flip ? 1.0f : 0.0f);
                    processSeconds (q, 0.1, 480);
                    MimicPhonEditor win (q);
                    win.setSize (1200, 800);
                    auto* pn = win.getStyledPanel();
                    if (pn == nullptr)
                        return juce::String ("no panel");
                    pn->refreshNow();
                    return pn->getReadout().delayLeft;
                };
                auto uRateFar = [] (MimicPhonProcessor& q) { setParameter (q, MphParams::uRate, 1.0f); };
                expect (box (uRateFar, false) == "647 ms" && box (uRateFar, true) == "653 ms", "Flip: uRate fully clockwise shortens the forward delay by 6 ms (647 ms) but not the chunk (653 ms)");
                auto zone7 = [] (MimicPhonProcessor& q) { setParameter (q, MphParams::zone, 1.0f); setParameter (q, MphParams::rate, 1.0f); };
                expect (box (zone7, false) == "41.8 s" && box (zone7, true) == "20.8 s", "Flip in Zone 7 with Rate fully right: the box reads 20.8 s (the chunk is held to what fits), not 41.8 s");
                auto zone0 = [] (MimicPhonProcessor& q) { setParameter (q, MphParams::zone, 0.0f); setParameter (q, MphParams::uRate, 1.0f); };
                expect (box (zone0, true) == box (zone0, false) && seconds (box (zone0, true)) > 0.0,
                        "Flip in Zone 0: uRate is a pitch there, so the box follows it and is the same figure as without Flip");
                auto ping = [] (MimicPhonProcessor& q) { setParameter (q, MphParams::pingPong, 1.0f); };
                expect (std::abs (seconds (box (ping, true)) - 0.3265) < 0.001, "Flip with Ping Pong: the box reads the hop, half of 653 ms");
            }
        }

        // ---- Hold (milestone 11): the Delay box shows the loop, Repeats reads the loop's start point ----
        {
            MimicPhonProcessor q;
            fresh (q);
            setParameter (q, MphParams::repeats, 0.0f);
            setParameter (q, MphParams::hold, 1.0f);
            processSeconds (q, 0.1, 480);
            MimicPhonEditor win (q);
            win.setSize (1200, 800);
            auto* pn = win.getStyledPanel();
            if (pn != nullptr)
            {
                pn->refreshNow();
                auto r = pn->getReadout();
                expect (r.holdOn && r.delayLeft == "653 ms" && r.delayRight == "653 ms", "Hold: the Delay box shows the loop length, 653 ms (the delay time it was)");
                std::vector<KnobCard*> cards;
                collect (*pn, cards);
                auto text = [&] (const char* id) -> juce::String
                {
                    for (auto* c : cards)
                        if (juce::String (c->getParameterId()) == id)
                            return c->getReadoutText();
                    return "(no such card)";
                };
                expect (text (MphParams::repeats) == "newest", "Hold with Repeats fully left: Repeats reads \"newest\"");
                setParameter (q, MphParams::repeats, 0.5f);
                processSeconds (q, 0.1, 480);
                pn->refreshNow();
                const auto t = text (MphParams::repeats);
                expect (t.startsWith ("start "), "Hold with Repeats at the middle: Repeats reads a start point (\"start ...\")");
                setParameter (q, MphParams::hold, 0.0f);
                processSeconds (q, 0.1, 480);
                pn->refreshNow();
                expect (! pn->getReadout().holdOn && ! text (MphParams::repeats).startsWith ("start ") && text (MphParams::repeats) != "newest", "Hold off: Repeats reads the feedback figure again, not a start point");
            }
        }

        // Hold through the real processor: the input is ignored, the loop keeps sounding
        {
            MimicPhonProcessor q;
            fresh (q);
            setParameter (q, MphParams::repeats, 0.0f);
            q.setPlayConfigDetails (2, 2, 48000.0, 480);
            juce::AudioBuffer<float> buf (2, 480);
            juce::MidiBuffer midi;
            auto run = [&] (double sec, float amp)
            {
                double e = 0.0;
                for (int b = 0; b < (int) (sec * 100); ++b)
                {
                    for (int i = 0; i < 480; ++i)
                        for (int c = 0; c < 2; ++c)
                            buf.setSample (c, i, amp * std::sin (0.05f * (float) (b * 480 + i)));
                    q.processBlock (buf, midi);
                    for (int i = 0; i < 480; ++i)
                        e += std::fabs (buf.getSample (0, i));
                }
                return e;
            };
            run (1.0, 0.5f);
            setParameter (q, MphParams::hold, 1.0f);
            run (0.1, 0.0f);
            const double loud = run (2.0, 0.0f);
            expect (loud > 1.0, "Hold: the loop keeps sounding with silence at the input");
            const double ignored = run (1.0, 0.8f);
            expect (ignored > 0.5, "Hold: new input does not break the loop");
        }

        // ---- Tempo Sync (milestone 8): the echo heard is the time the window shows ----
        {
            // Default Zone 4, Rate 1/1, 120 BPM assumed (no playhead here): 500 ms
            {
                MimicPhonProcessor q;
                fresh (q);
                setParameter (q, MphParams::tempoSync, 1.0f);
                processSeconds (q, 0.1, 480);
                MimicPhonEditor win (q);
                win.setSize (1200, 800);
                auto* pn = win.getStyledPanel();
                if (pn != nullptr)
                {
                    pn->refreshNow();
                    const auto r = pn->getReadout();
                    double left = -1, right = -1;
                    echoTimes (q, left, right);
                    expect (std::abs (left - 0.5) < 0.003 && std::abs (right - 0.5) < 0.003, "Tempo Sync: the echo heard is 500 ms");
                    expect (std::abs (left - seconds (r.delayLeft)) < 0.003, "Tempo Sync: the echo heard matches the Delay read-out");
                }
            }

            // Each of the 13 steps: the echo heard matches the Rate card's step and the delay read-out
            {
                bool allMatch = true;
                for (int step = 0; step < 13; ++step)
                {
                    MimicPhonProcessor q;
                    fresh (q);
                    setParameter (q, MphParams::tempoSync, 1.0f);
                    setParameter (q, MphParams::rate, ((float) step + 0.5f) / 13.0f);
                    processSeconds (q, 0.1, 480);
                    MimicPhonEditor win (q);
                    win.setSize (1200, 800);
                    auto* pn = win.getStyledPanel();
                    if (pn == nullptr)
                        continue;
                    pn->refreshNow();
                    const auto r = pn->getReadout();
                    double left = -1, right = -1;
                    echoTimes (q, left, right);
                    allMatch = allMatch && r.rateText.startsWith (mph::core::kSyncNames[step])
                               && std::abs (left - seconds (r.delayLeft)) < 0.003 && std::abs (right - seconds (r.delayRight)) < 0.003;
                }
                expect (allMatch, "Tempo Sync: all 13 steps, the echo heard matches the Rate card's step and the delay read-out");
            }

            // With Skew the sides sit on different steps: the card names both, and the echoes heard match
            {
                MimicPhonProcessor q;
                fresh (q);
                setParameter (q, MphParams::tempoSync, 1.0f);
                setParameter (q, MphParams::skew, 1.0f);
                processSeconds (q, 0.1, 480);
                setParameter (q, MphParams::rate, 0.8f);
                processSeconds (q, 0.5, 480);
                MimicPhonEditor win (q);
                win.setSize (1200, 800);
                auto* pn = win.getStyledPanel();
                if (pn != nullptr)
                {
                    pn->refreshNow();
                    const auto r = pn->getReadout();
                    double left = -1, right = -1;
                    echoTimes (q, left, right);
                    expect (r.rateText == "L x1 2/3  R /1 2/3", "Tempo Sync with Skew: the Rate card names both sides' steps");
                    expect (r.delayLeft == "300 ms" && r.delayRight == "833 ms", "Tempo Sync with Skew: L 300 ms, R 833 ms");
                    expect (std::abs (left - 0.3) < 0.003 && std::abs (right - 0.8333) < 0.003, "Tempo Sync with Skew: the echoes heard match (300 and 833 ms)");
                }
            }
        }

        // The plain window carries the same buttons and controls (nothing was added or removed)
        expect (proc.getParameters().size() == 21, "still 21 parameters: no control id was added or removed");
    }

    // ---------------- a picture with uRate and the LFO working in Zone 0 (its own plugin object) ----------------
    {
        MimicPhonProcessor lfoProc;
        lfoProc.setPlayConfigDetails (2, 2, 48000.0, 512);
        lfoProc.prepareToPlay (48000.0, 512);
        resetAll (lfoProc);
        MimicPhonEditor lfoEditor (lfoProc);
        lfoEditor.showView (MimicPhonEditor::View::styled);
        lfoEditor.setSize (1200, 800);
        setParameter (lfoProc, MphParams::zone, 1.0f / 7.0f);
        setParameter (lfoProc, MphParams::skew, 1.0f);
        processSeconds (lfoProc, 0.1, 480);
        setParameter (lfoProc, MphParams::rate, 0.62f);
        setParameter (lfoProc, MphParams::uRate, 0.68f);
        setParameter (lfoProc, MphParams::uRateLfoRate, 0.4f);
        setParameter (lfoProc, MphParams::uRateLfoDepth, 0.6f);
        processSeconds (lfoProc, 0.3, 480);
        letWindowThreadRun (150);
        save (lfoEditor, outDir.getChildFile ("styled_panel_urate.png"), 1.0f);
    }

    // ---------------- a picture with Tempo Sync on and Skew putting the two sides on different steps ----------------
    {
        MimicPhonProcessor syncProc;
        syncProc.setPlayConfigDetails (2, 2, 48000.0, 512);
        syncProc.prepareToPlay (48000.0, 512);
        resetAll (syncProc);
        MimicPhonEditor syncEditor (syncProc);
        syncEditor.showView (MimicPhonEditor::View::styled);
        syncEditor.setSize (1200, 800);
        setParameter (syncProc, MphParams::tempoSync, 1.0f);
        setParameter (syncProc, MphParams::skew, 1.0f);
        processSeconds (syncProc, 0.1, 480);
        setParameter (syncProc, MphParams::rate, 0.8f);
        processSeconds (syncProc, 0.3, 480);
        letWindowThreadRun (150);
        save (syncEditor, outDir.getChildFile ("styled_panel_sync.png"), 1.0f);
    }

    // ---------------- a picture with Skew pulling L and R apart and Swap on (its own plugin object: the audio has to run for the engine to know) ----------------
    {
        MimicPhonProcessor swapProc;
        swapProc.setPlayConfigDetails (2, 2, 48000.0, 512);
        swapProc.prepareToPlay (48000.0, 512);
        resetAll (swapProc);
        MimicPhonEditor swapEditor (swapProc);
        swapEditor.showView (MimicPhonEditor::View::styled);
        swapEditor.setSize (1200, 800);
        setParameter (swapProc, MphParams::skew, 1.0f);
        processSeconds (swapProc, 0.1, 480);                  // Skew goes on with Rate at 12:00
        setParameter (swapProc, MphParams::rate, 0.8f);
        setParameter (swapProc, MphParams::pingPong, 1.0f);
        processSeconds (swapProc, 0.3, 480);
        letWindowThreadRun (150);
        save (swapEditor, outDir.getChildFile ("styled_panel_swap.png"), 1.0f);
    }

    // ---------------- a picture with Halo in the middle-to-wide range, Repeats up (its own plugin object) ----------------
    {
        MimicPhonProcessor haloProc;
        haloProc.setPlayConfigDetails (2, 2, 48000.0, 512);
        haloProc.prepareToPlay (48000.0, 512);
        resetAll (haloProc);
        MimicPhonEditor haloEditor (haloProc);
        haloEditor.showView (MimicPhonEditor::View::styled);
        haloEditor.setSize (1200, 800);
        setParameter (haloProc, MphParams::halo, 0.62f);
        setParameter (haloProc, MphParams::repeats, 0.5f);
        setParameter (haloProc, MphParams::color, 0.6f);
        processSeconds (haloProc, 0.3, 480);
        letWindowThreadRun (150);
        save (haloEditor, outDir.getChildFile ("styled_panel_halo.png"), 1.0f);
    }

    // ---------------- a picture with Hold on: the Delay box says HOLD and the Repeats card reads its start point ----------------
    {
        MimicPhonProcessor holdProc;
        holdProc.setPlayConfigDetails (2, 2, 48000.0, 512);
        holdProc.prepareToPlay (48000.0, 512);
        resetAll (holdProc);
        MimicPhonEditor holdEditor (holdProc);
        holdEditor.showView (MimicPhonEditor::View::styled);
        holdEditor.setSize (1200, 800);
        setParameter (holdProc, MphParams::hold, 1.0f);
        setParameter (holdProc, MphParams::zone, 3.0f / 7.0f);
        setParameter (holdProc, MphParams::repeats, 0.4f);
        processSeconds (holdProc, 0.3, 480);
        letWindowThreadRun (150);
        save (holdEditor, outDir.getChildFile ("styled_panel_hold.png"), 1.0f);
    }

    // ---------------- a picture with Flip on (its own plugin object): the Delay box says FLIP and shows the chunk ----------------
    {
        MimicPhonProcessor flipProc;
        flipProc.setPlayConfigDetails (2, 2, 48000.0, 512);
        flipProc.prepareToPlay (48000.0, 512);
        resetAll (flipProc);
        MimicPhonEditor flipEditor (flipProc);
        flipEditor.showView (MimicPhonEditor::View::styled);
        flipEditor.setSize (1200, 800);
        setParameter (flipProc, MphParams::flip, 1.0f);
        setParameter (flipProc, MphParams::zone, 3.0f / 7.0f);
        setParameter (flipProc, MphParams::repeats, 0.55f);
        setParameter (flipProc, MphParams::halo, 0.3f);
        setParameter (flipProc, MphParams::uRate, 0.8f);
        processSeconds (flipProc, 0.3, 480);
        letWindowThreadRun (150);
        save (flipEditor, outDir.getChildFile ("styled_panel_flip.png"), 1.0f);
    }

    // ---------------- the pictures ----------------
    {
        resetAll (proc);
        MimicPhonEditor editor (proc);
        editor.showView (MimicPhonEditor::View::styled);      // this plugin object has been remembering "plain" from the checks above
        expect (editor.getView() == MimicPhonEditor::View::styled && editor.getStyledPanel() != nullptr, "the picture is of the styled panel");
        editor.setSize (1200, 800);
        letWindowThreadRun (150);
        save (editor, outDir.getChildFile ("styled_panel.png"), 1.0f);

        // A busier face for looking at: Zone 0 (green) with the sync read-out, Ping Pong flashing, knobs moved
        setParameter (proc, MphParams::zone, 0.0f);
        setParameter (proc, MphParams::tempoSync, 1.0f);
        setParameter (proc, MphParams::skew, 1.0f);
        setParameter (proc, MphParams::inputGain, 2.0f / 3.0f);
        setParameter (proc, MphParams::repeats, 0.62f);
        setParameter (proc, MphParams::halo, 0.35f);
        setParameter (proc, MphParams::uRateLfoDepth, 0.4f);
        setParameter (proc, MphParams::uRateLfoShape, 1.0f);
        letWindowThreadRun (150);
        save (editor, outDir.getChildFile ("styled_panel_zone0.png"), 1.0f);

        setParameter (proc, MphParams::zone, 1.0f);
        setParameter (proc, MphParams::tempoSync, 0.0f);
        setParameter (proc, MphParams::uRateLfoSync, 1.0f);
        letWindowThreadRun (150);
        save (editor, outDir.getChildFile ("styled_panel_zone7.png"), 1.0f);

        // At the smallest and the largest window size
        editor.setSize (840, 560);
        letWindowThreadRun (100);
        save (editor, outDir.getChildFile ("styled_panel_smallest.png"), 1.0f);
        editor.setSize (1800, 1200);
        letWindowThreadRun (100);
        save (editor, outDir.getChildFile ("styled_panel_largest.png"), 1.0f);

        editor.showView (MimicPhonEditor::View::plain);
        editor.setSize (1200, 800);
        letWindowThreadRun (150);
        save (editor, outDir.getChildFile ("plain_sliders.png"), 1.0f);
        resetAll (proc);
    }

    // ---------------- audio through the real processor ----------------
    {
        // Sets every control to a value, then puts the three that decide the live signal's level back (Input Level, Input Gain, Output
        // Level) and Mix fully left, so what comes out can be compared with what went in, bit for bit.
        auto everythingTo = [&] (MimicPhonProcessor& target, float value)
        {
            for (auto* param : target.getParameters())
                param->setValueNotifyingHost (value);
            for (const char* id : { MphParams::inputLevel, MphParams::inputGain, MphParams::outputLevel })
                if (auto* param = target.getValueTreeState().getParameter (id))
                    param->setValueNotifyingHost (param->getDefaultValue());
            setParameter (target, MphParams::mix, 0.0f);
        };

        resetAll (proc);
        setParameter (proc, MphParams::mix, 0.0f);
        const int blockSizes[] = { 1, 7, 32, 64, 100, 512, 1024, 4096 };

        for (int block : blockSizes)
            expect (passesThroughUnchanged (proc, 2, block, 20), "stereo: Mix fully left, audio unchanged, defaults, block " + juce::String (block));

        // Every other control moved to an extreme: the live signal must still come out exactly as it went in.
        everythingTo (proc, 1.0f);
        for (int block : blockSizes)
            expect (passesThroughUnchanged (proc, 2, block, 20), "stereo: Mix fully left, audio unchanged, every other control at maximum, block " + juce::String (block));
        everythingTo (proc, 0.0f);
        for (int block : blockSizes)
            expect (passesThroughUnchanged (proc, 2, block, 20), "stereo: Mix fully left, audio unchanged, every other control at minimum, block " + juce::String (block));
        resetAll (proc);

        // Mono, and other sample rates
        MimicPhonProcessor mono;
        mono.setPlayConfigDetails (1, 1, 44100.0, 256);
        mono.prepareToPlay (44100.0, 256);
        setParameter (mono, MphParams::mix, 0.0f);
        expect (passesThroughUnchanged (mono, 1, 256, 40), "mono at 44.1 kHz: Mix fully left, audio unchanged");

        MimicPhonProcessor wide;
        wide.setPlayConfigDetails (2, 2, 88200.0, 1024);
        wide.prepareToPlay (88200.0, 1024);
        setParameter (wide, MphParams::mix, 0.0f);
        expect (passesThroughUnchanged (wide, 2, 1024, 40), "stereo at 88.2 kHz: Mix fully left, audio unchanged");

        // The repeats, through the real processor: with Mix fully right and Repeats fully left, an impulse comes back once, at the
        // delay time the window shows (all eight Zones, Rate in the middle)
        for (int zone = 0; zone < 8; ++zone)
        {
            MimicPhonProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            resetAll (q);
            setParameter (q, MphParams::mix, 1.0f);
            setParameter (q, MphParams::repeats, 0.0f);
            setParameter (q, MphParams::zone, (float) zone / 7.0f);
            setParameter (q, MphParams::rate, 0.5f);

            const double delaySamples = mph::core::freeDelaySeconds (zone, 0.5f) * 48000.0;
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            std::vector<float> left;
            const int totalBlocks = (int) ((delaySamples + 3000.0) / 512.0) + 1;
            for (int b = 0; b < totalBlocks; ++b)
            {
                buffer.clear();
                if (b == 0)
                    buffer.setSample (0, 0, 1.0f);
                q.processBlock (buffer, midi);
                for (int i = 0; i < 512; ++i)
                    left.push_back (buffer.getSample (0, i));
            }
            int peak = 20;
            for (int i = 20; i < (int) left.size(); ++i)
                if (std::abs (left[(size_t) i]) > std::abs (left[(size_t) peak]))
                    peak = i;
            expect (std::abs ((double) peak - delaySamples) <= 1.0 && std::abs (left[(size_t) peak]) > 0.5f,
                    "Zone " + juce::String (zone) + ": the echo through the plugin falls at the delay time of the window (" + juce::String (delaySamples / 48.0, 1) + " ms)");
            expect (std::abs (q.getEngine().getDelaySeconds() - (float) mph::core::freeDelaySeconds (zone, 0.5f)) < 1.0e-4f,
                    "Zone " + juce::String (zone) + ": the engine's delay time is the one the window works out");
        }

        // Every control at an extreme, with Mix and the levels up too: always real numbers, never runaway
        {
            MimicPhonProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            for (auto* param : q.getParameters())
                param->setValueNotifyingHost (1.0f);
            setParameter (q, MphParams::clear, 0.0f);
            setParameter (q, MphParams::hold, 0.0f);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (1234);
            bool sane = true;
            float biggest = 0.0f;
            for (int b = 0; b < 400; ++b)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (c, i, random.nextFloat() * 2.0f - 1.0f);
                q.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = buffer.getSample (c, i);
                        sane = sane && std::isfinite (v);
                        biggest = std::max (biggest, std::abs (v));
                    }
            }
            expect (sane && biggest < 20.0f, "every control at maximum with loud noise: real numbers, no runaway (largest sample " + juce::String (biggest, 2) + ")");

            for (auto* param : q.getParameters())
                param->setValueNotifyingHost (0.0f);
            bool silent = true;
            for (int b = 0; b < 40; ++b)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (c, i, random.nextFloat() * 2.0f - 1.0f);
                q.processBlock (buffer, midi);
                if (b >= 20)                                  // the level knobs glide for a tenth of a second; after that it is exactly silent
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i)
                            silent = silent && buffer.getSample (c, i) == 0.0f;
            }
            expect (silent, "every control at minimum (Output Level at 0): silence");
        }

        // The tail told to the host
        {
            MimicPhonProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            resetAll (q);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            buffer.clear();
            q.processBlock (buffer, midi);
            expect (std::abs (q.getTailLengthSeconds() - 8.0 * 0.653) < 0.05, "starting positions: the tail told to Logic is about 5.2 s (8 echoes of 653 ms)");
            setParameter (q, MphParams::repeats, 0.0f);
            buffer.clear();
            q.processBlock (buffer, midi);
            expect (std::abs (q.getTailLengthSeconds() - 0.653) < 0.01, "Repeats fully left: the tail is one echo (653 ms)");
            setParameter (q, MphParams::repeats, 0.83f);
            buffer.clear();
            q.processBlock (buffer, midi);
            expect (q.getTailLengthSeconds() >= 100.0 && q.getTailLengthSeconds() <= 120.0, "Repeats at infinite: a long tail (the cap)");
        }

        // Clear, through the real processor: the repeats are gone at once
        {
            MimicPhonProcessor q;
            q.setPlayConfigDetails (2, 2, 48000.0, 512);
            q.prepareToPlay (48000.0, 512);
            resetAll (q);
            setParameter (q, MphParams::mix, 1.0f);
            setParameter (q, MphParams::zone, 2.0f / 7.0f);       // Zone 2, 163 ms with Rate in the middle
            setParameter (q, MphParams::repeats, 0.7f);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (42);
            float soundingBefore = 0.0f;
            for (int b = 0; b < 200; ++b)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (c, i, (random.nextFloat() * 2.0f - 1.0f) * 0.5f);
                q.processBlock (buffer, midi);
                if (b >= 150)
                    for (int i = 0; i < 512; ++i)
                        soundingBefore = std::max (soundingBefore, std::abs (buffer.getSample (0, i)));
            }
            expect (soundingBefore > 0.05f, "the repeats are sounding before Clear");

            setParameter (q, MphParams::clear, 1.0f);
            float after = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                buffer.clear();
                q.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i)
                        after = std::max (after, std::abs (buffer.getSample (c, i)));
            }
            expect (after == 0.0f, "Clear: the repeats are gone in the very block it goes on, and stay gone in silence");
        }

        // Layouts: mono and stereo are accepted; a mismatch or more channels is refused
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::mono());
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        expect (proc.checkBusesLayoutSupported (layout), "mono in / mono out is accepted");
        layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        expect (! proc.checkBusesLayoutSupported (layout), "mono in / stereo out is refused");
        layout.inputBuses.set (0, juce::AudioChannelSet::quadraphonic());
        layout.outputBuses.set (0, juce::AudioChannelSet::quadraphonic());
        expect (! proc.checkBusesLayoutSupported (layout), "four channels are refused");
    }

    // ---------------- the Clear button ----------------
    {
        resetAll (proc);
        setParameter (proc, MphParams::clear, 1.0f);
        processSeconds (proc, 0.02, 512);                 // one block: the engine notices Clear
        letWindowThreadRun (250);
        expect (parameterValue (proc, MphParams::clear) < 0.5f, "the Clear button switches itself off");

        // A project saved while Clear was on opens with it off
        setParameter (proc, MphParams::clear, 1.0f);
        juce::MemoryBlock whileOn;
        proc.getStateInformation (whileOn);
        setParameter (proc, MphParams::clear, 0.0f);
        MimicPhonProcessor reopened;
        reopened.setPlayConfigDetails (2, 2, 48000.0, 512);
        reopened.prepareToPlay (48000.0, 512);
        reopened.setStateInformation (whileOn.getData(), (int) whileOn.getSize());
        letWindowThreadRun (250);
        expect (parameterValue (reopened, MphParams::clear) < 0.5f, "a project saved with Clear on opens with Clear off");
        resetAll (proc);
    }

    // ---------------- opening and closing the window while audio runs, at different sizes ----------------
    {
        resetAll (proc);
        setParameter (proc, MphParams::mix, 0.0f);        // live signal only, so it can be compared bit for bit
        {
            // Mix glides for a tenth of a second: let it arrive before comparing
            juce::AudioBuffer<float> quiet (2, 512);
            juce::MidiBuffer midi;
            for (int b = 0; b < 60; ++b)
            {
                quiet.clear();
                proc.processBlock (quiet, midi);
            }
        }
        bool stillSame = true;
        const int widths[] = { 840, 1080, 1200, 1800 };
        for (int round = 0; round < 80; ++round)
        {
            const int w = widths[round % 4];
            MimicPhonEditor editor (proc);
            editor.setSize (w, w * 2 / 3);
            stillSame = stillSame && passesThroughUnchanged (proc, 2, 512, 2);
            letWindowThreadRun (5);
        }
        expect (stillSame, "Mix fully left: audio unchanged while the window is opened and closed 80 times at four sizes");

        // The window keeps its 3:2 shape
        MimicPhonEditor editor (proc);
        editor.setSize (1080, 720);
        expect (editor.getWidth() * 2 == editor.getHeight() * 3, "the window opens at 3:2");
    }

    std::cout << "preview checks: " << checks << " checks, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
