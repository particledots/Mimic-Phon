#include <mph/Engine.h>

namespace mph
{

void Engine::prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlockSize = newMaxBlockSize > 0 ? newMaxBlockSize : 512;
    numChannels = newNumChannels < 1 ? 1 : (newNumChannels > 2 ? 2 : newNumChannels);
}

void Engine::process (float* const* channels, int numChannels_, int numSamples)
{
    // Milestone 2: the audio passes through unchanged. Nothing is read from or written to the channels.
    (void) channels;
    (void) numChannels_;
    (void) numSamples;

    if (params.clear)
        clearHandled.store (true);      // nothing to clear yet; the flag lets the wrapper switch the button off
}

} // namespace mph
