// The list of control values the engine understands: one plain struct, filled in by the plugin wrapper once per
// audio block. Plain C++17, no JUCE.
//
// Every control of the whole plugin is declared here from milestone 2 on, even though most of them do nothing yet
// (the engine passes the audio through unchanged). The wrapper's matching list of control ids (plugin/Source/Parameters.h)
// is frozen at the same time: ids are what Logic stores in saved projects and automation, so they never change later.
//
// The knobs are 0.0 to 1.0 (the wrapper does no scaling); what a knob position means in seconds, semitones or dB is
// decided in the engine, in the milestone that makes that control work, and can be tuned by ear without touching an id.

#pragma once

namespace mph
{
    // The eight Zones of the Mimeophon manual (p. 10): the range of delay times Rate covers in each one.
    // One shared stereo buffer of at least kLongestSeconds serves all of them (plan, interpretation decision 1).
    struct ZoneInfo { const char* label; double minSeconds; double maxSeconds; };
    inline constexpr int kNumZones = 8;
    inline constexpr int kDefaultZone = 4;
    inline constexpr ZoneInfo kZones[kNumZones] = {
        { "0: 1.3 - 20.4 ms",   0.0013,  0.0204 },
        { "1: 20.4 - 81.6 ms",  0.0204,  0.0816 },
        { "2: 81.6 - 326.5 ms", 0.0816,  0.3265 },
        { "3: 163.3 - 653.1 ms",0.1633,  0.6531 },
        { "4: 326.5 ms - 1.31 s", 0.3265, 1.306 },
        { "5: 653 ms - 2.61 s", 0.6531,  2.612  },
        { "6: 1.31 - 5.23 s",   1.306,   5.225  },
        { "7: 2.61 - 41.8 s",   2.612,   41.796 },
    };
    inline constexpr double kLongestSeconds = 41.796;

    // Input gain steps, as the module's firmware has them (manual, appendix): -3 / 0 / +3 / +6 dB. Starts at 0 dB.
    inline constexpr int kNumInputGainSteps = 4;
    inline constexpr int kDefaultInputGainStep = 1;
    inline constexpr float kInputGainStepDb[kNumInputGainSteps] = { -3.0f, 0.0f, 3.0f, 6.0f };

    // Starting positions of the knobs (0..1). Only the starting positions can still be tuned; the ids cannot.
    inline constexpr float kDefaultInputLevel  = 0.75f;   // 0 dB on the level-knob curve (core/Gain.h)
    inline constexpr float kDefaultMix         = 0.5f;
    inline constexpr float kDefaultRepeats     = 0.4f;
    inline constexpr float kDefaultHalo        = 0.0f;
    inline constexpr float kDefaultOutputLevel = 0.75f;   // 0 dB
    inline constexpr float kDefaultColor       = 0.8f;    // "unity", about 3:00 on the module
    inline constexpr float kDefaultRate        = 0.5f;
    inline constexpr float kDefaultURate       = 0.5f;    // the middle = no pitch / doppler offset
    inline constexpr float kDefaultURateLfoRate  = 0.3f;
    inline constexpr float kDefaultURateLfoDepth = 0.0f;  // no modulation until asked for

    inline constexpr int kLfoSine = 0;
    inline constexpr int kLfoTriangle = 1;

    struct Params
    {
        // Knobs, 0..1
        float inputLevel = kDefaultInputLevel;
        float mix = kDefaultMix;
        float repeats = kDefaultRepeats;
        float halo = kDefaultHalo;
        float outputLevel = kDefaultOutputLevel;
        float color = kDefaultColor;
        float rate = kDefaultRate;
        float uRate = kDefaultURate;
        float uRateLfoRate = kDefaultURateLfoRate;
        float uRateLfoDepth = kDefaultURateLfoDepth;

        // Menus (a whole number each)
        int zone = kDefaultZone;                    // 0..7
        int inputGainStep = kDefaultInputGainStep;  // 0..3 into kInputGainStepDb
        int uRateLfoShape = kLfoSine;               // kLfoSine / kLfoTriangle
        int clockDivision = 3;                      // index into core::kClockDivisions (3 = 1/4)

        // Buttons (on / off)
        bool hold = false;
        bool flip = false;
        bool skew = false;
        bool pingPong = false;        // Ping Pong when not skewed, Swap when skewed (one button, as in the plan)
        bool clear = false;           // momentary: acts, then the wrapper switches it back off
        bool tempoSync = false;
        bool uRateLfoSync = false;
    };
}
