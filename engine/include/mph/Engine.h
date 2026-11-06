// The Mimic-Phon engine. Plain C++17, no JUCE, no plugin code.
//
// Milestone 4: the core repeater. What works now, and what does not:
//
//   Works:   Input Level and the Input Gain step, Zone, Rate, Repeats, Mix, Output Level, Clear.
//   Not yet: Color (5), Skew / Ping Pong / Swap (6), uRate and its LFO (7), Tempo Sync (8), Halo (9), Flip (10), Hold (11).
//            Those controls are read but do nothing; the left and right channels are independent until Skew and Ping Pong.
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
//
// Window-only things from milestone 3 (never affect the sound): Logic's tempo as last heard, and two counters for the blinking
// lights: one goes up at every tick of the tempo clock (TICK), the other every time the current delay time has passed (RATE).

#pragma once

#include <atomic>
#include <cmath>

#include "Params.h"
#include "core/DelayBuffer.h"
#include "core/HostClock.h"
#include "core/Repeats.h"
#include "core/ZoneMap.h"

namespace mph
{
    // Tempo Sync is built in milestone 8. Until then the Rate knob always sets a free-running delay time, in the sound AND in
    // the window's read-outs, whatever the Tempo Sync button says. Milestone 8 switches this on.
    inline constexpr bool kTempoSyncAudible = false;

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

        double currentDelaySeconds();
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
        bool started = false;                              // false until the first block after prepare(): then everything snaps to its setting
        bool clearWasOn = false;
        int lastZone = -1;
        Head heads[2];
        int current = 0;                                   // which read point is the audible one
        bool fading = false;                               // crossfading to the other read point (a Zone change)
        float fadePosition = 0.0f, fadeStep = 0.001f;
        float delayCoefficient = 0.0005f;                  // how fast Rate glides (one-pole, about 80 ms)
        float rampCoefficient = 0.002f;                    // how fast the level controls glide (one-pole, about 10 ms)
        Ramp inputGain, outputGain, dryGain, wetGain, feedback;

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
    };
}
