#include <mph/Engine.h>

#include <algorithm>
#include <cmath>

#include <mph/core/Gain.h>

namespace mph
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kRateGlideSeconds = 0.160;        // Rate: how long the delay time takes to settle (the doppler glide)
    constexpr double kLevelGlideSeconds = 0.010;       // levels, Mix, Repeats: no zipper noise when turned
    constexpr double kZoneFadeSeconds = 0.020;         // Zone jump: crossfade between the old and the new read point
    constexpr double kDcBlockerHz = 5.0;
    constexpr float kLevellerCeiling = 0.6f;           // the feedback path is held at this level (clean, instead of distorting)
    constexpr double kLevellerAttackSeconds = 0.002;   // how fast the leveller pulls the loop down when it gets too loud
    constexpr double kLevellerReleaseSeconds = 0.250;  // how slowly it lets go
    constexpr double kBeyondFullDelaySeconds = 0.5;    // "beyond infinite" grows per second, not per echo, below this delay time               // the feedback path lets nothing below this through (no DC build-up at infinite)

    inline float onePoleCoefficient (double seconds, double sampleRate)
    {
        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }
}

void Engine::prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    maxBlockSize = newMaxBlockSize > 0 ? newMaxBlockSize : 512;
    numChannels = newNumChannels < 1 ? 1 : (newNumChannels > 2 ? 2 : newNumChannels);

    clock.prepare (sampleRate);
    ratePhase = 0.0;
    tickCount.store (0);
    rateCount.store (0);

    // One shared buffer per channel, long enough for the longest Zone (41.8 s) with room to spare.
    const int length = (int) std::ceil (kLongestSeconds * sampleRate) + 16;
    for (int c = 0; c < 2; ++c)
        buffers[c].prepare (c < numChannels ? length : 64);

    dcCoefficient = (float) std::exp (-2.0 * kPi * kDcBlockerHz / sampleRate);
    delayCoefficient = onePoleCoefficient (kRateGlideSeconds, sampleRate);
    rampCoefficient = onePoleCoefficient (kLevelGlideSeconds, sampleRate);
    fadeStep = (float) (1.0 / std::max (16.0, kZoneFadeSeconds * sampleRate));

    levelAttack = onePoleCoefficient (kLevellerAttackSeconds, sampleRate);
    levelRelease = onePoleCoefficient (kLevellerReleaseSeconds, sampleRate);
    envRelease = (float) std::exp (-1.0 / (kLevellerReleaseSeconds * sampleRate));

    for (int c = 0; c < 2; ++c)
        dcIn[c] = dcOut[c] = levelEnv[c] = 0.0f, levelGain[c] = 1.0f;
    started = false;
    clearWasOn = false;
    lastZone = -1;
    current = 0;
    fading = false;
    fadePosition = 0.0f;

    publishTargets();
}

void Engine::setParams (const Params& p)
{
    params = p;
    params.zone = std::min (kNumZones - 1, std::max (0, params.zone));
    params.inputGainStep = std::min (kNumInputGainSteps - 1, std::max (0, params.inputGainStep));
    publishTargets();
}

double Engine::currentDelaySeconds()
{
    if (kTempoSyncAudible && params.tempoSync)
        return core::syncDelaySeconds (params.zone, params.rate, clock.beatsPerTickNow() * clock.secondsPerBeat());
    return core::freeDelaySeconds (params.zone, params.rate);
}

// What the window and the host get to know: the delay time aimed at, the feedback, the tail.
void Engine::publishTargets()
{
    const double delay = currentDelaySeconds();
    const float fb = core::repeatsToFeedback (params.repeats);
    delayShown.store ((float) delay);
    feedbackShown.store (fb);
    tailShown.store ((float) core::tailSeconds (delay, fb));
}

// Everything written so far reads as silence from now on; the feedback path's memory is emptied too.
void Engine::clearRepeats()
{
    for (auto& b : buffers)
        b.clearAll();
    for (int c = 0; c < 2; ++c)
        dcIn[c] = dcOut[c] = levelEnv[c] = 0.0f, levelGain[c] = 1.0f;
}

void Engine::process (float* const* channels, int numChannels_, int numSamples)
{
    const int channelCount = std::min (numChannels_, numChannels);

    if (params.clear)
    {
        clearHandled.store (true);              // the wrapper switches the button off a moment later
        if (! clearWasOn)
            clearRepeats();                     // acts once, when the button goes on
    }
    clearWasOn = params.clear;

    // ---- the two counters behind the window's blinking lights (they only count) ----
    clock.beginBlock (host, core::beatsPerTick (params.clockDivision, host.beatsPerBar));
    for (int i = 0; i < numSamples; ++i)
    {
        clock.tickNow();
        clock.next();
    }
    tickCount.store (clock.ticksSoFar());

    const double delaySeconds = currentDelaySeconds();
    ratePhase += (double) numSamples / (delaySeconds * sampleRate);
    if (ratePhase >= 1.0)
    {
        const double whole = std::floor (ratePhase);
        rateCount.fetch_add ((int) whole);
        ratePhase -= whole;
    }
    publishTargets();

    if (numSamples <= 0 || channelCount <= 0 || channels == nullptr)
        return;

    // ---- this block's settings ----
    const float mixAngle = (float) (std::min (1.0f, std::max (0.0f, params.mix)) * kPi * 0.5);
    inputGain.target  = dsp::levelKnobToGain (params.inputLevel) * dsp::dbToGain (kInputGainStepDb[params.inputGainStep]);
    outputGain.target = dsp::levelKnobToGain (params.outputLevel);
    dryGain.target    = std::cos (mixAngle);
    wetGain.target    = std::sin (mixAngle);
    {
        // Beyond infinite grows per second rather than per echo, so short delays do not run away in a moment.
        float fb = core::repeatsToFeedback (params.repeats);
        if (fb > 1.0f)
            fb = 1.0f + (fb - 1.0f) * (float) std::min (1.0, delaySeconds / kBeyondFullDelaySeconds);
        feedback.target = fb;
    }

    // The two read points. Rate glides the audible one; a new Zone is a jump, so it goes to the other one and crossfades.
    const double logLive = std::log (delaySeconds);
    if (! started)
    {
        inputGain.snap (inputGain.target);
        outputGain.snap (outputGain.target);
        dryGain.snap (dryGain.target);
        wetGain.snap (wetGain.target);
        feedback.snap (feedback.target);
        heads[0].logDelay = heads[0].logTarget = logLive;
        heads[1] = heads[0];
        current = 0;
        fading = false;
        lastZone = params.zone;
        started = true;
    }
    else if (params.zone != lastZone)
    {
        lastZone = params.zone;
        Head& incoming = heads[1 - current];
        incoming.logDelay = incoming.logTarget = logLive;          // the new Zone's time, at once (no glide, so no pitch effect)
        if (! fading)
        {
            fading = true;
            fadePosition = 0.0f;
        }
    }
    if (fading)
    {
        heads[current].logTarget = heads[current].logDelay;        // the old read point holds its time while it fades out
        heads[1 - current].logTarget = logLive;
    }
    else
        heads[current].logTarget = logLive;

    const double minDelay = dsp::DelayBuffer::kMinDelaySamples;
    const double maxDelay = buffers[0].maxDelaySamples();

    // ---- the sound ----
    for (int n = 0; n < numSamples; ++n)
    {
        const float inG = inputGain.next (rampCoefficient), outG = outputGain.next (rampCoefficient);
        const float dryG = dryGain.next (rampCoefficient), wetG = wetGain.next (rampCoefficient);
        const float fbG = feedback.next (rampCoefficient);

        Head& hc = heads[current];
        hc.logDelay += (hc.logTarget - hc.logDelay) * (double) delayCoefficient;
        const double delayCurrent = std::min (maxDelay, std::max (minDelay, std::exp (hc.logDelay) * sampleRate));

        double delayIncoming = delayCurrent;
        if (fading)
        {
            Head& hi = heads[1 - current];
            hi.logDelay += (hi.logTarget - hi.logDelay) * (double) delayCoefficient;
            delayIncoming = std::min (maxDelay, std::max (minDelay, std::exp (hi.logDelay) * sampleRate));
        }

        for (int c = 0; c < channelCount; ++c)
        {
            float x = channels[c][n];
            if (! std::isfinite (x))
                x = 0.0f;                                          // a bad sample must never get into the feedback
            x *= inG;

            float wet = buffers[c].read (delayCurrent);
            if (fading)
                wet += fadePosition * (buffers[c].read (delayIncoming) - wet);

            // What is fed back: the repeat, minus any DC, scaled by Repeats, kept from running away by the soft limiter.
            float blocked = wet - dcIn[c] + dcCoefficient * dcOut[c];
            dcIn[c] = wet;
            if (std::fabs (blocked) < 1.0e-20f)
                blocked = 0.0f;
            dcOut[c] = blocked;
            // The leveller keeps the loop at a steady, clean level when it grows (beyond infinite, or loud material at infinite);
            // the soft limiter is only the last safety.
            float fed = blocked * fbG;
            const float mag = std::fabs (fed);
            levelEnv[c] = std::max (mag, levelEnv[c] * envRelease);
            const float wanted = levelEnv[c] > kLevellerCeiling ? kLevellerCeiling / levelEnv[c] : 1.0f;
            levelGain[c] += (wanted - levelGain[c]) * (wanted < levelGain[c] ? levelAttack : levelRelease);
            fed *= levelGain[c];
            buffers[c].write (x + dsp::softLimit (fed));

            channels[c][n] = (dryG * x + wetG * wet) * outG;
        }

        if (fading)
        {
            fadePosition += fadeStep;
            if (fadePosition >= 1.0f)
            {
                current = 1 - current;
                fading = false;
                fadePosition = 0.0f;
            }
        }
    }
}

} // namespace mph
