// Skew, Ping Pong and Swap: the rules, with no audio in them. Plain C++17, no JUCE.
//
// The engine uses these to aim the left and right read points, and the window uses the very same functions for its Left / Right
// delay read-out, so the number on the screen is the delay that is heard.
//
// What the module does (manual p. 11, and what Bryn heard on his own module):
//   * Skew on: Rate no longer moves both sides together. It pulls the Left and Right delay times apart, in opposite directions.
//     Rate clockwise = Left faster (shorter delay), Right slower (longer delay). Rate at 12:00 = no skew.
//   * Skew off again: the split stays as it was, and Rate moves both sides together from there (nothing jumps when the button is
//     pressed). Skew on and off once with Rate untouched clears the split and brings the two sides back together.
//   * Ping Pong (button on, sides not skewed): the Left and Right inputs are summed to mono and go into the left side only; the
//     repeats bounce right, left, right ... The delay time is split between the two sides (each hop is half the time).
//   * Swap (button on, sides skewed): the left and right feedback paths are swapped (what repeats on the left feeds the right
//     side and the other way round); the inputs stay stereo and the delay times stay as they are.
//
// How this is modelled: knob positions (0..1) are worked out for each side, then turned into a time by the Zone mapping.
//   centre   = the common position of the two sides (the Rate knob, plus whatever shift Skew left behind)
//   skew     = how far apart they are, in knob units: Left = centre - skew, Right = centre + skew (each kept inside 0..1)
// With Skew on, centre is held (the Rate knob's position at the moment Skew went on) and the Rate knob sets the skew:
//   skew = (Rate - 0.5) * kSkewSwing, so at the far right the sides are half the knob's travel either side of the centre.

#pragma once

#include <algorithm>
#include <cmath>

namespace mph::core
{
    // How much split the Rate knob gives with Skew on. Full travel from 12:00 gives +-0.5 * kSkewSwing in knob units.
    // 1.0: with the centre in the middle, the far right puts Left at the Zone's shortest delay and Right at its longest (a factor of 4).
    inline constexpr float kSkewSwing = 1.0f;

    // Rate counts as "untouched" while Skew was on if it moved no more than this (knob units). Then switching Skew off clears the split.
    inline constexpr float kSkewTouch = 0.002f;

    // The sides count as skewed above kSkewedOn and go back to "not skewed" below kSkewedOff (a small gap so it does not flutter).
    // Skewed decides whether the button means Swap (skewed) or Ping Pong (not skewed).
    inline constexpr float kSkewedOn = 0.01f;
    inline constexpr float kSkewedOff = 0.005f;

    // Ping Pong: each hop is this share of the delay time the Zone and Rate give (the delay time is split between the two sides).
    inline constexpr double kPingPongHop = 0.5;

    inline float clamp01 (float x) { return std::min (1.0f, std::max (0.0f, x)); }

    // The skew the Rate knob gives while Skew is on (positive = Left faster).
    inline float skewFromRate (float rate) { return (clamp01 (rate) - 0.5f) * kSkewSwing; }

    struct ChannelRates { float left = 0.5f, right = 0.5f; };      // knob positions (0..1) for the two sides

    class SkewState
    {
    public:
        bool engaged = false;          // the Skew button is on
        bool skewed = false;           // the two sides are noticeably different (with hysteresis)
        float centre = 0.5f;           // held while engaged: where the two sides are centred
        float held = 0.0f;             // the split kept after Skew goes off
        float shift = 0.0f;            // how far the Rate knob's tandem position is from the knob after Skew went off (keeps things from jumping)

        // Where the two sides are centred right now, and how far apart they are.
        float baseNow (float rate) const { return engaged ? centre : clamp01 (rate + shift); }
        float skewNow (float rate) const { return engaged ? skewFromRate (rate) : held; }

        ChannelRates ratesNow (float rate) const
        {
            const float c = baseNow (rate), k = skewNow (rate);
            return { clamp01 (c - k), clamp01 (c + k) };
        }

        // Called once per block with the Skew button and the Rate knob.
        void update (bool button, float rate)
        {
            rate = clamp01 (rate);
            const ChannelRates before = ratesNow (rate);

            if (button && ! buttonWas)
            {
                centre = baseNow (rate);           // the position the two sides are centred on from now on
                rateAtEngage = rate;
                engaged = true;
            }
            else if (! button && buttonWas)
            {
                const float k = skewNow (rate);
                const bool untouched = std::fabs (rate - rateAtEngage) <= kSkewTouch;
                engaged = false;
                if (untouched)
                {
                    held = 0.0f;                   // on and off with Rate left alone: clear the split, back in step with the knob
                    shift = 0.0f;
                }
                else
                {
                    held = k;                      // keep the split; Rate moves both sides from where they are now
                    shift = centre - rate;
                }
            }
            buttonWas = button;

            const float magnitude = std::fabs (skewNow (rate));
            skewed = skewed ? magnitude > kSkewedOff : magnitude > kSkewedOn;

            // A button press that makes the two delay times jump (Skew on away from 12:00, or a clear) is a jump, not a glide:
            // the engine crossfades to it instead of bending the pitch.
            const ChannelRates after = ratesNow (rate);
            if (std::fabs (after.left - before.left) > 0.0005f || std::fabs (after.right - before.right) > 0.0005f)
                jumpPending = true;
        }

        // True once after something made the delay times jump.
        bool takeJump()
        {
            const bool j = jumpPending;
            jumpPending = false;
            return j;
        }

    private:
        bool buttonWas = false;
        bool jumpPending = false;
        float rateAtEngage = 0.5f;
    };

    enum class PingMode { off, pingPong, swap };

    // What the one button means right now.
    inline PingMode pingMode (bool buttonOn, bool stereo, const SkewState& s)
    {
        if (! buttonOn || ! stereo)
            return PingMode::off;
        return s.skewed ? PingMode::swap : PingMode::pingPong;
    }

    // The button's label on the panel (and in the checks).
    inline const char* pingLabel (PingMode m)
    {
        switch (m)
        {
            case PingMode::pingPong: return "PING PONG";
            case PingMode::swap:     return "SWAP";
            default:                 return "PING PONG / SWAP";
        }
    }
}
