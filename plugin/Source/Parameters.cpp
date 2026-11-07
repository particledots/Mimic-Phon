#include "Parameters.h"
#include <mph/Params.h>
#include <mph/core/HostClock.h>

namespace MphParams
{

static std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                        name,
                                                        juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f),
                                                        defaultValue);
}

static std::unique_ptr<juce::AudioParameterBool> button (const char* id, const char* name, bool defaultValue = false)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
}

static std::unique_ptr<juce::AudioParameterChoice> menu (const char* id, const char* name, const juce::StringArray& names, int defaultIndex)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, names, defaultIndex);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The knobs. Their starting positions come from the engine's list, so there is one source for them.
    layout.add (knob (inputLevel,    "Input Level",      mph::kDefaultInputLevel));
    layout.add (knob (mix,           "Mix",              mph::kDefaultMix));
    layout.add (knob (repeats,       "Repeats",          mph::kDefaultRepeats));
    layout.add (knob (halo,          "Halo",             mph::kDefaultHalo));
    layout.add (knob (outputLevel,   "Output Level",     mph::kDefaultOutputLevel));
    layout.add (knob (color,         "Color",            mph::kDefaultColor));
    layout.add (knob (rate,          "Rate",             mph::kDefaultRate));
    layout.add (knob (uRate,         "uRate",            mph::kDefaultURate));
    layout.add (knob (uRateLfoRate,  "uRate LFO Rate",   mph::kDefaultURateLfoRate));
    layout.add (knob (uRateLfoDepth, "uRate LFO Depth",  mph::kDefaultURateLfoDepth));

    // Zone: the eight ranges of the manual, named with their delay times (the engine's table is the single source).
    juce::StringArray zoneNames;
    for (int i = 0; i < mph::kNumZones; ++i)
        zoneNames.add (mph::kZones[i].label);
    layout.add (menu (zone, "Zone", zoneNames, mph::kDefaultZone));

    // Input gain step: -3 / 0 / +3 / +6 dB, starting at 0 dB.
    juce::StringArray gainNames;
    for (int i = 0; i < mph::kNumInputGainSteps; ++i)
    {
        const int db = (int) mph::kInputGainStepDb[i];
        gainNames.add ((db > 0 ? "+" : "") + juce::String (db) + " dB");
    }
    layout.add (menu (inputGain, "Input Gain Step", gainNames, mph::kDefaultInputGainStep));

    layout.add (menu (uRateLfoShape, "uRate LFO Shape", { "Sine", "Triangle" }, mph::kLfoSine));

    juce::StringArray divisionNames;
    for (int i = 0; i < mph::core::kNumClockDivisions; ++i)
        divisionNames.add (mph::core::kClockDivisions[i].name);
    layout.add (menu (clockDivision, "Clock Division", divisionNames, mph::core::kDefaultClockDivision));

    layout.add (button (hold,         "Hold"));
    layout.add (button (flip,         "Flip"));
    layout.add (button (skew,         "Skew"));
    layout.add (button (pingPong,     "Ping Pong / Swap"));
    layout.add (button (clear,        "Clear"));
    layout.add (button (tempoSync,    "Tempo Sync"));
    layout.add (button (uRateLfoSync, "uRate LFO Sync"));
    layout.add (button (holdRange,    "Hold Range (off = Zone, on = Full)"));      // 22nd id, appended in 1.0.0

    return layout;
}

} // namespace MphParams
