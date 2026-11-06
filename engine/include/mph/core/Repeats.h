// What the Repeats knob means. Plain C++17, no JUCE.
//
// The knob sets how much of the repeat is fed back into the delay each time round (the "feedback"):
//
//   knob 0.00          0.0   one echo only (nothing is fed back)
//   knob 0.00 .. 0.80  0 .. 1.0 rising along a gentle curve (more and more echoes; 1.0 = each echo as loud as the last)
//   knob 0.80 .. 0.86  1.0   "infinite": the repeats never die away (a flat stretch so the knob can sit on it)
//   knob 0.86 .. 1.00  1.0 .. 1.12   "beyond infinite": the repeats grow until the soft limiter (core/Gain.h) bends them,
//                                    so they saturate and fill up, but never run away
//
// First guesses, tuned by ear later without touching a control id. The window's read-out uses the same function, so the
// number shown is the number heard.

#pragma once

#include <algorithm>
#include <cmath>

namespace mph::core
{
    inline constexpr float kRepeatsInfiniteStart = 0.80f;      // where "infinite" begins
    inline constexpr float kRepeatsInfiniteEnd   = 0.86f;      // where "beyond infinite" begins
    inline constexpr float kRepeatsBeyondMax     = 1.12f;      // feedback with the knob fully right

    inline float repeatsToFeedback (float knob)
    {
        const float x = std::min (1.0f, std::max (0.0f, knob));
        if (x <= 0.0f)
            return 0.0f;
        if (x < kRepeatsInfiniteStart)
        {
            const float t = x / kRepeatsInfiniteStart;
            return t * std::sqrt (t);                          // t to the power 1.5
        }
        if (x <= kRepeatsInfiniteEnd)
            return 1.0f;
        const float u = (x - kRepeatsInfiniteEnd) / (1.0f - kRepeatsInfiniteEnd);
        return 1.0f + (kRepeatsBeyondMax - 1.0f) * u;
    }

    // How long the repeats keep sounding, for the host's "tail": the time for the repeats to fall by 60 dB, counted in whole
    // echoes of the given delay time (one more echo for the first one). Infinite or growing feedback gives the cap.
    inline constexpr double kTailCapSeconds = 120.0;

    inline double tailSeconds (double delaySeconds, float feedback)
    {
        double seconds;
        if (feedback <= 0.0005f)
            seconds = delaySeconds;                            // one echo
        else if (feedback >= 0.999f)
            seconds = kTailCapSeconds;                         // never dies away
        else
        {
            const double echoes = std::ceil (std::log (0.001) / std::log ((double) feedback));
            seconds = delaySeconds * (echoes + 1.0);
        }
        return std::min (kTailCapSeconds, std::max (0.0, seconds));
    }
}
