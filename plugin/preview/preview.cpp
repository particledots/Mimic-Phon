// Offline preview and control checks (not part of the plugin itself; only built with -DMPH_BUILD_PREVIEW=ON).
//
// It does these things without needing Logic:
//   1. draws the window to a picture file;
//   2. checks that the 21 control ids are exactly the frozen list, that every control changes exactly one parameter,
//      that every parameter has exactly one control, and that host automation moves the controls;
//   3. drives the real processor with audio the way Logic does and checks the audio comes out unchanged, bit for bit
//      (mono and stereo, several block sizes), with every control moved;
//   4. checks saving and reloading, the Clear button switching itself off, and opening and closing the window while audio runs.
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

    // Moves every control in the window and checks the result.
    void checkControls (MimicPhonProcessor& proc, MimicPhonEditor& editor)
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
        expect (ids.size() == controlled.size(), "no parameter has two controls");
        expect (controlled == allIds, "every parameter has a control");

        std::set<juce::String> moved;
        auto moveAndCheck = [&] (const juce::String& what, const std::function<void()>& action)
        {
            const auto before = snapshotParams (proc);
            action();
            const auto changed = changedBetween (before, snapshotParams (proc));
            expect (changed.size() == 1, what + " changes exactly one parameter (changed " + juce::String ((int) changed.size()) + ")");
            if (changed.size() == 1)
            {
                expect (moved.insert (changed[0]).second, what + " is the only control for " + changed[0]);
                std::cout << "  " << what << " -> " << changed[0] << std::endl;
            }
        };

        std::vector<juce::Slider*> sliders;
        collect (editor, sliders);
        expect (sliders.size() == 10, "10 knobs / sliders (found " + juce::String ((int) sliders.size()) + ")");
        for (auto* s : sliders)
            moveAndCheck ("slider '" + s->getTitle() + "'", [s] { s->setValue (0.9, juce::sendNotificationSync); });

        std::vector<juce::ComboBox*> boxes;
        collect (editor, boxes);
        expect (boxes.size() == 4, "4 menus (found " + juce::String ((int) boxes.size()) + ")");
        for (auto* b : boxes)
            moveAndCheck ("menu '" + b->getTitle() + "'", [b]
            {
                b->setSelectedItemIndex (b->getSelectedItemIndex() == 1 ? 2 : 1, juce::sendNotificationSync);
            });

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
        expect (toggles == 7, "7 on/off buttons (found " + juce::String (toggles) + ")");

        expect (moved == allIds, "moving the controls reached all 21 parameters");

        // And the other way: a parameter change shows up in the control (host automation -> window)
        if (! sliders.empty())
        {
            resetAll (proc);
            proc.getParameters()[0]->setValueNotifyingHost (0.25f);
            letWindowThreadRun (50);
            expect (std::abs (sliders.front()->getValue() - 0.25) < 1.0e-3, "host automation moves the first knob");
        }

        resetAll (proc);
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

    // ---------------- control checks ----------------
    {
        MimicPhonEditor editor (proc);
        editor.setSize (1080, 720);
        checkControls (proc, editor);

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

    // ---------------- the picture ----------------
    {
        resetAll (proc);
        MimicPhonEditor editor (proc);
        editor.setSize (1200, 800);
        letWindowThreadRun (150);
        save (editor, outDir.getChildFile ("plain_sliders.png"), 1.0f);
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
