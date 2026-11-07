// uRate and its LFO: the rules, with no audio in them. Plain C++17, no JUCE.
//
// The engine uses these to move the read points, and the window uses the very same functions for its uRate, LFO Rate, LFO Depth and
// Delay read-outs, so what is written on the panel is what is heard.
//
// What the module does (manual p. 12, and what Bryn heard on his own module):
//   * uRate is a dedicated doppler destination: a small, fixed range that is the same in every Zone, so the doppler effects (chorus,
//     vibrato, flange) are consistent whatever the Zone. A change in delay time bends the pitch of the repeats; a steady offset
//     only shifts the delay a little. In Zones 1-7 the response is linear (an offset in milliseconds, the same in every Zone).
//     In Zone 0 it is one volt per octave (exponential): a pitch control for Karplus-style tones.
//   * With Skew on, in Zones 1-7 the right side gets the opposite offset (stereo chorus). In Zone 0 Skew does not touch uRate.
//   * The module has no LFO (it takes any LFO patched into its uRate input), so the knob and the built-in LFO are our own invention:
//       - the knob adds a steady offset (clockwise = shorter delay = higher pitch, as for Rate and as Zone 0 pitch up),
//       - the LFO adds a swing around it: Rate (free, in Hz, or a note division), Depth, Sine / Triangle.
//   * Bryn's module: the pitch wobble is VERY deep at the extremes, and a lovely chorus when Rate and uRate are about at noon. So the
//     depth knob is on a squared curve: shallow wobble over the first half of its travel, very deep at the far end.
//
// Everything here is a first guess, tuned by ear without touching a control id.

#pragma once

#include <algorithm>
#include <cmath>

#include "HostClock.h"
#include "../Params.h"

namespace mph::core
{
    // ---- the knob ----
    inline constexpr double kURateKnobMaxSeconds = 0.006;      // Zones 1-7: the knob's far ends shift the delay by 6 ms (shorter / longer)
    inline constexpr double kURateKnobMaxSemitones = 12.0;     // Zone 0: the knob's far ends are one octave up / down
    inline constexpr float kURateDeadZone = 0.01f;             // around the middle the knob is exactly zero (nothing is modulated at all)

    // The knob as -1..+1 (0 = middle, exactly zero inside the dead zone, then rising smoothly from zero).
    inline double uRateBipolar (float knob)
    {
        const double x = (std::min (1.0, std::max (0.0, (double) knob)) - 0.5) * 2.0;
        const double a = std::fabs (x);
        if (a <= kURateDeadZone)
            return 0.0;
        const double scaled = (a - kURateDeadZone) / (1.0 - kURateDeadZone);
        return x < 0.0 ? -scaled : scaled;
    }

    // Which way the right side goes: -1 when Skew has split the sides (Zones 1-7 only), else +1.
    inline double uRateSide (int zone, int channel, bool skewedStereo)
    {
        return (zone >= 1 && channel == 1 && skewedStereo) ? -1.0 : 1.0;
    }

    // A delay time (seconds) with the knob's steady offset in it. Zone 0: exponential (1 V/oct); Zones 1-7: linear.
    inline double uRateStaticDelay (int zone, double baseSeconds, float knob, int channel, bool skewedStereo)
    {
        const double u = uRateBipolar (knob);
        if (u == 0.0)
            return baseSeconds;
        if (zone == 0)
            return baseSeconds * std::exp2 (-u * kURateKnobMaxSemitones / 12.0);
        return baseSeconds - uRateSide (zone, channel, skewedStereo) * u * kURateKnobMaxSeconds;
    }

    // ---- the LFO ----
    inline constexpr double kLfoMinHz = 0.1, kLfoMaxHz = 20.0;
    inline constexpr double kLfoMaxSeconds = 0.012;            // Zones 1-7: Depth at the far end swings the delay by up to +-12 ms ...
    inline constexpr double kLfoMaxSemitones = 12.0;           // Zone 0: ... or +-12 semitones
    inline constexpr double kLfoMaxSlope = 0.6;                // ... but never faster than this change of pitch (0.6 = from -15 to +8 semitones)
    inline constexpr double kTwoPi = 6.28318530717958647692;

    // Depth as a share of the full swing: a squared curve (shallow first, very deep at the far end).
    inline double lfoAmount (float depth)
    {
        const double d = std::min (1.0, std::max (0.0, (double) depth));
        return d * d;
    }

    // Free-running LFO speed from the Rate knob: logarithmic, 0.1 Hz to 20 Hz.
    inline double lfoFreeHz (float rate01)
    {
        const double x = std::min (1.0, std::max (0.0, (double) rate01));
        return kLfoMinHz * std::pow (kLfoMaxHz / kLfoMinHz, x);
    }

    // Synced: the Rate knob picks one of the clock divisions (ten bands), slowest on the left, as free Rate is.
    // The names and lengths are the Clock Division menu's own (core::kClockDivisions); one LFO cycle = one division.
    inline int lfoSyncStep (float rate01)
    {
        const double x = std::min (1.0, std::max (0.0, (double) rate01));
        return std::min (kNumClockDivisions - 1, (int) std::floor (x * kNumClockDivisions));
    }
    inline double lfoCycleBeats (float rate01, double beatsPerBar) { return beatsPerTick (lfoSyncStep (rate01), beatsPerBar); }
    inline const char* lfoSyncName (float rate01) { return kClockDivisions[lfoSyncStep (rate01)].name; }

    // How fast the LFO runs, in Hz, free or synced to a tempo (0 or less = Logic gave none: 120 BPM).
    inline double lfoHz (float rate01, bool synced, double bpm, double beatsPerBar)
    {
        if (! synced)
            return lfoFreeHz (rate01);
        const double b = bpm > 0.0 && std::isfinite (bpm) ? bpm : 120.0;
        return b / 60.0 / lfoCycleBeats (rate01, beatsPerBar);
    }

    // The LFO's peak swing. Zones 1-7: seconds, held back at fast speeds so the pitch never swings wilder than kLfoMaxSlope.
    // Zone 0: semitones.
    inline double lfoPeakSeconds (float depth, double hz)
    {
        double a = kLfoMaxSeconds * lfoAmount (depth);
        if (hz > 0.0)
            a = std::min (a, kLfoMaxSlope / (kTwoPi * hz));
        return a;
    }
    inline double lfoPeakSemitones (float depth) { return kLfoMaxSemitones * lfoAmount (depth); }

    // One LFO value, -1..+1, from a phase (0..1). Both shapes start at zero and rise.
    inline double lfoValue (int shape, double phase)
    {
        if (shape == kLfoTriangle)
            return phase < 0.25 ? 4.0 * phase : (phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0);
        return std::sin (kTwoPi * phase);
    }
}
