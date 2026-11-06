// What the Zone and Rate controls mean in time. Plain C++17, no JUCE.
//
// Milestone 3 uses this for the window's read-outs and lights only (the repeater itself arrives in milestone 4, which will
// use the same functions, so the window and the sound can never disagree). Both mappings are first guesses that are tuned by
// ear later without touching a control id:
//
//   Free running (Tempo Sync off): inside the selected Zone the Rate knob moves the delay time from the Zone's shortest to
//   its longest time on a logarithmic curve (equal knob steps = equal musical steps). Knob fully left = shortest time =
//   the highest repeat rate (the manual: "x2" is at the left end of the Rate control).
//
//   Tempo Sync on: the Rate knob chooses one of 13 multiples / divisions of the clock (manual p. 13), in 13 equal bands,
//   "1/1" in the middle. The Zone then multiplies or divides the result by powers of 2 relative to the Zone that fits the
//   clock best ("the Zone most appropriate to the Tempo", manual p. 13).

#pragma once

#include <algorithm>
#include <cmath>

#include "../Params.h"

namespace mph::core
{
    // ---- free running ----

    // Delay time in seconds for a Zone (0..7) and a Rate knob position (0..1).
    inline double freeDelaySeconds (int zone, float rate01)
    {
        const auto& z = kZones[std::min (kNumZones - 1, std::max (0, zone))];
        const double x = std::min (1.0, std::max (0.0, (double) rate01));
        return z.minSeconds * std::pow (z.maxSeconds / z.minSeconds, x);
    }

    // ---- tempo sync ----

    inline constexpr int kNumSyncSteps = 13;
    inline constexpr int kSyncUnityStep = 6;                    // "1/1", the middle of the Rate knob

    // How many times faster than the clock each step repeats (2 = twice as fast), from the left end of the knob to the right.
    inline constexpr double kSyncFactors[kNumSyncSteps] = {
        2.0, 1.75, 5.0 / 3.0, 1.5, 4.0 / 3.0, 1.25, 1.0, 1.0 / 1.25, 0.75, 2.0 / 3.0, 0.6, 4.0 / 7.0, 0.5
    };
    // The names as the manual writes them.
    inline constexpr const char* kSyncNames[kNumSyncSteps] = {
        "x2", "x1 3/4", "x1 2/3", "x1 1/2", "x1 1/3", "x1 1/4", "1/1", "/1 1/4", "/1 1/3", "/1 1/2", "/1 2/3", "/1 3/4", "/2"
    };

    // The step for a Rate knob position: 13 equal bands.
    inline int syncStep (float rate01)
    {
        const double x = std::min (1.0, std::max (0.0, (double) rate01));
        return std::min (kNumSyncSteps - 1, (int) std::floor (x * kNumSyncSteps));
    }

    // The Zone that suits a clock period best: the one whose middle (on a logarithmic scale) is closest to the period.
    inline int bestZoneForPeriod (double periodSeconds)
    {
        const double p = periodSeconds > 1.0e-6 ? periodSeconds : 1.0e-6;
        int best = 0;
        double bestDistance = 1.0e30;
        for (int i = 0; i < kNumZones; ++i)
        {
            const double middle = std::sqrt (kZones[i].minSeconds * kZones[i].maxSeconds);
            const double distance = std::fabs (std::log2 (p / middle));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    // Delay time in seconds with Tempo Sync on: the clock period, divided by the step's factor, doubled for every Zone above
    // the best-fitting one (halved for every Zone below it), kept inside the whole range of the module.
    inline double syncDelaySeconds (int zone, float rate01, double clockPeriodSeconds)
    {
        const int z = std::min (kNumZones - 1, std::max (0, zone));
        const int reference = bestZoneForPeriod (clockPeriodSeconds);
        const double seconds = clockPeriodSeconds * std::pow (2.0, z - reference) / kSyncFactors[syncStep (rate01)];
        return std::min (kLongestSeconds, std::max (kZones[0].minSeconds, seconds));
    }
}
