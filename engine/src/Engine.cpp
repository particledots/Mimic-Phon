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
    constexpr double kLfoSmoothSeconds = 0.002;        // uRate LFO: its output is smoothed a little, so a jump of phase makes no click
    constexpr double kColorGlideSeconds = 0.030;       // Color: how fast the knob's effect settles (no clicks when it is turned)
    constexpr int kColorUpdateSamples = 16;            // how often the Color filter's coefficients follow the knob
    constexpr double kDcBlockerHz = 5.0;
    constexpr float kLevellerCeiling = 0.6f;           // the feedback path is held at this level (clean, instead of distorting)
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
    colorCoefficient = onePoleCoefficient (kColorGlideSeconds, sampleRate);
    lfoSmoothCoefficient = 1.0 - std::exp (-1.0 / (kLfoSmoothSeconds * sampleRate));
    lfoPhase = 0.0;
    lfoStep = 0.0;
    lfoSmoothed = 0.0;
    headZone[0] = headZone[1] = kDefaultZone;
    colorFilter.prepare (sampleRate);
    colorApplied = -1.0f;
    colorCountdown = 0;
    halo.prepare (sampleRate, numChannels);
    for (int c = 0; c < 2; ++c)
    {
        flipVoices[c].prepare (sampleRate);
        dcInH[c] = dcOutH[c] = 0.0f;
    }
    flipAmount.snap (0.0);
    flipWasActive = false;
    holdColorFilter.prepare (sampleRate);
    holdAmount.snap (0.0);
    holdSoft.snap (0.0);
    holdSwap.snap (0.0);
    holdSwapSide = false;
    holdOn = false;
    holdWasActive = false;
    holdEnd = 0;
    for (int c = 0; c < 2; ++c)
        holdHeads[c][0] = holdHeads[c][1] = core::HoldHead();
    holdScrubCoefficient = onePoleCoefficient (core::kHoldScrubGlideSeconds, sampleRate);

    levelRelease = onePoleCoefficient (kLevellerReleaseSeconds, sampleRate);
    envRelease = (float) std::exp (-1.0 / (kLevellerReleaseSeconds * sampleRate));

    for (int c = 0; c < 2; ++c)
        dcIn[c] = dcOut[c] = levelEnv[c] = 0.0f, levelGain[c] = 1.0f;
    started = false;
    clearWasOn = false;
    lastZone = -1;
    lastPingHop = false;
    lastSyncKey = SyncKey();
    current = 0;
    fading = false;
    fadePosition = 0.0f;
    skew = core::SkewState();
    ratePhaseRight = 0.0;

    publishTargets();
}

void Engine::setParams (const Params& p)
{
    params = p;
    params.zone = std::min (kNumZones - 1, std::max (0, params.zone));
    params.inputGainStep = std::min (kNumInputGainSteps - 1, std::max (0, params.inputGainStep));
    skew.update (params.skew, params.rate);          // the Skew button and Rate: engages, holds or clears the split (once per block)
    publishTargets();
}

// The clock the Clock Division menu stands for: Logic's tempo at that note value (120 BPM when Logic gives none). The window calls
// the same core function with the same inputs.
double Engine::clockPeriod() const
{
    return core::clockPeriodSeconds (params.clockDivision, host.beatsPerBar, host.tempoValid ? host.bpm : 0.0);
}

double Engine::delayForRate (float rate01)
{
    if (kTempoSyncAudible && params.tempoSync)
        return core::syncDelaySeconds (params.zone, rate01, clockPeriod());
    return core::freeDelaySeconds (params.zone, rate01);
}

// The Rate knob position each side listens to: both the knob itself, or pulled apart by Skew (two channels only).
core::ChannelRates Engine::sideRates()
{
    core::ChannelRates r;
    r.left = r.right = params.rate;
    if (numChannels == 2)
        r = skew.ratesNow (params.rate);
    return r;
}

double Engine::currentDelaySeconds()
{
    return delayForRate (params.rate);
}

// The two sides' delay times. Skew moves them apart; the Ping Pong hop halves both (each bounce is half the delay time).
Engine::SideDelays Engine::sideDelays()
{
    const core::ChannelRates r = sideRates();
    SideDelays d { delayForRate (r.left), delayForRate (r.right) };
    if (pingHop())
    {
        d.left *= core::kPingPongHop;
        d.right *= core::kPingPongHop;
    }
    return d;
}

// What the window and the host get to know: the delay time aimed at, the feedback, the tail, the Skew memory.
void Engine::publishTargets()
{
    const double delay = currentDelaySeconds();
    const SideDelays sides = sideDelays();
    const float fb = core::repeatsToFeedback (params.repeats);
    delayShown.store ((float) delay);
    feedbackShown.store (fb);
    // Flip: the last echo is heard backwards one chunk after it, so the tail is one chunk longer
    const double flipExtra = params.flip ? std::min (core::kFlipMaxChunkSeconds, std::max (sides.left, sides.right)) : 0.0;
    // Hold: the loop never dies away, so the host is told the longest tail it will accept
    const double tail = params.hold ? core::kTailCapSeconds
                                    : core::tailSeconds (std::max (sides.left, sides.right), fb) + core::haloTailSeconds (params.halo) + flipExtra;
    tailShown.store ((float) tail);
    skewEngagedShown.store (skew.engaged);
    skewSkewedShown.store (skew.skewed);
    skewCentreShown.store (skew.centre);
    skewHeldShown.store (skew.held);
    skewShiftShown.store (skew.shift);
}

// Everything written so far reads as silence from now on; the feedback path's memory is emptied too.
void Engine::clearRepeats()
{
    for (auto& b : buffers)
        b.clearAll();
    for (int c = 0; c < 2; ++c)
        dcIn[c] = dcOut[c] = dcInH[c] = dcOutH[c] = levelEnv[c] = 0.0f, levelGain[c] = 1.0f;
    colorFilter.reset();
    holdColorFilter.reset();
    halo.reset();
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

    // The RATE light: one blink per delay time of the Rate knob's own time; with the two sides skewed, a blink for each side.
    {
        const core::ChannelRates r = sideRates();
        const float rateLeft = r.left, rateRight = r.right;
        ratePhase += (double) numSamples / (delayForRate (rateLeft) * sampleRate);
        if (ratePhase >= 1.0)
        {
            const double whole = std::floor (ratePhase);
            rateCount.fetch_add ((int) whole);
            ratePhase -= whole;
        }
        if (rateRight != rateLeft)
        {
            ratePhaseRight += (double) numSamples / (delayForRate (rateRight) * sampleRate);
            if (ratePhaseRight >= 1.0)
            {
                const double whole = std::floor (ratePhaseRight);
                rateCount.fetch_add ((int) whole);
                ratePhaseRight -= whole;
            }
        }
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
    const SideDelays sides = sideDelays();
    const bool stereoRouting = numChannels == 2 && channelCount == 2;
    const bool pingHopNow = pingHop() && stereoRouting;
    crossing.target = (params.pingPong && stereoRouting) ? 1.0 : 0.0;     // Ping Pong and Swap both cross the feedback paths
    monoSum.target = pingHopNow ? 1.0 : 0.0;                               // only Ping Pong sums the inputs into the left side
    {
        // Beyond infinite grows per second rather than per echo, so short delays do not run away in a moment.
        const double shortest = std::min (sides.left, sides.right);
        float fb = core::repeatsToFeedback (params.repeats);
        if (fb > 1.0f)
            fb = 1.0f + (fb - 1.0f) * (float) std::min (1.0, shortest / kBeyondFullDelaySeconds);
        feedback.target = fb;
    }
    colorKnob.target = (double) std::min (1.0f, std::max (0.0f, params.color));
    halo.setKnob (params.halo);
    flipAmount.target = params.flip ? 1.0 : 0.0;

    // uRate and its LFO: the knob and the LFO's swing glide (about 10 ms); the LFO runs free (Hz) or in note divisions of Logic's tempo.
    {
        const double bpm = host.tempoValid ? host.bpm : 120.0;
        const double hz = core::lfoHz (params.uRateLfoRate, params.uRateLfoSync, bpm, host.beatsPerBar);
        uRateKnob.target = core::uRateBipolar (params.uRate);
        lfoSeconds.target = core::lfoPeakSeconds (params.uRateLfoDepth, hz);
        lfoSemitones.target = core::lfoPeakSemitones (params.uRateLfoDepth);
        rightSide.target = (stereoRouting && skew.skewed) ? -1.0 : 1.0;
        lfoStep = std::min (0.45, hz / sampleRate);
        if (params.uRateLfoSync && host.playing && host.ppqValid && std::isfinite (host.ppq))
        {
            // Locked to the song: the phase is where the song position puts it (so it lines up after a jump or a loop too).
            const double cycle = core::lfoCycleBeats (params.uRateLfoRate, host.beatsPerBar);
            const double position = host.ppq / cycle;
            lfoPhase = position - std::floor (position);
        }
    }

    // The read points: one pair per side. Rate glides the audible one; a jump (a new Zone, the Ping Pong hop switching on or off, or a
    // Skew button press that moves the times) goes to the other one of the pair and crossfades.
    const double logLive[2] = { std::log (sides.left), std::log (sides.right) };
    const bool skewJump = skew.takeJump();
    bool restartFlip = false;

    // Hold (milestone 11): the button as of this block, where each side's loop should start, and whether that asks for a crossfaded jump
    const bool holdNow = params.hold;
    const bool holdPress = holdNow && ! holdOn;
    const double holdCapSamples = core::kHoldMaxLoopSeconds * sampleRate;
    auto holdLoopClamp = [&] (double samples) { return std::min (holdCapSamples, std::max (core::kHoldMinLoopSamples, samples)); };
    auto holdStartTarget = [&] (int zone, double loopSamples)
    {
        return core::holdStartSeconds (params.repeats, zone, loopSamples / sampleRate) * sampleRate;
    };
    bool holdPressJump = false, holdScrubJump = false;
    if (started && holdNow)
    {
        const int audible = fading ? 1 - current : current;
        for (int c = 0; c < channelCount; ++c)
        {
            const double loop = holdLoopClamp ((c == 0 ? sides.left : sides.right) * sampleRate);
            if (holdPress)
            {
                if (holdStartTarget (params.zone, loop) > core::kHoldPressJumpSeconds * sampleRate)
                    holdPressJump = true;
            }
            else if (std::fabs (holdStartTarget (headZone[audible], loop) - holdHeads[c][audible].s) > core::kHoldScrubJumpSeconds * sampleRate)
                holdScrubJump = true;
        }
    }
    // Tempo Sync: which step each side is on (and which clock). A change is a jump in time, crossfaded like a Zone change.
    SyncKey syncKey;
    if (kTempoSyncAudible && params.tempoSync)
    {
        const core::ChannelRates r = sideRates();
        syncKey.step[0] = core::syncStep (r.left);
        syncKey.step[1] = core::syncStep (r.right);
        syncKey.division = params.clockDivision;
        syncKey.reference = core::bestZoneForPeriod (clockPeriod());
    }
    const bool syncJump = started && syncKey != lastSyncKey;
    lastSyncKey = syncKey;
    const bool genericJump = started && (params.zone != lastZone || pingHopNow != lastPingHop || skewJump || syncJump);
    bool jumpRan = false;
    if (! started)
    {
        inputGain.snap (inputGain.target);
        outputGain.snap (outputGain.target);
        dryGain.snap (dryGain.target);
        wetGain.snap (wetGain.target);
        feedback.snap (feedback.target);
        flipAmount.snap (flipAmount.target);
        crossing.snap (crossing.target);
        monoSum.snap (monoSum.target);
        colorKnob.snap (colorKnob.target);
        uRateKnob.snap (uRateKnob.target);
        lfoSeconds.snap (lfoSeconds.target);
        lfoSemitones.snap (lfoSemitones.target);
        rightSide.snap (rightSide.target);
        headZone[0] = headZone[1] = params.zone;
        colorApplied = (float) colorKnob.value;
        colorFilter.setShape (core::colorShape (colorApplied));
        colorCountdown = kColorUpdateSamples;
        halo.setTimbre (core::colorShape (colorApplied));
        halo.snap();
        for (int c = 0; c < 2; ++c)
        {
            heads[c][0].logDelay = heads[c][0].logTarget = logLive[c];
            heads[c][1] = heads[c][0];
        }
        current = 0;
        fading = false;
        lastZone = params.zone;
        lastPingHop = pingHopNow;
        started = true;
    }
    else if (genericJump || holdPressJump || holdScrubJump)
    {
        jumpRan = true;
        lastZone = params.zone;
        lastPingHop = pingHopNow;
        for (int c = 0; c < 2; ++c)
        {
            Head& incoming = heads[c][1 - current];
            incoming.logDelay = incoming.logTarget = logLive[c];       // the new time, at once (no glide, so no pitch effect)
        }
        headZone[1 - current] = params.zone;                           // the new read point is modulated the way the new Zone says
        restartFlip = true;                                            // Flip: a new chunk with the new time, at once
        if (! fading)
        {
            fading = true;
            fadePosition = 0.0f;
        }
    }
    for (int c = 0; c < 2; ++c)
    {
        if (fading)
        {
            heads[c][current].logTarget = heads[c][current].logDelay;      // the old read point holds its time while it fades out
            heads[c][1 - current].logTarget = logLive[c];
        }
        else
            heads[c][current].logTarget = logLive[c];
    }

    // Hold: pressing freezes the memory and starts the loops where the delay was reading (so no click); releasing lets go
    if (holdPress)
    {
        holdOn = true;
        holdEnd = buffers[0].writeIndex();
        for (auto& b : buffers)
            b.freeze();
        holdAmount.snap (1.0);                       // the loop carries on exactly where the delay was reading
        holdSoft.target = 1.0;                       // Color on the loop and the reversed loop fade in over about 10 ms
        holdSwap.snap (0.0);
        holdSwapSide = false;
        holdColorFilter.setShape (core::colorShape (colorApplied));
        holdColorFilter.reset();
        const double zeroFactor = (uRateKnob.value != 0.0 || lfoSeconds.value != 0.0 || lfoSemitones.value != 0.0)
                                      ? std::exp2 (-(uRateKnob.value * core::kURateKnobMaxSemitones + lfoSmoothed * lfoSemitones.value) / 12.0)
                                      : 1.0;
        for (int c = 0; c < 2; ++c)
            holdHeads[c][0] = holdHeads[c][1] = core::HoldHead();
        for (int c = 0; c < channelCount; ++c)
        {
            const int outgoing = current;
            double loop = holdLoopClamp (std::exp (heads[c][outgoing].logDelay) * sampleRate);
            holdHeads[c][outgoing].u = headZone[outgoing] == 0 ? loop * zeroFactor : loop;      // Zone 0: the loop is the pitch-bent length
            if (fading)
            {
                const int incoming = 1 - outgoing;
                loop = holdLoopClamp (std::exp (heads[c][incoming].logDelay) * sampleRate);
                holdHeads[c][incoming].u = loop;
                holdHeads[c][incoming].s = holdStartTarget (headZone[incoming], loop);
            }
        }
    }
    else if (! holdNow && holdOn)
    {
        holdOn = false;
        for (auto& b : buffers)
            b.unfreeze();
        holdAmount.target = 0.0;                     // back to the live repeat over about 10 ms
        holdSoft.target = 0.0;
        for (int c = 0; c < 2; ++c)
            flipVoices[c].reset();
    }
    else if (holdOn && jumpRan)
    {
        // A jump while held (Zone, Rate step, Skew, Ping Pong hop, Tempo Sync, or a far move of Repeats): the incoming loop starts at its
        // beginning (or, for a far move of Repeats only, at the same place in the loop, so the rhythm stays) and at the new start point
        const int incoming = 1 - current;
        for (int c = 0; c < channelCount; ++c)
        {
            core::HoldHead& hh = holdHeads[c][incoming];
            const double loop = holdLoopClamp (std::exp (heads[c][incoming].logDelay) * sampleRate);
            if (genericJump)
            {
                hh.u = loop;
                hh.ur = 0.0;
            }
            else
            {
                hh.u = holdHeads[c][current].u;
                hh.ur = holdHeads[c][current].ur;
            }
            hh.s = holdStartTarget (headZone[incoming], loop);
        }
    }

    const double minDelay = dsp::DelayBuffer::kMinDelaySamples;
    const double maxDelay = buffers[0].maxDelaySamples();

    // Flip: a jump starts a new chunk; a long chunk is also restarted when Rate has moved far from the length it started with
    if (params.flip || flipAmount.value > 0.0)
    {
        const int audible = fading ? 1 - current : current;
        for (int c = 0; c < channelCount; ++c)
        {
            if (restartFlip)
                flipVoices[c].restart();
            if (headZone[audible] != 0)
                flipVoices[c].checkLength (std::min (core::kFlipMaxChunkSeconds * sampleRate, std::exp (heads[c][audible].logDelay) * sampleRate), sampleRate);
        }
    }

    // ---- the sound ----
    for (int n = 0; n < numSamples; ++n)
    {
        const float inG = inputGain.next (rampCoefficient), outG = outputGain.next (rampCoefficient);
        const float dryG = dryGain.next (rampCoefficient), wetG = wetGain.next (rampCoefficient);
        const float fbG = feedback.next (rampCoefficient);
        const float colorNow = colorKnob.next (colorCoefficient);
        if (--colorCountdown <= 0)
        {
            colorCountdown = kColorUpdateSamples;
            if (colorNow != colorApplied)
            {
                colorApplied = colorNow;
                const core::ColorShape shapeNow = core::colorShape (colorNow);
                colorFilter.setShape (shapeNow);
                if (holdOn || holdWasActive)
                    holdColorFilter.setShape (shapeNow);        // and the filter on the held loop
                halo.setTimbre (shapeNow);          // Color also sets Halo's timbre
            }
        }

        const float crossG = crossing.next (rampCoefficient), monoG = monoSum.next (rampCoefficient);
        const float flipG = flipAmount.next (rampCoefficient);
        const bool flipActive = flipAmount.value > 0.0 || flipAmount.target > 0.0;
        const float holdG = holdAmount.next (rampCoefficient), holdS = holdSoft.next (rampCoefficient), swapG = holdSwap.next (rampCoefficient);
        const bool holdActive = holdAmount.value > 0.0 || holdAmount.target > 0.0;
        if ((flipActive || holdActive) && ! (flipWasActive || holdWasActive))
            for (int c = 0; c < 2; ++c)
                dcInH[c] = dcIn[c], dcOutH[c] = dcOut[c];                  // the second DC blocker starts where the first one is
        if (! flipActive && flipWasActive)
            for (int c = 0; c < 2; ++c)
                flipVoices[c].reset();                                      // the reversed sound has faded out: the readers rest
        if (! holdActive && holdWasActive)
            holdColorFilter.reset();                                        // the loop has faded out
        flipWasActive = flipActive;
        holdWasActive = holdActive;
        const bool heldFully = holdOn && holdS >= 1.0f;                     // the flip readers rest while the loop is all that is heard

        // uRate and its LFO: one offset for the sample, added to each read point's own (glided) time below
        uRateKnob.next (rampCoefficient);
        lfoSeconds.next (rampCoefficient);
        lfoSemitones.next (rampCoefficient);
        rightSide.next (delayCoefficient);
        lfoPhase += lfoStep;
        if (lfoPhase >= 1.0)
            lfoPhase -= std::floor (lfoPhase);
        lfoSmoothed += (core::lfoValue (params.uRateLfoShape, lfoPhase) - lfoSmoothed) * lfoSmoothCoefficient;
        const double knobNow = uRateKnob.value;
        const bool modulating = knobNow != 0.0 || lfoSeconds.value != 0.0 || lfoSemitones.value != 0.0;
        auto modulated = [&] (int zone, double seconds, int channel)
        {
            if (! modulating)
                return seconds;                                        // nothing at all is changed: bit for bit as before milestone 7
            if (zone == 0)
                return seconds * std::exp2 (-(knobNow * core::kURateKnobMaxSemitones + lfoSmoothed * lfoSemitones.value) / 12.0);
            const double side = channel == 1 ? rightSide.value : 1.0;
            return seconds - side * (knobNow * core::kURateKnobMaxSeconds + lfoSmoothed * lfoSeconds.value);
        };

        // Each side's read point(s)
        double delayCurrent[2] = { minDelay, minDelay }, delayIncoming[2] = { minDelay, minDelay };
        double baseCurrent[2] = { minDelay, minDelay }, baseIncoming[2] = { minDelay, minDelay };      // the same times before uRate (the held loop's length)
        for (int c = 0; c < channelCount; ++c)
        {
            Head& hc = heads[c][current];
            hc.logDelay += (hc.logTarget - hc.logDelay) * (double) delayCoefficient;
            const double secondsCurrent = std::exp (hc.logDelay);
            baseCurrent[c] = secondsCurrent * sampleRate;
            delayCurrent[c] = std::min (maxDelay, std::max (minDelay, modulated (headZone[current], secondsCurrent, c) * sampleRate));
            delayIncoming[c] = delayCurrent[c];
            if (fading)
            {
                Head& hi = heads[c][1 - current];
                hi.logDelay += (hi.logTarget - hi.logDelay) * (double) delayCoefficient;
                const double secondsIncoming = std::exp (hi.logDelay);
                baseIncoming[c] = secondsIncoming * sampleRate;
                delayIncoming[c] = std::min (maxDelay, std::max (minDelay, modulated (headZone[1 - current], secondsIncoming, c) * sampleRate));
            }
        }

        // Hold (milestone 11): the held loop of each side, forwards (and backwards under Flip), then every loop's phase moves on one sample.
        // Ping Pong / Swap: the two sides trade places on every pass of the left loop.
        const bool stereoHold = numChannels == 2 && channelCount == 2;
        float loopFwd[2] = { 0.0f, 0.0f }, loopRev[2] = { 0.0f, 0.0f };
        if (holdActive)
        {
            for (int c = 0; c < channelCount; ++c)
            {
                auto headValue = [&] (int h, double baseSamples, bool backwards)
                {
                    const core::HoldHead& hh = holdHeads[c][h];
                    double length = baseSamples, offset = 0.0;
                    if (modulating)
                    {
                        if (headZone[h] == 0)
                            length *= std::exp2 (-(knobNow * core::kURateKnobMaxSemitones + lfoSmoothed * lfoSemitones.value) / 12.0);     // Zone 0: pitch
                        else
                            offset = (c == 1 ? rightSide.value : 1.0) * (knobNow * core::kURateKnobMaxSeconds + lfoSmoothed * lfoSeconds.value) * sampleRate;
                    }
                    length = holdLoopClamp (length);
                    return core::holdLoopValue (buffers[c], (double) holdEnd - hh.s, backwards ? hh.ur + offset : hh.u - offset, length,
                                                core::holdFadeSamples (length, sampleRate));
                };
                loopFwd[c] = headValue (current, baseCurrent[c], false);
                if (fading)
                    loopFwd[c] += fadePosition * (headValue (1 - current, baseIncoming[c], false) - loopFwd[c]);
                if (flipActive)
                {
                    loopRev[c] = headValue (current, baseCurrent[c], true);
                    if (fading)
                        loopRev[c] += fadePosition * (headValue (1 - current, baseIncoming[c], true) - loopRev[c]);
                }
            }
            bool leftWrapped = false;
            for (int c = 0; c < channelCount; ++c)
            {
                auto advance = [&] (int h, double baseSamples)
                {
                    core::HoldHead& hh = holdHeads[c][h];
                    const double length = holdLoopClamp (baseSamples);
                    bool wrapped = false;
                    hh.u -= 1.0;
                    if (hh.u < 0.0)
                    {
                        hh.u += length;
                        wrapped = true;
                    }
                    if (hh.u > length)
                        hh.u = std::fmod (hh.u, length);
                    hh.ur += 1.0;
                    if (hh.ur >= length)
                        hh.ur -= length;
                    if (hh.ur >= length)
                        hh.ur = std::fmod (hh.ur, length);
                    // the start point follows Repeats (a small move glides; a far one was a jump), and stays inside the memory;
                    // the read point that is fading out keeps its start point (it must not run while it goes)
                    if (! (fading && h == current))
                        hh.s += (holdStartTarget (headZone[h], length) - hh.s) * (double) holdScrubCoefficient;
                    hh.s = std::min (hh.s, std::max (0.0, holdCapSamples - length));
                    return wrapped;
                };
                if (advance (current, baseCurrent[c]) && c == 0)
                    leftWrapped = true;
                if (fading)
                    advance (1 - current, baseIncoming[c]);
            }
            if (holdOn && stereoHold && crossing.target > 0.5)
            {
                if (leftWrapped)
                {
                    holdSwapSide = ! holdSwapSide;
                    holdSwap.target = holdSwapSide ? 1.0 : 0.0;
                }
            }
            else
            {
                holdSwapSide = false;
                holdSwap.target = 0.0;
            }
        }

        // First every side's repeat is read (and its DC taken out); then Halo works on both sides' repeats; then each side's feedback is
        // prepared; then everything is written (Ping Pong and Swap cross the two).
        float x[2] = { 0.0f, 0.0f }, wet[2] = { 0.0f, 0.0f }, blocked[2] = { 0.0f, 0.0f }, fedBack[2] = { 0.0f, 0.0f };
        float heardWet[2] = { 0.0f, 0.0f }, haloIn[2] = { 0.0f, 0.0f };      // what is heard (the repeat, or its reverse) and what Halo works on
        for (int c = 0; c < channelCount; ++c)
        {
            x[c] = channels[c][n];
            if (! std::isfinite (x[c]))
                x[c] = 0.0f;                                       // a bad sample must never get into the feedback
            x[c] *= inG;

            if (holdActive)
            {
                // Hold: the loop (and, for a few ms after Hold is released, the live repeat fading back in)
                float loop = loopFwd[c];
                if (stereoHold && crossG > 0.0f && swapG > 0.0f)
                    loop += crossG * swapG * (loopFwd[1 - c] - loop);
                float live = 0.0f;
                if (holdG < 1.0f)
                {
                    live = buffers[c].read (delayCurrent[c]);
                    if (fading)
                        live += fadePosition * (buffers[c].read (delayIncoming[c]) - live);
                }
                wet[c] = live + holdG * (loop - live);
            }
            else
            {
                wet[c] = buffers[c].read (delayCurrent[c]);
                if (fading)
                    wet[c] += fadePosition * (buffers[c].read (delayIncoming[c]) - wet[c]);
            }

            // The repeat minus any DC (what is fed back, and what Halo works on)
            float b = wet[c] - dcIn[c] + dcCoefficient * dcOut[c];
            dcIn[c] = wet[c];
            if (std::fabs (b) < 1.0e-20f)
                b = 0.0f;
            dcOut[c] = b;
            blocked[c] = b;
            heardWet[c] = wet[c];
            haloIn[c] = b;
        }

        // Flip (milestone 10): the same memory read backwards, in chunks as long as the delay time. Off = nothing here runs, bit for bit.
        if (flipActive)
        {
            const int audible = fading ? 1 - current : current;
            const int zoneNow = headZone[audible];
            for (int c = 0; c < channelCount; ++c)
            {
                // uRate and its LFO move the reading point (Zones 1-7), so the reversed sound bends; clockwise = higher pitch as before
                double offsetSamples = 0.0;
                if (modulating && zoneNow != 0)
                    offsetSamples = (c == 1 ? rightSide.value : 1.0) * (knobNow * core::kURateKnobMaxSeconds + lfoSmoothed * lfoSeconds.value) * sampleRate;
                // A new chunk is as long as the delay time (in Zone 0 with uRate's pitch in it), held to what the memory can read backwards
                auto chunk = [&] ()
                {
                    double seconds = std::exp (heads[c][audible].logDelay);
                    if (zoneNow == 0 && modulating)
                        seconds = modulated (0, seconds, c);
                    return std::min (core::kFlipMaxChunkSeconds * sampleRate, std::max (core::kFlipMinChunkSamples, seconds * sampleRate));
                };
                float reversed = heldFully ? 0.0f : flipVoices[c].process (buffers[c], offsetSamples, minDelay, maxDelay, chunk);
                if (holdActive)
                {
                    // Hold: the reversed sound is the held loop played backwards (the chunk reader fades out as the loop fades in)
                    float backwards = loopRev[c];
                    if (stereoHold && crossG > 0.0f && swapG > 0.0f)
                        backwards += crossG * swapG * (loopRev[1 - c] - backwards);
                    reversed += holdS * (backwards - reversed);
                }
                heardWet[c] = wet[c] + flipG * (reversed - wet[c]);
                if (! holdActive)
                {
                    // Halo works on the reversed sound that is heard (its own DC blocker; flipG = 0 gives exactly the forward repeat's)
                    float bh = heardWet[c] - dcInH[c] + dcCoefficient * dcOutH[c];
                    dcInH[c] = heardWet[c];
                    if (std::fabs (bh) < 1.0e-20f)
                        bh = 0.0f;
                    dcOutH[c] = bh;
                    haloIn[c] = blocked[c] + flipG * (bh - blocked[c]);
                }
            }
        }

        // Hold: Color shapes the loop that is heard (not written into the memory), and Halo works on that
        if (holdActive)
        {
            for (int c = 0; c < channelCount; ++c)
            {
                const float coloured = holdColorFilter.process (c, heardWet[c]);
                heardWet[c] += holdS * (coloured - heardWet[c]);
                float bh = heardWet[c] - dcInH[c] + dcCoefficient * dcOutH[c];
                dcInH[c] = heardWet[c];
                if (std::fabs (bh) < 1.0e-20f)
                    bh = 0.0f;
                dcOutH[c] = bh;
                haloIn[c] = blocked[c] + std::max (flipG, holdS) * (bh - blocked[c]);
            }
        }

        // Halo (milestone 9): diffusion of the repeat that is heard. Off = nothing is touched, bit for bit.
        float haloOut[2] = { 0.0f, 0.0f };
        const bool haloOn = halo.process (haloIn, haloOut);
        const float haloLevel = haloOn ? halo.level() : 0.0f;
        const float haloInject = haloOn ? halo.injection() : 0.0f;

        for (int c = 0; c < channelCount && ! holdOn; ++c)         // (Hold: nothing is fed back)
        {
            // What is fed back: the repeat (and, at high Halo settings, its halo), scaled by Repeats, kept from running away by the leveller
            // and the soft limiter.
            float fed = (haloInject > 0.0f ? blocked[c] + haloInject * haloOut[c] : blocked[c]) * fbG;
            const float mag = std::fabs (fed);
            levelEnv[c] = std::max (mag, levelEnv[c] * envRelease);
            const float wanted = levelEnv[c] > kLevellerCeiling ? kLevellerCeiling / levelEnv[c] : 1.0f;
            if (wanted < levelGain[c])
                levelGain[c] = wanted;                         // instant attack: no part of a loud echo's start slips past
            else
                levelGain[c] += (wanted - levelGain[c]) * levelRelease;
            fed *= levelGain[c];
            fedBack[c] = dsp::softLimit (fed);
        }

        for (int c = 0; c < channelCount; ++c)
        {
            // What goes in: the live signal (Ping Pong: both inputs summed to mono, into the left side only) plus the fed-back repeat
            // (Ping Pong and Swap: the other side's repeat), shaped by Color (exactly untouched at unity).
            if (holdOn)
                buffers[c].advanceFrozen();                        // Hold: the input is ignored and the memory stays as it is
            else
            {
                float live = x[c];
                float returned = fedBack[c];
                if (stereoRouting)
                {
                    if (monoG > 0.0f)
                        live = c == 0 ? x[0] + monoG * (0.5f * (x[0] + x[1]) - x[0]) : x[1] * (1.0f - monoG);
                    if (crossG > 0.0f)
                        returned = fedBack[c] + crossG * (fedBack[1 - c] - fedBack[c]);
                }
                buffers[c].write (colorFilter.process (c, live + returned));
            }

            const float heard = haloOn ? core::haloMix (heardWet[c], haloOut[c], haloLevel) : heardWet[c];
            channels[c][n] = (dryG * x[c] + wetG * heard) * outG;
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
