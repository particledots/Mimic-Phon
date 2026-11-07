// The Mimic-Phon engine. Plain C++17, no JUCE, no plugin code.
//
// Milestone 8: the core repeater (milestone 4), Color (5), Skew, Ping Pong and Swap (6), uRate with its LFO (7), and Tempo Sync of Rate.
// What works now, and what does not:
//
//   Works:   Input Level and the Input Gain step, Zone, Rate, Repeats, Mix, Output Level, Clear, Color, Skew, Ping Pong / Swap,
//            uRate and the uRate LFO (Rate, Depth, Sine / Triangle, Sync to Logic's tempo), Tempo Sync of Rate.
//   Not yet: Halo (9), Flip (10), Hold (11).
//            Those controls are read but do nothing.
//
// How it sounds, in short: the input is written into one shared stereo buffer (core/DelayBuffer.h, 41.8 s or more) that serves
// all eight Zones. The repeat is the buffer read back at the delay time that Zone and Rate give (core/ZoneMap.h, the same
// function the window uses for its read-out). The repeat is also fed back into the buffer by the Repeats amount
// (core/Repeats.h), through a DC blocker and the soft limiter, so it can go to infinite and beyond and saturate, never run away.
//
//   * Rate moves the delay time smoothly inside the Zone: the read point glides, so there is a doppler effect on the repeats.
//   * Zone jumps: the new Zone's delay time is read by a second read point and the two are crossfaded (20 ms), so there is no
//     pitch effect. Changing Zone again during a crossfade re-aims the incoming read point (a tiny discontinuity, in theory).
//   * Mix: equal-power balance between the live signal (after Input Level / gain) and the repeats; 0 = all live, 1 = all repeats.
//   * Clear: everything written so far reads as silence from that moment (all Zones at once, no pause in the audio).
//   * Color (core/Color.h): a saturation and filter character applied to everything written into the buffer (live signal plus
//     feedback), so each echo is shaped once more than the last. Left of unity: dark (saturation and a closing low-pass, down to
//     "oil can"); at unity (about 3:00): nothing, bit for bit; right of unity: thin (a rising high-pass and a little fizz).
//   * Skew (core/Skew.h): with Skew on, Rate pulls the left and right delay times apart (clockwise: left faster, right slower; 12:00 =
//     none). Each side has its own read points, so a Zone change or a button press that makes the times jump crossfades on both.
//   * Ping Pong (button on, sides not skewed): the inputs are summed to mono into the left side only, the feedback of each side goes
//     into the other, and each hop is half the delay time, so the repeats bounce left, right, left ... Swap (button on, sides
//     skewed): the left and right feedback paths are swapped, inputs stay stereo, times stay as Skew set them.
//   * uRate (core/URate.h): a doppler-only offset on the read points, added AFTER Rate's own glide so it is quick: a steady offset of up
//     to 6 ms either way in Zones 1-7 (clockwise = shorter delay = pitch up), one octave either way in Zone 0 (1 V/oct, Karplus pitch).
//     The LFO swings around it (0.1 - 20 Hz, or a note division with Sync, locked to Logic's song position while it plays); its Depth is on
//     a squared curve, shallow at first and very deep at the far end. With sides split by Skew the right side is opposite in Zones 1-7
//     (stereo chorus); Zone 0 ignores Skew. With uRate at the middle and Depth at zero nothing is touched, bit for bit.
//
//   * Tempo Sync (core/ZoneMap.h): with the button on, Rate no longer sets a free-running time; its knob picks one of 13 steps of the clock
//     (x2 on the left, 1/1 near the middle, /2 on the right) and the Clock Division menu says what the clock is (Logic's tempo at that
//     note value). The Zone then doubles or halves the time per Zone away from the Zone that fits the clock best. A step is a JUMP, not a
//     glide: the engine crossfades to the new time (the same 20 ms crossfade as a Zone change), so there is no pitch bend. The same goes for a
//     new Zone, a new Clock Division, the button itself, and the best-fitting Zone changing with the tempo. With Skew the two sides pick their
//     own steps (two multiples at once). A tempo that merely drifts is followed by the normal Rate glide. uRate and its LFO still bend the pitch.
//
// Window-only things from milestone 3 (never affect the sound): Logic's tempo as last heard, and two counters for the blinking
// lights: one goes up at every tick of the tempo clock (TICK), the other every time the current delay time has passed (RATE).

#pragma once

#include <atomic>
#include <cmath>

#include "Params.h"
#include "core/Color.h"
#include "core/DelayBuffer.h"
#include "core/HostClock.h"
#include "core/Repeats.h"
#include "core/Skew.h"
#include "core/URate.h"
#include "core/ZoneMap.h"

namespace mph
{
    // Tempo Sync (milestone 8). When false the Rate knob always sets a free-running delay time, in the sound AND in the window's
    // read-outs, whatever the Tempo Sync button says. Built in milestone 8 and switched on there.
    inline constexpr bool kTempoSyncAudible = true;

    class Engine
    {
    public:
        // Called when playback is set up (and again if the sample rate or block size changes). Allocates the buffers.
        void prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels);

        // Called once per block, before process().
        void setParams (const Params& p);
        void setHost (const core::HostInfo& h)
        {
            host = h;
            bpmShown.store (h.tempoValid ? (float) h.bpm : 0.0f);
            beatsPerBarShown.store ((float) h.beatsPerBar);
        }

        // Works in place on the channels (1 or 2).
        void process (float* const* channels, int numChannels, int numSamples);

        // True once after Clear went on; the wrapper then switches the Clear button off (checked a few times a second).
        bool takeClearHandled() { return clearHandled.exchange (false); }

        // For the window and the host (read from other threads; they never affect the sound).
        float getHostBpm() const { return bpmShown.load(); }                  // 0 = Logic has not given a tempo
        float getHostBeatsPerBar() const { return beatsPerBarShown.load(); }
        int getClockTickCount() const { return tickCount.load(); }            // +1 at every tick of the tempo clock
        int getRateTickCount() const { return rateCount.load(); }             // +1 every time the current delay time has passed
        float getDelaySeconds() const { return delayShown.load(); }           // the delay time the Zone and Rate settings aim at
        float getFeedback() const { return feedbackShown.load(); }            // what the Repeats knob feeds back (1.0 = infinite)
        bool isStereo() const { return numChannels == 2; }                    // Skew and Ping Pong / Swap need two channels
        // The Skew memory as of the last block (the window works out its Left / Right read-out from it and the live Rate knob).
        core::SkewState getSkewState() const
        {
            core::SkewState s;
            s.engaged = skewEngagedShown.load();
            s.skewed = skewSkewedShown.load();
            s.centre = skewCentreShown.load();
            s.held = skewHeldShown.load();
            s.shift = skewShiftShown.load();
            return s;
        }
        double getTailSeconds() const { return (double) tailShown.load(); }   // how long the repeats keep sounding (host "tail")

        double getSampleRate() const { return sampleRate; }
        int getMaxBlockSize() const { return maxBlockSize; }
        const Params& getParams() const { return params; }

    private:
        struct Head { double logDelay = 0.0; double logTarget = 0.0; };       // a read point: its delay time (log of seconds)
        struct Ramp
        {
            double value = 0.0, target = 0.0;      // double precision: a single-precision glide stalls just short of its target
            void snap (double v) { value = target = v; }
            // One step of the glide. It lands exactly on the target, so a control left alone is exactly at its setting:
            // Mix fully left is exactly the live signal, Output Level at 0 is exactly silent.
            float next (double coefficient)
            {
                value += (target - value) * coefficient;
                if (std::fabs (target - value) < 1.0e-7)
                    value = target;
                return (float) value;
            }
        };

        double delayForRate (float rate01);                // the delay time a knob position gives (free running, or synced from milestone 8)
        double currentDelaySeconds();                      // the Rate knob's own delay time (no skew, no ping pong hop)
        double clockPeriod() const;                        // seconds between ticks of the Clock Division (Logic's tempo, or 120 BPM)
        core::ChannelRates sideRates();                    // the two sides' Rate knob positions (Skew moves them apart)
        struct SideDelays { double left, right; };
        SideDelays sideDelays();                           // the two sides' delay times, with Skew and the Ping Pong hop in them
        bool pingHop() const { return numChannels == 2 && params.pingPong && ! skew.skewed; }
        void publishTargets();
        void clearRepeats();

        double sampleRate = 48000.0;
        int maxBlockSize = 512;
        int numChannels = 2;
        Params params;
        core::HostInfo host;
        std::atomic<bool> clearHandled { false };

        // The repeater
        dsp::DelayBuffer buffers[2];
        float dcIn[2] = { 0.0f, 0.0f }, dcOut[2] = { 0.0f, 0.0f };      // the DC blocker in the feedback path (one per channel)
        float dcCoefficient = 0.9993f;
    float levelEnv[2] = { 0.0f, 0.0f }, levelGain[2] = { 1.0f, 1.0f };   // the leveller in the feedback path (one per channel)
    float levelRelease = 0.0001f, envRelease = 0.9999f;
        bool started = false;                              // false until the first block after prepare(): then everything snaps to its setting
        bool clearWasOn = false;
        int lastZone = -1;
        Head heads[2][2];                                  // [channel][which read point]
        int current = 0;                                   // which read point is the audible one (the same for both channels)
        bool fading = false;                               // crossfading to the other read point (a Zone change, or any jump)
        bool lastPingHop = false;
        // What Tempo Sync is pointing at, per side. When it changes (a new step, Zone, Clock Division, the button, or the best-fitting
        // Zone moving with the tempo) the engine jumps with a crossfade instead of gliding. Everything is -1 while Tempo Sync is off.
        struct SyncKey
        {
            int step[2] = { -1, -1 };
            int division = -1;
            int reference = -1;
            bool operator== (const SyncKey& o) const
            {
                return step[0] == o.step[0] && step[1] == o.step[1] && division == o.division && reference == o.reference;
            }
            bool operator!= (const SyncKey& o) const { return ! (*this == o); }
        };
        SyncKey lastSyncKey;
        float fadePosition = 0.0f, fadeStep = 0.001f;
        float delayCoefficient = 0.0005f;                  // how fast Rate glides (one-pole, about 160 ms)
        float rampCoefficient = 0.002f;                    // how fast the level controls glide (one-pole, about 10 ms)
        Ramp inputGain, outputGain, dryGain, wetGain, feedback;

        // Skew, Ping Pong, Swap (milestone 6)
        core::SkewState skew;
        Ramp crossing, monoSum;                            // 1 = feedback paths crossed (Ping Pong / Swap); 1 = inputs summed into the left side (Ping Pong)
        double ratePhaseRight = 0.0;                       // the RATE light follows both sides when they differ

        // uRate and its LFO (milestone 7). Each read point remembers the Zone it was aimed at (the knob means pitch in Zone 0, time in
        // the others), so a Zone jump does not change how the old read point is modulated while it fades out.
        int headZone[2] = { kDefaultZone, kDefaultZone };  // [which read point]
        Ramp uRateKnob;                                    // the knob as -1..+1 (core::uRateBipolar), glides about 10 ms
        Ramp lfoSeconds, lfoSemitones;                     // the LFO's peak swing (Zones 1-7 in seconds, Zone 0 in semitones)
        Ramp rightSide;                                    // +1, or -1 for the right side when Skew has split the sides (Zones 1-7)
        double lfoPhase = 0.0;                             // 0..1
        double lfoStep = 0.0;                              // phase added per sample
        double lfoSmoothed = 0.0;                          // the LFO's output, smoothed by about 2 ms (a jump of phase makes no click)
        double lfoSmoothCoefficient = 0.01;

        // Color (milestone 5): the knob glides (about 30 ms) and the filter's coefficients follow every 16 samples
        core::ColorFilter colorFilter;
        Ramp colorKnob;
        float colorCoefficient = 0.0007f;
        float colorApplied = -1.0f;                        // the knob value the filter is currently set for
        int colorCountdown = 0;

        // Window-only things (milestone 3)
        core::HostClock clock;
        double ratePhase = 0.0;                            // how far through the current delay time the RATE light is (0..1)
        std::atomic<float> bpmShown { 0.0f };
        std::atomic<float> beatsPerBarShown { 4.0f };
        std::atomic<int> tickCount { 0 };
        std::atomic<int> rateCount { 0 };
        std::atomic<float> delayShown { 0.6531f };
        std::atomic<float> feedbackShown { 0.0f };
        std::atomic<float> tailShown { 0.0f };
        std::atomic<bool> skewEngagedShown { false }, skewSkewedShown { false };
        std::atomic<float> skewCentreShown { 0.5f }, skewHeldShown { 0.0f }, skewShiftShown { 0.0f };
    };
}
