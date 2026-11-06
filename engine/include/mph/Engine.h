// The Mimic-Phon engine. Plain C++17, no JUCE, no plugin code.
//
// Milestone 2: an empty engine. process() leaves the audio exactly as it is (bit for bit). It already takes the control
// values and Logic's tempo, so the wrapper is complete; the repeater itself arrives in milestone 4 onwards.
// The one control it acts on is Clear: it notes that the button went on (so the wrapper can switch the button back off).
//
// Milestone 3 adds things for the window only (none of them touches the audio): Logic's tempo as last heard, and two
// counters for the blinking lights: one goes up at every tick of the tempo clock (the TICK light), the other every time the
// current delay time has passed (the RATE light, the stand-in for the hardware's Rate Out pulse).

#pragma once

#include <atomic>

#include "Params.h"
#include "core/HostClock.h"
#include "core/ZoneMap.h"

namespace mph
{
    class Engine
    {
    public:
        // Called when playback is set up (and again if the sample rate or block size changes).
        void prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels);

        // Called once per block, before process().
        void setParams (const Params& p) { params = p; }
        void setHost (const core::HostInfo& h)
        {
            host = h;
            bpmShown.store (h.tempoValid ? (float) h.bpm : 0.0f);
            beatsPerBarShown.store ((float) h.beatsPerBar);
        }

        // Works in place on the channels (1 or 2). Milestone 2: no change to the audio.
        void process (float* const* channels, int numChannels, int numSamples);

        // True once after Clear went on; the wrapper then switches the Clear button off (checked a few times a second).
        bool takeClearHandled() { return clearHandled.exchange (false); }

        // For the window (read from the window thread; they never affect the sound).
        float getHostBpm() const { return bpmShown.load(); }                  // 0 = Logic has not given a tempo
        float getHostBeatsPerBar() const { return beatsPerBarShown.load(); }
        int getClockTickCount() const { return tickCount.load(); }            // +1 at every tick of the tempo clock
        int getRateTickCount() const { return rateCount.load(); }             // +1 every time the current delay time has passed

        double getSampleRate() const { return sampleRate; }
        int getMaxBlockSize() const { return maxBlockSize; }
        const Params& getParams() const { return params; }

    private:
        double sampleRate = 48000.0;
        int maxBlockSize = 512;
        int numChannels = 2;
        Params params;
        core::HostInfo host;
        std::atomic<bool> clearHandled { false };

        // Window-only things (milestone 3)
        core::HostClock clock;
        double ratePhase = 0.0;                            // how far through the current delay time the RATE light is (0..1)
        std::atomic<float> bpmShown { 0.0f };
        std::atomic<float> beatsPerBarShown { 4.0f };
        std::atomic<int> tickCount { 0 };
        std::atomic<int> rateCount { 0 };
    };
}
