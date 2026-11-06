// One channel of the shared delay buffer. Plain C++17, no JUCE.
//
// All eight Zones read from this single buffer (plan, decision 1): the incoming sound is always being written into it, and a
// Zone is only a different delay time to read at. The buffer holds at least 41.8 seconds.
//
// Reading uses cubic (Hermite) interpolation, so a delay time that moves smoothly gives a clean doppler effect.
//
// "Clear" does not wipe the memory (that would take a noticeable moment with a buffer this long, and the audio thread must
// never stall). It notes the moment of clearing instead: anything written before that moment reads as silence from then on.
// The result is the same as wiping every Zone at once, instantly.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mph::dsp
{
    class DelayBuffer
    {
    public:
        // The shortest delay that can be read cleanly (the cubic needs three samples between the write point and the read point)
        static constexpr double kMinDelaySamples = 4.0;

        void prepare (int lengthSamples)
        {
            length = std::max (lengthSamples, 64);
            memory.assign ((size_t) length, 0.0f);
            writeCount = 0;
            clearedAt = 0;
        }

        int capacity() const { return length; }
        double maxDelaySamples() const { return (double) (length - 8); }

        // Add the newest sample.
        void write (float x)
        {
            memory[(size_t) (writeCount % length)] = x;
            ++writeCount;
        }

        // Everything written so far now reads as silence.
        void clearAll() { clearedAt = writeCount; }

        // The sound that went in "delaySamples" ago (1 = the sample just written), between samples if need be.
        // Call before write() for the current sample. delaySamples must be between kMinDelaySamples and maxDelaySamples().
        float read (double delaySamples) const
        {
            const double position = (double) writeCount - delaySamples;
            const double whole = std::floor (position);
            const std::int64_t i = (std::int64_t) whole;
            const float f = (float) (position - whole);

            const float xm1 = tap (i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

    private:
        float tap (std::int64_t absoluteIndex) const
        {
            return absoluteIndex >= clearedAt ? memory[(size_t) (absoluteIndex % length)] : 0.0f;
        }

        std::vector<float> memory;
        int length = 64;
        std::int64_t writeCount = 0;       // how many samples have been written since prepare()
        std::int64_t clearedAt = 0;        // samples with a smaller absolute index than this read as silence
    };
}
