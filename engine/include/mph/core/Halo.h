// What the Halo knob means, and the diffusion that does it. Plain C++17, no JUCE.
//
// Halo smears the repeats in stereo space (manual: "a smear of excess sound around the edges of the Repeats"). From Bryn's module:
//   fully left     nothing at all
//   middle         a tight, Karplus-like reverb (short, ringing, a little pitched)
//   fully right    a big reverb with a wide stereo image; it does not run away
//   Repeats fully left, Mix fully right (the "Filter-Verb" patch): Halo still works, so it acts on the repeat that is heard, not only
//                  on what is fed back.
//   Color          sets Halo's timbre (a dark Color gives a dark reverb, a thin Color a thin one).
//   high settings  the halos are also injected into the feedback path with the repeats ("wider and denser").
//
// How it is built here: the repeat that is read from the delay buffer (DC removed) goes into
//   1. a high-pass (only when Color is on its thin side),
//   2. four allpass diffusers per side (their times grow with the knob: a smear at first, a wash at the end),
//   3. one feedback line per side ("the tank"): its time grows with the knob (2.5 ms = a plucked-string pitch, 60 ms = a hall),
//      a damping low-pass in the loop (set by Color), and a rotation between the two sides in the loop (a lossless mix of left and
//      right, more of it as the knob goes up, which is what widens the image). The loop gain stays below 0.9 and the rotation loses
//      nothing, so the tail always dies away: it cannot run away by itself.
// The halo's sound goes two places: to the output beside the repeat (core: haloMix) and, at high settings, into the feedback path
// (injection; it is scaled by Repeats in the engine, so with Repeats fully left there is no loop through the delay buffer at all).
//
// The window's read-out (haloWord, haloText) and the host tail (haloTailSeconds) use the same rules as the sound.
// All numbers are first guesses, to be tuned by ear without touching a control id.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "Color.h"

namespace mph::core
{
    inline constexpr float kHaloDeadZone = 0.004f;          // at or below this the knob does nothing at all (the audio goes through bit for bit)
    inline constexpr double kHaloTankMinSeconds = 0.0025;   // tank line at the first touch (about 400 Hz: string-like)
    inline constexpr double kHaloTankMaxSeconds = 0.060;    // tank line fully right (a hall)
    inline constexpr double kHaloTankRightRatio = 1.137;    // the right side's line is this much longer (decorrelates the two sides)
    inline constexpr double kHaloDiffMinScale = 0.15;       // allpass times at the first touch, as a part of their full size
    inline constexpr float kHaloFeedbackMin = 0.30f;        // tank loop gain at the first touch
    inline constexpr float kHaloFeedbackMax = 0.88f;        // tank loop gain fully right (never 1: the tail always ends)
    inline constexpr float kHaloAllpassGain = 0.60f;
    inline constexpr double kHaloCrossMaxRadians = 1.13;    // rotation between the sides fully right (about 65 degrees)
    inline constexpr float kHaloLevelKnob = 0.70f;          // the halo reaches its full level here
    inline constexpr float kHaloInjectStart = 0.55f;        // above this the halo is also injected into the feedback path
    inline constexpr float kHaloInjectMax = 0.50f;          // how much, fully right (times the Repeats amount)
    inline constexpr double kHaloDampHz = 8000.0;           // tank damping at unity Color (a dark Color lowers it)
    inline constexpr double kHaloTailCapSeconds = 6.0;

    struct HaloShape
    {
        float level = 0.0f;           // how much halo is heard: 0 = none, 1 = full (the repeat itself is turned down by half at full)
        float feedback = 0.0f;        // tank loop gain
        float tankSeconds = 0.0f;     // the left side's tank line
        float diffScale = 1.0f;       // allpass times as a part of full
        float crossRadians = 0.0f;    // rotation between the two sides in the loop
        float inject = 0.0f;          // how much goes into the feedback path (before the Repeats amount)
    };

    inline HaloShape haloShape (float knob)
    {
        const float h = std::min (1.0f, std::max (0.0f, knob));
        HaloShape s;
        if (h <= kHaloDeadZone)
            return s;
        const float t = std::min (1.0f, h / kHaloLevelKnob);
        s.level = std::sin (t * 1.57079632679f);
        s.feedback = kHaloFeedbackMin + (kHaloFeedbackMax - kHaloFeedbackMin) * std::pow (h, 0.8f);
        s.tankSeconds = (float) (kHaloTankMinSeconds + (kHaloTankMaxSeconds - kHaloTankMinSeconds) * (double) h * (double) h);
        s.diffScale = (float) (kHaloDiffMinScale + (1.0 - kHaloDiffMinScale) * std::pow ((double) h, 1.5));
        s.crossRadians = (float) (kHaloCrossMaxRadians * (double) h);
        const float over = std::max (0.0f, (h - kHaloInjectStart) / (1.0f - kHaloInjectStart));
        s.inject = kHaloInjectMax * over * std::sqrt (over);
        return s;
    }

    // How the halo and the repeat meet at the output: repeat * (1 - 0.5 level) + level * halo.
    inline float haloMix (float repeat, float halo, float level)
    {
        return repeat + level * (halo - 0.5f * repeat);
    }

    // Color's say in Halo's timbre: the tank's damping low-pass and the input high-pass, from Color's own shape (core/Color.h).
    struct HaloTimbre { double dampHz = kHaloDampHz; double highpassHz = 0.0; };
    inline HaloTimbre haloTimbre (const ColorShape& c)
    {
        HaloTimbre t;
        const double lp = c.lowpassHz > 0.0f ? std::min (kHaloDampHz, (double) c.lowpassHz) : kHaloDampHz;
        t.dampHz = kHaloDampHz * std::pow (lp / kHaloDampHz, (double) c.amount);
        t.highpassHz = c.highpassHz > 0.0f ? (double) c.highpassHz * (double) c.amount : 0.0;
        return t;
    }

    // How long the halo rings after the repeat that feeds it (the host's tail). 0 when Halo is off.
    inline double haloTailSeconds (float knob)
    {
        const HaloShape s = haloShape (knob);
        if (s.level <= 0.0f)
            return 0.0;
        const double line = (double) s.tankSeconds * (1.0 + (kHaloTankRightRatio - 1.0) * 0.5);
        const double rt60 = 3.0 * line / -std::log10 ((double) s.feedback);
        return std::min (kHaloTailCapSeconds, rt60 * 1.1);
    }

    // The word and the percentage in the window
    inline const char* haloWord (float knob)
    {
        if (knob <= kHaloDeadZone) return "off";
        if (knob < 0.15f) return "glint";
        if (knob < 0.40f) return "string";
        if (knob < 0.62f) return "tight verb";
        if (knob < 0.85f) return "room";
        return "wide hall";
    }

    namespace detail
    {
        // A delay line read between samples (linear: it only smears, so the gentle dulling of linear reading is welcome)
        struct HaloLine
        {
            std::vector<float> memory;
            int size = 8, position = 0;
            void prepare (int length) { size = std::max (length, 8); memory.assign ((size_t) size, 0.0f); position = 0; }
            void clear() { std::fill (memory.begin(), memory.end(), 0.0f); position = 0; }
            // the sample written "delay" samples ago (1 = the newest); call before write()
            float read (double delay) const
            {
                const double d = std::min ((double) (size - 2), std::max (1.0, delay));
                const double at = (double) position - d;
                const double whole = std::floor (at);
                const float f = (float) (at - whole);
                int i = (int) whole % size;
                if (i < 0)
                    i += size;
                const int j = i + 1 >= size ? 0 : i + 1;
                return memory[(size_t) i] + f * (memory[(size_t) j] - memory[(size_t) i]);
            }
            void write (float x)
            {
                memory[(size_t) position] = x;
                position = position + 1 >= size ? 0 : position + 1;
            }
        };

        inline constexpr double kAllpassMs[2][4] = { { 3.9, 5.7, 8.9, 12.7 }, { 4.4, 6.3, 9.5, 13.9 } };
    }

    class Halo
    {
    public:
        void prepare (double newSampleRate, int newChannels)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            channels = newChannels < 1 ? 1 : (newChannels > 2 ? 2 : newChannels);
            const int length = (int) std::ceil (0.2 * sampleRate) + 16;
            for (int c = 0; c < 2; ++c)
            {
                tank[c].prepare (length);
                for (auto& a : allpass[c])
                    a.prepare (length);
            }
            gainCoefficient = (float) (1.0 - std::exp (-1.0 / (0.025 * sampleRate)));
            delayCoefficient = 1.0 - std::exp (-1.0 / (0.100 * sampleRate));
            modStep[0] = 0.37 / sampleRate;
            modStep[1] = 0.53 / sampleRate;
            modPhase[0] = 0.0;
            modPhase[1] = 0.25;
            setTimbre (ColorShape());
            knob = 0.0f;
            snap();
            reset();
        }

        void reset()
        {
            for (int c = 0; c < 2; ++c)
            {
                tank[c].clear();
                for (auto& a : allpass[c])
                    a.clear();
                dampState[c] = hpState[c] = 0.0f;
            }
            wasActive = false;
        }

        // Called once per block with the knob, and whenever Color's shape changes.
        void setKnob (float k) { knob = std::min (1.0f, std::max (0.0f, k)); }
        void setTimbre (const ColorShape& c)
        {
            const HaloTimbre t = haloTimbre (c);
            dampCoefficient = (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * t.dampHz / sampleRate));
            hpCoefficient = t.highpassHz > 0.0 ? (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * t.highpassHz / sampleRate)) : 0.0f;
        }
        // Everything goes to the knob's setting at once (the first block after prepare()).
        void snap()
        {
            target = haloShape (knob);
            shape = target;
            tankSeconds = target.tankSeconds;
            countdown = kUpdateSamples;
        }

        // The halo is heard (or still dying away)
        bool active() const { return knob > kHaloDeadZone || shape.level > 0.0f; }
        float level() const { return shape.level; }
        float injection() const { return shape.inject; }

        // One sample. in[] and out[] hold one value per channel (channel count as given to prepare()). Returns false (and zeros) when the
        // halo is off; the lines are emptied once as it turns off, so it starts from silence the next time.
        bool process (const float* in, float* out)
        {
            if (! active())
            {
                if (wasActive)
                    reset();
                out[0] = out[1] = 0.0f;
                return false;
            }
            if (! wasActive)
            {
                // Turning on from silence: the lines start at the knob's size at once (no chirp from nothing); only the level rises.
                const float rising = shape.level;
                target = haloShape (knob);
                shape = target;
                shape.level = rising;
                tankSeconds = target.tankSeconds;
                countdown = kUpdateSamples;
                wasActive = true;
            }

            if (--countdown <= 0)
            {
                countdown = kUpdateSamples;
                target = haloShape (knob);
            }
            glide (shape.level, target.level);
            glide (shape.feedback, target.feedback);
            glide (shape.inject, target.inject);
            glide (shape.crossRadians, target.crossRadians);
            glide (shape.diffScale, target.diffScale);
            tankSeconds += (target.tankSeconds - tankSeconds) * delayCoefficient;
            if (target.level == 0.0f && std::fabs (target.tankSeconds - tankSeconds) < 1.0e-9)
                tankSeconds = target.tankSeconds;

            const float cs = channels == 2 ? std::cos (shape.crossRadians) : 1.0f;
            const float sn = channels == 2 ? std::sin (shape.crossRadians) : 0.0f;

            float diffused[2] = { 0.0f, 0.0f }, rung[2] = { 0.0f, 0.0f };
            for (int c = 0; c < channels; ++c)
            {
                float x = in[c];
                if (hpCoefficient > 0.0f)
                {
                    hpState[c] += hpCoefficient * (x - hpState[c]);
                    x -= hpState[c];
                }
                for (int k = 0; k < 4; ++k)
                {
                    auto& line = allpass[c][k];
                    const double delay = std::max (2.0, detail::kAllpassMs[c][k] * 0.001 * (double) shape.diffScale * sampleRate);
                    const float d = line.read (delay);
                    const float w = x + kHaloAllpassGain * d;
                    x = d - kHaloAllpassGain * w;
                    line.write (w);
                }
                diffused[c] = x;

                modPhase[c] += modStep[c];
                if (modPhase[c] >= 1.0)
                    modPhase[c] -= 1.0;
                const double side = c == 1 ? kHaloTankRightRatio : 1.0;
                const double wobble = 0.004 * (double) tankSeconds * std::sin (6.283185307179586 * modPhase[c]);
                const double delay = std::max (4.0, ((double) tankSeconds * side + wobble) * sampleRate);
                const float tapped = tank[c].read (delay);
                dampState[c] += dampCoefficient * (tapped - dampState[c]);
                rung[c] = dampState[c];
            }

            const float g = shape.feedback;
            const float norm = std::sqrt (std::max (0.0f, 1.0f - g * g));
            for (int c = 0; c < channels; ++c)
            {
                // the loop: a rotation of the two sides, times the gain (below 1), plus what comes in
                const float loop = channels == 2 ? (c == 0 ? cs * rung[0] + sn * rung[1] : cs * rung[1] - sn * rung[0]) : rung[0];
                float fed = diffused[c] + g * loop;
                if (! std::isfinite (fed))
                    fed = 0.0f;
                tank[c].write (fed);
                out[c] = rung[c] * norm;
            }
            if (channels == 1)
                out[1] = 0.0f;
            return true;
        }

    private:
        static constexpr int kUpdateSamples = 16;

        void glide (float& value, float goal)
        {
            value += (goal - value) * gainCoefficient;
            if (std::fabs (goal - value) < 1.0e-6f)
                value = goal;
        }

        double sampleRate = 48000.0;
        int channels = 2;
        float knob = 0.0f;
        HaloShape target, shape;
        double tankSeconds = 0.0;
        int countdown = kUpdateSamples;
        bool wasActive = false;
        float gainCoefficient = 0.002f;
        double delayCoefficient = 0.0002;
        float dampCoefficient = 0.5f, hpCoefficient = 0.0f;
        double modPhase[2] = { 0.0, 0.25 }, modStep[2] = { 0.0, 0.0 };
        detail::HaloLine tank[2];
        detail::HaloLine allpass[2][4];
        float dampState[2] = { 0.0f, 0.0f }, hpState[2] = { 0.0f, 0.0f };
    };
}
