// The Mimic-Phon engine. Plain C++17, no JUCE, no plugin code.
//
// Milestone 2: an empty engine. process() leaves the audio exactly as it is (bit for bit). It already takes the control
// values and Logic's tempo, so the wrapper is complete; the repeater itself arrives in milestone 4 onwards.
// The one control it acts on is Clear: it notes that the button went on (so the wrapper can switch the button back off).

#pragma once

#include <atomic>

#include "Params.h"
#include "core/HostClock.h"

namespace mph
{
    class Engine
    {
    public:
        // Called when playback is set up (and again if the sample rate or block size changes).
        void prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels);

        // Called once per block, before process().
        void setParams (const Params& p) { params = p; }
        void setHost (const core::HostInfo& h) { host = h; }

        // Works in place on the channels (1 or 2). Milestone 2: no change to the audio.
        void process (float* const* channels, int numChannels, int numSamples);

        // True once after Clear went on; the wrapper then switches the Clear button off (checked a few times a second).
        bool takeClearHandled() { return clearHandled.exchange (false); }

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
    };
}
