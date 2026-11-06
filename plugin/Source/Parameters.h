// The list of controls Logic can see and automate: every control of the whole plugin (21 in all), declared in
// milestone 2 and FROZEN from then on.
//
// The short ids below are what Logic stores in saved projects and automation, so they must never be renamed or removed.
// If a later milestone ever needs one more control, it gets a new id added at the end (and old projects open with it
// at its starting position). Most of these do nothing yet: milestone 2 passes the audio through unchanged.
//
// Which milestone makes which control work (plan): Zone, Rate, Repeats, Mix, Input Level, Input Gain Step and
// Output Level in 4; Color in 5; Skew, Ping Pong / Swap in 6; uRate and its LFO in 7; Tempo Sync, its Clock Division and
// the LFO Sync in 8; Halo in 9; Flip in 10; Hold in 11. Clear works with the first echo (4).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace MphParams
{
    // Knobs (0.0 to 1.0), in panel order: first row, then second row
    inline constexpr const char* inputLevel    = "inputLevel";
    inline constexpr const char* mix           = "mix";
    inline constexpr const char* repeats       = "repeats";
    inline constexpr const char* halo          = "halo";
    inline constexpr const char* outputLevel   = "outputLevel";
    inline constexpr const char* color         = "color";
    inline constexpr const char* rate          = "rate";
    inline constexpr const char* uRate         = "uRate";
    inline constexpr const char* uRateLfoRate  = "uRateLfoRate";
    inline constexpr const char* uRateLfoDepth = "uRateLfoDepth";

    // Menus (stepped controls)
    inline constexpr const char* zone          = "zone";             // the eight Zones, 0 to 7
    inline constexpr const char* inputGain     = "inputGain";        // -3 / 0 / +3 / +6 dB
    inline constexpr const char* uRateLfoShape = "uRateLfoShape";    // Sine / Triangle
    inline constexpr const char* clockDivision = "clockDivision";    // the tempo-sync clock: 2 bars ... 1/32 (default 1/4)

    // Buttons (on / off, automatable)
    inline constexpr const char* hold          = "hold";
    inline constexpr const char* flip          = "flip";
    inline constexpr const char* skew          = "skew";
    inline constexpr const char* pingPong      = "pingPong";         // Ping Pong when not skewed, Swap when skewed
    inline constexpr const char* clear         = "clear";            // momentary: acts, then switches itself off
    inline constexpr const char* tempoSync     = "tempoSync";
    inline constexpr const char* uRateLfoSync  = "uRateLfoSync";

    inline constexpr int kNumParameters = 21;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
