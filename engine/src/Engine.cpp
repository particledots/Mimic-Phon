#include <mph/Engine.h>

#include <cmath>

namespace mph
{

void Engine::prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlockSize = newMaxBlockSize > 0 ? newMaxBlockSize : 512;
    numChannels = newNumChannels < 1 ? 1 : (newNumChannels > 2 ? 2 : newNumChannels);

    clock.prepare (sampleRate);
    ratePhase = 0.0;
    tickCount.store (0);
    rateCount.store (0);
}

void Engine::process (float* const* channels, int numChannels_, int numSamples)
{
    // Milestone 2: the audio passes through unchanged. Nothing is read from or written to the channels.
    (void) channels;
    (void) numChannels_;
    (void) numSamples;

    if (params.clear)
        clearHandled.store (true);      // nothing to clear yet; the flag lets the wrapper switch the button off

    // The two counters behind the window's blinking lights. They only count; the audio above is not touched.
    clock.beginBlock (host, core::beatsPerTick (params.clockDivision, host.beatsPerBar));
    for (int i = 0; i < numSamples; ++i)
    {
        clock.tickNow();
        clock.next();
    }
    tickCount.store (clock.ticksSoFar());

    const double delay = params.tempoSync
        ? core::syncDelaySeconds (params.zone, params.rate, clock.beatsPerTickNow() * clock.secondsPerBeat())
        : core::freeDelaySeconds (params.zone, params.rate);
    ratePhase += (double) numSamples / (delay * sampleRate);
    if (ratePhase >= 1.0)
    {
        const double whole = std::floor (ratePhase);
        rateCount.fetch_add ((int) whole);
        ratePhase -= whole;
    }
}

} // namespace mph
