// What the Color knob means, and the filter that does it. Plain C++17, no JUCE.
//
// Color shapes the character of what is written into the repeater (the live signal plus the feedback). Because the
// shaping is part of the loop, every echo is shaped once more than the one before: a dark setting gets darker and darker
// along the echoes, a thin one gets thinner. The window's read-out uses the same functions, so the word shown is the sound heard.
//
//   knob 0.00 .. 0.78   DARK side, from "oil can" (fully left) through warm and crunchy BBD to tape:
//                       a saturation (tanh) that grows with the setting, then a 2-pole low-pass that closes
//                       from 17 kHz down to 700 Hz (no high-pass).
//   knob 0.78 .. 0.82   UNITY (the knob's 3:00): nothing is done at all; the audio goes through bit for bit.
//   knob 0.82 .. 1.00   THIN side, from "crisp" to "thin digital": a little saturation (fizz), then a 2-pole high-pass
//                       that rises from 20 Hz up to 1.5 kHz (no low-pass: the top end is left alone).
//
// A short stretch each side of unity (kColorSmoothBand) fades the effect in, so there is no jump when leaving unity.
// All first guesses, to be tuned by ear without touching a control id.

#pragma once

#include <algorithm>
#include <cmath>

namespace mph::core
{
    inline constexpr float kColorUnity      = 0.80f;    // the knob position that does nothing (about 3:00)
    inline constexpr float kColorUnityBand  = 0.02f;    // exactly nothing within this distance of unity
    inline constexpr float kColorSmoothBand = 0.05f;    // the effect fades in over this much more
    inline constexpr float kColorBandRounding = 1.0e-4f; // so that a knob sitting on the edge of the band (0.78, 0.82) is inside it

    inline constexpr double kColorDarkOpenHz   = 17000.0;   // low-pass at the first touch of "dark"
    inline constexpr double kColorDarkClosedHz = 700.0;     // low-pass fully left
    inline constexpr double kColorDarkDrive    = 2.2;       // saturation fully left (tanh (k x) / k: small signals unchanged)
    inline constexpr double kColorThinOpenHz   = 20.0;      // high-pass at the first touch of "thin"
    inline constexpr double kColorThinClosedHz = 1500.0;    // high-pass fully right
    inline constexpr double kColorThinDrive    = 1.2;       // saturation fully right

    struct ColorShape
    {
        float amount = 0.0f;         // 0 = untouched (exact bypass), 1 = the full effect; in between fades
        float drive = 0.0f;          // saturation: y = tanh (drive x) / drive (0 = none)
        float lowpassHz = 0.0f;      // 0 = no low-pass stage
        float highpassHz = 0.0f;     // 0 = no high-pass stage
    };

    inline ColorShape colorShape (float knob)
    {
        const float c = std::min (1.0f, std::max (0.0f, knob));
        ColorShape s;

        const float distance = std::fabs (c - kColorUnity);
        const float fade = std::min (1.0f, std::max (0.0f, (distance - kColorUnityBand - kColorBandRounding) / kColorSmoothBand));
        s.amount = fade * fade * (3.0f - 2.0f * fade);                     // smooth start, smooth end

        if (c < kColorUnity)
        {
            const double edge = (double) (kColorUnity - kColorUnityBand);
            const double d = std::min (1.0, std::max (0.0, ((double) edge - (double) c) / edge));    // 0 at the edge, 1 fully left
            s.lowpassHz = (float) (kColorDarkOpenHz * std::pow (kColorDarkClosedHz / kColorDarkOpenHz, std::pow (d, 0.85)));
            s.drive = (float) (kColorDarkDrive * std::pow (d, 1.2));
        }
        else
        {
            const double edge = (double) (kColorUnity + kColorUnityBand);
            const double t = std::min (1.0, std::max (0.0, ((double) c - edge) / (1.0 - edge)));   // 0 at the edge, 1 fully right
            s.highpassHz = (float) (kColorThinOpenHz * std::pow (kColorThinClosedHz / kColorThinOpenHz, std::pow (t, 0.9)));
            s.drive = (float) (kColorThinDrive * std::pow (t, 1.5));
        }
        return s;
    }

    // The word the window shows (the manual's own description, from the darkest oil-can echo to a thin early digital delay line).
    inline const char* colorWord (float knob)
    {
        if (knob < 0.15f) return "oil can";
        if (knob < 0.32f) return "warm BBD";
        if (knob < 0.48f) return "crunch BBD";
        if (knob < 0.68f) return "tape";
        if (knob < 0.84f) return "clean";
        if (knob < 0.92f) return "crisp";
        return "thin";
    }

    // The filter itself: saturation, then a 2-pole low-pass, then a 2-pole high-pass (both Butterworth, so they never boost
    // anything and cannot add gain in the feedback loop). One set of coefficients, one set of memories per channel.
    class ColorFilter
    {
    public:
        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            reset();
            setShape (ColorShape {});
        }

        void reset()
        {
            for (auto& s : state)
                s = {};
        }

        bool isBypassed() const { return shape.amount <= 0.0f; }
        float getAmount() const { return shape.amount; }

        void setShape (const ColorShape& newShape)
        {
            shape = newShape;
            const double nyquistLimit = 0.45 * sampleRate;
            if (shape.lowpassHz > 0.0f)
                makeLowpass (lp, std::min ((double) shape.lowpassHz, nyquistLimit));
            if (shape.highpassHz > 0.0f)
                makeHighpass (hp, std::min ((double) shape.highpassHz, nyquistLimit));
        }

        // One sample of one channel (0 or 1). Exactly the input when the shape's amount is 0.
        float process (int channel, float in)
        {
            if (shape.amount <= 0.0f)
            {
                state[channel] = {};                               // nothing is kept while bypassed, so nothing stale comes back
                return in;
            }

            double x = (double) in;
            if (shape.drive > 1.0e-4f)
            {
                const double k = (double) shape.drive;
                x = std::tanh (k * x) / k;
            }
            State& s = state[channel];
            if (shape.lowpassHz > 0.0f)
                x = run (lp, s.lp, x);
            if (shape.highpassHz > 0.0f)
                x = run (hp, s.hp, x);
            return in + shape.amount * ((float) x - in);
        }

    private:
        struct Coefficients { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
        struct Memory { double z1 = 0.0, z2 = 0.0; };
        struct State { Memory lp, hp; };

        static constexpr double kQ = 0.70710678118654752;          // Butterworth: flat, no peak
        static constexpr double kPi = 3.14159265358979323846;

        void makeLowpass (Coefficients& c, double hz) const
        {
            const double w = 2.0 * kPi * hz / sampleRate, cw = std::cos (w), alpha = std::sin (w) / (2.0 * kQ);
            const double a0 = 1.0 + alpha;
            c.b0 = (1.0 - cw) * 0.5 / a0;
            c.b1 = (1.0 - cw) / a0;
            c.b2 = c.b0;
            c.a1 = -2.0 * cw / a0;
            c.a2 = (1.0 - alpha) / a0;
        }

        void makeHighpass (Coefficients& c, double hz) const
        {
            const double w = 2.0 * kPi * hz / sampleRate, cw = std::cos (w), alpha = std::sin (w) / (2.0 * kQ);
            const double a0 = 1.0 + alpha;
            c.b0 = (1.0 + cw) * 0.5 / a0;
            c.b1 = -(1.0 + cw) / a0;
            c.b2 = c.b0;
            c.a1 = -2.0 * cw / a0;
            c.a2 = (1.0 - alpha) / a0;
        }

        // Transposed direct form II, in double precision (a 20 Hz high-pass is delicate in single precision).
        static double run (const Coefficients& c, Memory& m, double x)
        {
            const double y = c.b0 * x + m.z1;
            m.z1 = c.b1 * x - c.a1 * y + m.z2;
            m.z2 = c.b2 * x - c.a2 * y;
            if (std::fabs (m.z1) < 1.0e-30) m.z1 = 0.0;             // no slow denormal numbers in the quiet tails
            if (std::fabs (m.z2) < 1.0e-30) m.z2 = 0.0;
            return y;
        }

        double sampleRate = 48000.0;
        ColorShape shape;
        Coefficients lp, hp;
        State state[2];
    };
}
