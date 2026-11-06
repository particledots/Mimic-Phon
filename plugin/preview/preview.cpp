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
//   5. drives the real processor with audio the way Logic does and checks the audio comes out unchanged, bit for bit
//      (mono and stereo, several block sizes), with every control moved;
//   6. checks saving and reloading, the Clear button switching itself off, and opening and closing the window while audio runs.
//
// Usage: MimicPhonPreview <folder for the pictures>

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
                same = same && std::memcmp (buffer.getReadPointer (c), original.getReadPointer (c), sizeof (float) * (size_t) blockSize) == 0;
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

            // Tempo Sync: the Rate knob names the step and the clock; the tempo from Logic is shown
            resetAll (p);
            setParameter (p, MphParams::tempoSync, 1.0f);
            panel->refreshNow();
            r = panel->getReadout();
            expect (r.synced && r.rateText == "1/1 (tempo 1/4)", "Tempo Sync: the Rate knob in the middle reads \"1/1 (tempo 1/4)\"");
            expect (r.delayLeft == "500 ms", "Tempo Sync at 120 BPM, 1/4 clock, Zone 4, 1/1: 500 ms");
            setParameter (p, MphParams::rate, 0.0f);
            expect (panel->getReadout().rateText == "x2 (tempo 1/4)", "Tempo Sync: Rate fully left reads x2");
            setParameter (p, MphParams::rate, 1.0f);
            expect (panel->getReadout().rateText == "/2 (tempo 1/4)", "Tempo Sync: Rate fully right reads /2");
            setParameter (p, MphParams::clockDivision, 1.0f / 9.0f);        // index 1 = "1 bar"
            setParameter (p, MphParams::rate, 0.5f);
            expect (panel->getReadout().rateText == "1/1 (tempo 1 bar)", "Tempo Sync: the Clock Division menu is named in the read-out");

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
        resetAll (proc);
        const int blockSizes[] = { 1, 7, 32, 64, 100, 512, 1024, 4096 };

        for (int block : blockSizes)
            expect (passesThroughUnchanged (proc, 2, block, 20), "stereo: audio unchanged, defaults, block " + juce::String (block));

        // Every control moved to an extreme: the audio must still come out exactly as it went in.
        for (auto* param : proc.getParameters())
            param->setValueNotifyingHost (1.0f);
        for (int block : blockSizes)
            expect (passesThroughUnchanged (proc, 2, block, 20), "stereo: audio unchanged, every control at maximum, block " + juce::String (block));
        for (auto* param : proc.getParameters())
            param->setValueNotifyingHost (0.0f);
        for (int block : blockSizes)
            expect (passesThroughUnchanged (proc, 2, block, 20), "stereo: audio unchanged, every control at minimum, block " + juce::String (block));
        resetAll (proc);

        // Mono, and other sample rates
        MimicPhonProcessor mono;
        mono.setPlayConfigDetails (1, 1, 44100.0, 256);
        mono.prepareToPlay (44100.0, 256);
        expect (passesThroughUnchanged (mono, 1, 256, 40), "mono at 44.1 kHz: audio unchanged");

        MimicPhonProcessor wide;
        wide.setPlayConfigDetails (2, 2, 88200.0, 1024);
        wide.prepareToPlay (88200.0, 1024);
        expect (passesThroughUnchanged (wide, 2, 1024, 40), "stereo at 88.2 kHz: audio unchanged");

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
        passesThroughUnchanged (proc, 2, 512, 2);
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
        expect (stillSame, "audio unchanged while the window is opened and closed 80 times at four sizes");

        // The window keeps its 3:2 shape
        MimicPhonEditor editor (proc);
        editor.setSize (1080, 720);
        expect (editor.getWidth() * 2 == editor.getHeight() * 3, "the window opens at 3:2");
    }

    std::cout << "preview checks: " << checks << " checks, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
