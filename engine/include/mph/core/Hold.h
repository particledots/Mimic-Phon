// Hold: freeze the sound into a loop. Plain C++17, no JUCE.
//
// What the module does (manual p. 14, and what Bryn heard on his own module):
//   * Hold on: no sound enters or leaves the feedback path. The Mimeophon ignores the input and goes on repeating what it has, according to Zone
//     and Rate, with its timbre set by Color and Halo. Pressing it makes no click; the loop is as long as the delay time was.
//   * While Hold is on, every other control stays active (and none of them damages what is stored: you can always come back to the same sound).
//   * Repeats stops being "how many echoes" and becomes the START POINT of the loop inside the current Zone: it scrubs through the stored sound.
//
// How it is built here: pressing Hold stops writing into the shared memory (core/DelayBuffer.h): from that moment the memory is the stored sound.
// The loop is one stretch of it, "length" long (the delay time of the Zone and Rate, the Left / Right figure in the window), and it ends
// "start" before the moment Hold was pressed:
//
//        stored sound:  ...............[ start | <------ loop ------> ]..press
//
//   * Repeats 0 (fully left) = start 0: the loop is the newest stretch, exactly what the delay was playing, so pressing makes no jump and no
//     click (the loop carries on from the very sample the delay was reading). Turning Repeats right moves the start back through the Zone's
//     memory, up to the Zone's longest time (Zone 4: 1.3 s, Zone 7: 41.8 s, minus the loop). A far move is a crossfaded jump (like a Zone change);
//     a small move glides, so a hand on the knob scrubs the loop with a little tape-like bend.
//   * The loop seam is a short raised-cosine crossfade (a quarter of the loop, never more than 30 ms, never less than 4 samples) with the sound
//     just BEFORE the loop starts, so the loop closes without a click (core::holdLoopValue is a seamless, repeating function of the position).
//   * Rate, Zone, Tempo Sync, Skew and the Ping Pong hop set the loop length (and each side its own); a jump of any of them starts the new loop at
//     its beginning with a 20 ms crossfade. Rate gliding changes the loop length smoothly (no doppler: the loop plays at normal speed and simply
//     wraps sooner or later). uRate and its LFO still bend the loop (clockwise = pitch up; Zone 0: they set the loop length, so the pitch).
//   * Flip reverses the held loop (it plays the same stretch backwards, seam included). Color shapes the loop that is heard (a second colour filter,
//     not written into the memory: Color is not cumulative while held, and leaves the stored sound untouched). Halo works on the loop heard. Ping
//     Pong / Swap: each pass of the left loop the two sides trade places. Clear empties the stored sound (the loop goes silent).
//   * Releasing Hold crossfades back to the live repeat in 10 ms; writing and feedback resume where they stopped.
// All numbers are first guesses, to be tuned by ear without touching a control id.

#pragma once

#include <algorithm>
#include <cmath>

#include "../Params.h"
#include "DelayBuffer.h"

namespace mph::core
{
    inline constexpr double kHoldFadeFraction = 0.25;         // the loop seam is this part of the loop ...
    inline constexpr double kHoldFadeMaxSeconds = 0.030;      // ... but never more than this ...
    inline constexpr double kHoldFadeMinSamples = 4.0;        // ... and never less than this
    inline constexpr double kHoldMinLoopSamples = 16.0;
    // The loop, its start point and its seam must fit in the shared memory (41.8 s); a little is kept back for the seam and the cubic reads.
    inline constexpr double kHoldReserveSeconds = kHoldFadeMaxSeconds + 0.001;
    inline constexpr double kHoldMaxLoopSeconds = kLongestSeconds - kHoldReserveSeconds;
    inline constexpr double kHoldScrubGlideSeconds = 0.040;   // a small move of the start point glides over about this long
    inline constexpr double kHoldScrubJumpSeconds = 0.020;    // a move bigger than this is a crossfaded jump instead
    inline constexpr double kHoldPressJumpSeconds = 0.001;    // pressing Hold with the start point further back than this: crossfaded jump

    // The loop length that is heard, in seconds, for a delay time (very long ones are held to what the memory can keep: 41.76 s).
    inline double holdLoopSeconds (double delaySeconds)
    {
        return std::min (kHoldMaxLoopSeconds, std::max (0.0, delaySeconds));
    }

    // Where the loop starts, in seconds back from the moment Hold was pressed, for a Repeats knob position: 0 = the newest stretch, fully
    // right = as far back as the current Zone reaches (the Zone's longest time), minus the loop itself.
    // Milestone 12: with "full" (the Hold Range switch on "Full") the scrub reaches as far back as the longest Zone does (the whole memory,
    // 41.8 s minus the loop), whatever the Zone is. With full = false it is exactly what it was in 0.11.0.
    inline double holdStartSeconds (float repeatsKnob, int zone, double loopSeconds, bool full = false)
    {
        const double k = std::min (1.0, std::max (0.0, (double) repeatsKnob));
        const int z = full ? kNumZones - 1 : std::min (kNumZones - 1, std::max (0, zone));
        const double room = std::min (kZones[z].maxSeconds, kHoldMaxLoopSeconds) - loopSeconds;
        return room > 0.0 ? k * room : 0.0;
    }

    // The seam's length in samples for a loop length in samples.
    inline double holdFadeSamples (double loopSamples, double sampleRate)
    {
        return std::max (kHoldFadeMinSamples, std::min (kHoldFadeFraction * loopSamples, kHoldFadeMaxSeconds * sampleRate));
    }

    // The held loop as one seamless function that repeats every "length": v is how far back from the loop's END the sound is read (any number:
    // it is folded into 0 .. length), "end" is the absolute memory position of the loop's end. A loop that plays forwards makes v fall by one per
    // sample; one that plays backwards makes it rise. In the last "fade" samples before the end the sound is crossfaded with the sound just
    // before the START, so value (v = 0) = value (v = length): no jump when the loop wraps, in either direction.
    inline float holdLoopValue (const dsp::DelayBuffer& buffer, double end, double v, double length, double fade)
    {
        v -= length * std::floor (v / length);
        float a = buffer.readAt (end - v);
        if (v < fade)
        {
            const double w = 0.5 + 0.5 * std::cos (3.14159265358979323846 * v / fade);      // 1 at the end of the loop, 0 where the seam begins
            const float b = buffer.readAt (end - v - length);
            a += (float) w * (b - a);
        }
        return a;
    }

    // One side's loop read point.
    struct HoldHead
    {
        double u = 0.0;        // forward phase: samples back from the loop's end (counts down; wraps to the loop length)
        double ur = 0.0;       // backward phase (Flip): counts up from the loop's end
        double s = 0.0;        // start point, in samples back from the moment Hold was pressed (follows Repeats, glided)
    };
}
