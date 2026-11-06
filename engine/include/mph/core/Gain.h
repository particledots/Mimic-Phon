// Small reusable level helpers: decibels, the level-knob curve and the soft limiter (copied from PhenomeEon; the
// Sound On Sound mix law and loop-edge fades were left out, Mimic-Phon does not need them).
// Plain C++, no JUCE. Later plugins can use these as they are.

#pragma once

#include <algorithm>
#include <cmath>

namespace mph::dsp
{
    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

    // The curve of the Input Level and Output Level knobs (position 0..1):
    //   0.00        silent
    //   0.00..0.75  falls from -60 dB up to 0 dB (the very bottom fades to true silence)
    //   0.75        0 dB (no change)
    //   0.75..1.00  rises to +6 dB
    // Our own curve; the knob positions can be retuned by ear later without touching anything else.
    inline float levelKnobToGain (float position)
    {
        const float x = std::min (1.0f, std::max (0.0f, position));
        if (x <= 0.0f)
            return 0.0f;

        const float db = x <= 0.75f ? -60.0f * (1.0f - x / 0.75f)
                                    :   6.0f * (x - 0.75f) / 0.25f;
        float gain = dbToGain (db);
        if (x < 0.02f)
            gain *= x / 0.02f;
        return gain;
    }

    // The same thing in decibels, for read-outs. Returns -120 for silence.
    inline float levelKnobToDb (float position)
    {
        const float g = levelKnobToGain (position);
        return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g);
    }

    // A gentle safety limit for what is written back into the repeats (the feedback). Below 0.8 (about -2 dB) the signal
    // passes exactly as it is; above that it bends smoothly towards 1.0 and never goes above it (for very loud input, above
    // about 2.6, the single-precision result is exactly 1.0). At Repeats = infinite and beyond, the feedback can then
    // saturate but never run away.
    inline float softLimit (float x)
    {
        constexpr float knee = 0.8f, room = 1.0f - knee;
        const float a = std::fabs (x);
        if (! (a > knee))
            return x;
        const float y = knee + room * std::tanh ((a - knee) / room);
        return x < 0.0f ? -y : y;
    }
}
