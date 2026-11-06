// The plugin wrapper. It talks to Logic, reads the control values and passes them, with the audio and Logic's tempo,
// to the engine. It contains no sound-making code. It tells Logic the tail length (how long the repeats keep sounding).
//
// One housekeeping job: a few times a second, on the window thread, it checks whether the engine has dealt with the
// Clear button and, if so, switches the control off too, so the button always shows the truth. A project saved while
// Clear was on opens with it off.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <mph/Engine.h>
#include "Parameters.h"

class MimicPhonProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MimicPhonProcessor();
    ~MimicPhonProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // How long the repeats keep sounding with the current settings (about 60 dB of fall, capped at 120 s; infinite and beyond = the cap).
    double getTailLengthSeconds() const override { return engine.getTailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // For the window: the controls, and a read-only look at the engine.
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    const mph::Engine& getEngine() const { return engine; }

private:
    mph::Params readParams() const;
    void timerCallback() override;
    void switchOff (const char* parameterId);

    juce::AudioProcessorValueTreeState apvts;
    mph::Engine engine;
    std::atomic<bool> switchTransientButtonsOff { false };     // set when a saved project is loaded

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MimicPhonProcessor)
};
