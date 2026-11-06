// Self-test of the engine (pass-through, control list, tempo clock, level helpers; milestone 3 adds the Zone / Rate time map and the
// counters behind the window's lights).
// Prints "engine tests: N checks, F failed" and exits with 0 only if nothing failed.

#include <mph/Engine.h>
#include <mph/Params.h>
#include <mph/core/Gain.h>
#include <mph/core/HostClock.h>
#include <mph/core/ZoneMap.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{
    int checks = 0, failures = 0;

    void expect (bool ok, const char* what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what);
        }
    }

    // ---- the control list ----
    void testParams()
    {
        const mph::Params p;
        expect (p.inputLevel >= 0.0f && p.inputLevel <= 1.0f, "default Input Level is a knob position");
        expect (p.mix >= 0.0f && p.mix <= 1.0f, "default Mix is a knob position");
        expect (p.repeats >= 0.0f && p.repeats <= 1.0f, "default Repeats is a knob position");
        expect (p.halo >= 0.0f && p.halo <= 1.0f, "default Halo is a knob position");
        expect (p.outputLevel >= 0.0f && p.outputLevel <= 1.0f, "default Output Level is a knob position");
        expect (p.color >= 0.0f && p.color <= 1.0f, "default Color is a knob position");
        expect (p.rate >= 0.0f && p.rate <= 1.0f, "default Rate is a knob position");
        expect (p.uRate >= 0.0f && p.uRate <= 1.0f, "default uRate is a knob position");
        expect (p.uRateLfoRate >= 0.0f && p.uRateLfoRate <= 1.0f, "default uRate LFO Rate is a knob position");
        expect (p.uRateLfoDepth >= 0.0f && p.uRateLfoDepth <= 1.0f, "default uRate LFO Depth is a knob position");
        expect (p.zone >= 0 && p.zone < mph::kNumZones, "default Zone exists");
        expect (p.inputGainStep >= 0 && p.inputGainStep < mph::kNumInputGainSteps, "default input gain step exists");
        expect (mph::kInputGainStepDb[p.inputGainStep] == 0.0f, "default input gain is 0 dB");
        expect (p.clockDivision >= 0 && p.clockDivision < mph::core::kNumClockDivisions, "default clock division exists");
        expect (std::strcmp (mph::core::kClockDivisions[p.clockDivision].name, "1/4") == 0, "default clock division is 1/4");
        expect (p.uRateLfoShape == mph::kLfoSine, "default LFO shape is sine");
        expect (! p.hold && ! p.flip && ! p.skew && ! p.pingPong && ! p.clear && ! p.tempoSync && ! p.uRateLfoSync,
                "all buttons start off");
        expect (p.uRateLfoDepth == 0.0f, "no modulation until asked for");
    }

    // ---- the zone table matches the manual (p. 10) ----
    void testZones()
    {
        expect (mph::kNumZones == 8, "eight zones");
        const double minMs[8] = { 1.3, 20.4, 81.6, 163.3, 326.5, 653.1, 1306.0, 2612.0 };
        const double maxMs[8] = { 20.4, 81.6, 326.5, 653.1, 1306.0, 2612.0, 5225.0, 41796.0 };
        for (int z = 0; z < 8; ++z)
        {
            expect (std::fabs (mph::kZones[z].minSeconds * 1000.0 - minMs[z]) < 0.06, "zone minimum matches the manual");
            expect (std::fabs (mph::kZones[z].maxSeconds * 1000.0 - maxMs[z]) < 0.6, "zone maximum matches the manual");
            expect (mph::kZones[z].minSeconds < mph::kZones[z].maxSeconds, "zone range runs upwards");
            if (z > 0)
                expect (mph::kZones[z].maxSeconds > mph::kZones[z - 1].maxSeconds, "zones get longer");
        }
        expect (mph::kLongestSeconds >= 41.79, "the shared buffer will need at least 41.8 s");
        expect (mph::kZones[7].maxSeconds / mph::kZones[0].minSeconds > 2000.0, "zone 7 is over 2000x zone 0 (manual p. 10)");
    }

    // ---- pass-through: bit for bit ----
    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
    }

    void testPassThrough()
    {
        std::mt19937 rng (12345);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::uniform_real_distribution<float> knob (0.0f, 1.0f);

        const double rates[3] = { 44100.0, 48000.0, 88200.0 };
        const int blocks[8] = { 1, 7, 32, 64, 100, 512, 1024, 4096 };

        for (int channels = 1; channels <= 2; ++channels)
            for (double rate : rates)
                for (int block : blocks)
                {
                    mph::Engine engine;
                    engine.prepare (rate, block, channels);

                    std::vector<std::vector<float>> audio ((size_t) channels, std::vector<float> ((size_t) block));
                    bool allSame = true;
                    for (int round = 0; round < 6; ++round)
                    {
                        // Random control values and random buttons: the audio must still come out exactly as it went in.
                        mph::Params p;
                        p.inputLevel = knob (rng); p.mix = knob (rng); p.repeats = knob (rng); p.halo = knob (rng);
                        p.outputLevel = knob (rng); p.color = knob (rng); p.rate = knob (rng); p.uRate = knob (rng);
                        p.uRateLfoRate = knob (rng); p.uRateLfoDepth = knob (rng);
                        p.zone = (int) (knob (rng) * 7.99f); p.inputGainStep = (int) (knob (rng) * 3.99f);
                        p.uRateLfoShape = round & 1; p.clockDivision = (int) (knob (rng) * 9.99f);
                        p.hold = rng() & 1; p.flip = rng() & 1; p.skew = rng() & 1; p.pingPong = rng() & 1;
                        p.clear = (round % 3) == 0; p.tempoSync = rng() & 1; p.uRateLfoSync = rng() & 1;
                        engine.setParams (p);

                        mph::core::HostInfo host;
                        host.tempoValid = true; host.bpm = 60.0 + 120.0 * knob (rng); host.playing = rng() & 1;
                        host.ppqValid = true; host.ppq = 100.0 * knob (rng);
                        engine.setHost (host);

                        for (auto& ch : audio)
                            for (auto& s : ch)
                                s = noise (rng);
                        const auto before = audio;

                        std::vector<float*> pointers;
                        for (auto& ch : audio)
                            pointers.push_back (ch.data());
                        engine.process (pointers.data(), channels, block);

                        for (int c = 0; c < channels; ++c)
                            allSame = allSame && identical (before[(size_t) c], audio[(size_t) c]);
                    }
                    expect (allSame, "audio passes through unchanged (bit for bit), random controls");
                }

        // Special values are left alone too: silence, full scale, tiny numbers, even not-a-number.
        {
            mph::Engine engine;
            engine.prepare (48000.0, 8, 2);
            std::vector<float> l { 0.0f, 1.0f, -1.0f, 1.0e-30f, -1.0e-30f, 3.0f, std::nanf (""), 0.5f };
            std::vector<float> r (l.rbegin(), l.rend());
            const auto l0 = l, r0 = r;
            float* pointers[2] = { l.data(), r.data() };
            engine.process (pointers, 2, 8);
            expect (identical (l0, l) && identical (r0, r), "special values pass through unchanged");
        }

        // An empty block does nothing and does not crash.
        {
            mph::Engine engine;
            engine.prepare (48000.0, 512, 2);
            float dummy = 0.25f;
            float* pointers[2] = { &dummy, &dummy };
            engine.process (pointers, 2, 0);
            expect (dummy == 0.25f, "an empty block changes nothing");
        }
    }

    // ---- Clear: the engine notes it, once, so the wrapper can switch the button off ----
    void testClear()
    {
        mph::Engine engine;
        engine.prepare (48000.0, 512, 2);
        std::vector<float> l (64, 0.1f), r (64, 0.2f);
        float* pointers[2] = { l.data(), r.data() };

        engine.process (pointers, 2, 64);
        expect (! engine.takeClearHandled(), "nothing handled while Clear is off");

        mph::Params p;
        p.clear = true;
        engine.setParams (p);
        engine.process (pointers, 2, 64);
        expect (engine.takeClearHandled(), "Clear on is noticed");
        expect (! engine.takeClearHandled(), "and reported only once");

        p.clear = false;
        engine.setParams (p);
        engine.process (pointers, 2, 64);
        expect (! engine.takeClearHandled(), "nothing more once Clear is off again");
    }

    // ---- the tempo clock (copied from PhenomeEon) ----
    int countTicks (mph::core::HostClock& clock, const mph::core::HostInfo& host, double beatsEvery, int totalSamples, int block)
    {
        int ticks = 0;
        for (int done = 0; done < totalSamples; done += block)
        {
            mph::core::HostInfo h = host;
            if (host.playing)
                h.ppq = clock.followingHost() ? clock.beatsNow() : host.ppq;      // as Logic would: the position moves on
            clock.beginBlock (h, beatsEvery);
            const int n = std::min (block, totalSamples - done);
            for (int i = 0; i < n; ++i)
            {
                if (clock.tickNow())
                    ++ticks;
                clock.next();
            }
        }
        return ticks;
    }

    void testClock()
    {
        // 1/4 note at 120 BPM = a tick every half second. Ten seconds at 48 kHz.
        mph::core::HostInfo host;
        host.tempoValid = true;
        host.bpm = 120.0;
        host.beatsPerBar = 4.0;

        {
            mph::core::HostClock clock;
            clock.prepare (48000.0);
            host.playing = true; host.ppqValid = true; host.ppq = 0.0;
            const double every = mph::core::beatsPerTick (mph::core::kDefaultClockDivision, 4.0);
            expect (every == 1.0, "1/4 is one beat");
            const int ticks = countTicks (clock, host, every, 480000, 512);
            expect (ticks == 20, "playing from 0: 20 ticks of 1/4 in ten seconds at 120 BPM (tick on the first sample)");
        }
        {
            mph::core::HostClock clock;
            clock.prepare (48000.0);
            host.playing = false; host.ppqValid = false;
            const int ticks = countTicks (clock, host, 1.0, 480000, 512);
            expect (ticks >= 19 && ticks <= 20, "stopped: the clock free-runs at Logic's tempo (about 20 ticks in ten seconds)");
            expect (std::fabs (clock.bpm() - 120.0) < 1.0e-9, "tempo is taken from the host");
        }
        {
            mph::core::HostClock clock;
            clock.prepare (44100.0);
            host.playing = false; host.ppqValid = false; host.bpm = 90.0;
            const double every = mph::core::beatsPerTick (7, 4.0);          // 1/16 = a quarter of a beat
            expect (every == 0.25, "1/16 is a quarter of a beat");
            const int ticks = countTicks (clock, host, every, 44100 * 20, 300);
            // 90 BPM = 1.5 beats per second; 20 seconds = 30 beats = 120 sixteenths
            expect (ticks >= 119 && ticks <= 120, "1/16 at 90 BPM at 44.1 kHz: 120 ticks in twenty seconds");
        }
        {
            // A bar follows the host's time signature
            expect (mph::core::beatsPerTick (1, 3.0) == 3.0, "1 bar in 3/4 is three beats");
            expect (mph::core::beatsPerTick (0, 4.0) == 8.0, "2 bars in 4/4 is eight beats");
            expect (mph::core::beatsPerTick (1, 0.0) == 4.0, "a bad bar length falls back to 4 beats");
            expect (mph::core::beatsPerTick (99, 4.0) == mph::core::beatsPerTick (9, 4.0), "an out-of-range division is held to the last one");
        }
        expect (mph::core::kNumClockDivisions == 10, "ten clock divisions");
    }

    // ---- level helpers (copied from PhenomeEon) ----
    void testGain()
    {
        expect (mph::dsp::levelKnobToGain (0.0f) == 0.0f, "level knob at 0 is silent");
        expect (std::fabs (mph::dsp::levelKnobToGain (0.75f) - 1.0f) < 1.0e-6f, "level knob at 0.75 is 0 dB");
        expect (std::fabs (mph::dsp::levelKnobToDb (1.0f) - 6.0f) < 0.01f, "level knob at the top is +6 dB");
        expect (mph::dsp::levelKnobToDb (0.0f) == -120.0f, "silence reads -120 dB");
        float last = -1.0f;
        bool rising = true;
        for (int i = 0; i <= 1000; ++i)
        {
            const float g = mph::dsp::levelKnobToGain ((float) i / 1000.0f);
            rising = rising && g >= last;
            last = g;
        }
        expect (rising, "the level knob only ever gets louder as it is turned up");
        expect (std::fabs (mph::dsp::dbToGain (6.0f) - 1.995f) < 0.002f, "+6 dB is about 2x");

        // The soft limiter: unchanged below the knee, never reaches 1.0, never runs away.
        expect (mph::dsp::softLimit (0.5f) == 0.5f && mph::dsp::softLimit (-0.79f) == -0.79f, "soft limiter leaves quiet signal alone");
        bool bounded = true, odd = true, belowOneWhenModerate = true;
        for (int i = 0; i <= 2000; ++i)
        {
            const float x = (float) i * 0.05f;
            const float y = mph::dsp::softLimit (x);
            bounded = bounded && y <= 1.0f && y >= 0.0f;
            if (x <= 2.0f)
                belowOneWhenModerate = belowOneWhenModerate && y < 1.0f;
            odd = odd && mph::dsp::softLimit (-x) == -y;
        }
        expect (bounded, "soft limiter output never goes above 1.0, however large the input");
        expect (belowOneWhenModerate, "soft limiter output is below 1.0 for input up to 2.0");
        expect (mph::dsp::softLimit (1.0e30f) == 1.0f && mph::dsp::softLimit (-1.0e30f) == -1.0f, "a huge input gives exactly full scale, not more");
        expect (odd, "soft limiter treats positive and negative alike");
    }
    // ---- Zone and Rate in time (milestone 3, used by the window's read-outs and the lights) ----
    void testZoneMap()
    {
        using namespace mph::core;

        // Free running: knob fully left = the Zone's shortest time, fully right = its longest, the middle = the geometric middle
        bool ends = true, middle = true, rising = true;
        for (int z = 0; z < mph::kNumZones; ++z)
        {
            ends = ends && std::fabs (freeDelaySeconds (z, 0.0f) - mph::kZones[z].minSeconds) < 1.0e-12
                        && std::fabs (freeDelaySeconds (z, 1.0f) - mph::kZones[z].maxSeconds) < 1.0e-9;
            const double geo = std::sqrt (mph::kZones[z].minSeconds * mph::kZones[z].maxSeconds);
            middle = middle && std::fabs (freeDelaySeconds (z, 0.5f) - geo) < 1.0e-9 * geo;
            double last = 0.0;
            for (int i = 0; i <= 100; ++i)
            {
                const double d = freeDelaySeconds (z, (float) i / 100.0f);
                rising = rising && d > last;
                last = d;
            }
        }
        expect (ends, "Rate fully left / right gives the Zone's shortest / longest time (all 8 Zones)");
        expect (middle, "Rate in the middle gives the geometric middle of the Zone");
        expect (rising, "a higher Rate knob position always means a longer delay time (all 8 Zones)");
        expect (std::fabs (freeDelaySeconds (4, 0.5f) - 0.6531) < 0.001, "Zone 4 with Rate in the middle is about 653 ms");
        expect (freeDelaySeconds (-5, 0.5f) == freeDelaySeconds (0, 0.5f) && freeDelaySeconds (99, 0.5f) == freeDelaySeconds (7, 0.5f),
                "a Zone outside 0..7 is clamped");
        expect (freeDelaySeconds (3, -1.0f) == freeDelaySeconds (3, 0.0f) && freeDelaySeconds (3, 2.0f) == freeDelaySeconds (3, 1.0f),
                "a Rate outside 0..1 is clamped");

        // Tempo sync: 13 steps as in the manual
        expect (kNumSyncSteps == 13, "13 multiples / divisions");
        expect (kSyncFactors[kSyncUnityStep] == 1.0 && std::strcmp (kSyncNames[kSyncUnityStep], "1/1") == 0, "step 6 is 1/1");
        expect (std::strcmp (kSyncNames[0], "x2") == 0 && kSyncFactors[0] == 2.0, "the left end is x2");
        expect (std::strcmp (kSyncNames[12], "/2") == 0 && kSyncFactors[12] == 0.5, "the right end is /2");
        bool falling = true;
        for (int i = 1; i < kNumSyncSteps; ++i)
            falling = falling && kSyncFactors[i] < kSyncFactors[i - 1];
        expect (falling, "the factors fall steadily from the left end to the right end");
        expect (std::fabs (kSyncFactors[1] - 1.75) < 1e-12 && std::fabs (kSyncFactors[5] - 1.25) < 1e-12 &&
                std::fabs (kSyncFactors[7] * 1.25 - 1.0) < 1e-12 && std::fabs (kSyncFactors[11] * 1.75 - 1.0) < 1e-12,
                "x1 3/4 is 1.75, x1 1/4 is 1.25, /1 1/4 divides by 1.25, /1 3/4 divides by 1.75");
        expect (syncStep (0.0f) == 0 && syncStep (1.0f) == 12 && syncStep (0.5f) == 6, "Rate left / right / middle give step 0 / 12 / 6");
        bool allReached = true, monotone = true;
        int seen[kNumSyncSteps] = {};
        int lastStep = 0;
        for (int i = 0; i <= 1000; ++i)
        {
            const int s = syncStep ((float) i / 1000.0f);
            allReached = allReached && s >= 0 && s < kNumSyncSteps;
            if (s >= 0 && s < kNumSyncSteps) ++seen[s];
            monotone = monotone && s >= lastStep;
            lastStep = s;
        }
        for (int i = 0; i < kNumSyncSteps; ++i)
            allReached = allReached && seen[i] > 0;
        expect (allReached && monotone, "every one of the 13 steps can be reached, in order, and none outside");

        // The Zone that fits a clock best, and the delay with sync
        const double quarterAt120 = 0.5;
        expect (bestZoneForPeriod (quarterAt120) == 4, "a clock of 500 ms fits Zone 4 best (its middle is 653 ms; Zone 3's is 327 ms)");
        expect (bestZoneForPeriod (0.006) == 0 && bestZoneForPeriod (100.0) == 7, "very fast / very slow clocks fit Zone 0 / Zone 7");
        expect (std::fabs (syncDelaySeconds (4, 0.5f, quarterAt120) - 0.5) < 1e-9, "1/1 in the best Zone is exactly the clock period");
        expect (std::fabs (syncDelaySeconds (5, 0.5f, quarterAt120) - 1.0) < 1e-9, "one Zone up doubles it");
        expect (std::fabs (syncDelaySeconds (3, 0.5f, quarterAt120) - 0.25) < 1e-9, "one Zone down halves it");
        expect (std::fabs (syncDelaySeconds (4, 0.0f, quarterAt120) - 0.25) < 1e-9, "x2 halves it");
        expect (std::fabs (syncDelaySeconds (4, 1.0f, quarterAt120) - 1.0) < 1e-9, "/2 doubles it");
        bool inRange = true;
        for (int z = 0; z < mph::kNumZones; ++z)
            for (int i = 0; i <= 20; ++i)
                for (double period : { 0.001, 0.05, 0.5, 4.0, 60.0 })
                {
                    const double d = syncDelaySeconds (z, (float) i / 20.0f, period);
                    inRange = inRange && std::isfinite (d) && d >= mph::kZones[0].minSeconds - 1e-12 && d <= mph::kLongestSeconds + 1e-12;
                }
        expect (inRange, "the synced delay always stays inside the module's whole range");
    }

    // ---- the counters behind the window's lights, and Logic's tempo as last heard ----
    void runSeconds (mph::Engine& e, double seconds, double sampleRate, int block)
    {
        std::vector<float> l ((size_t) block, 0.0f), r ((size_t) block, 0.0f);
        float* chans[2] = { l.data(), r.data() };
        long long total = (long long) (seconds * sampleRate);
        while (total > 0)
        {
            const int n = (int) std::min<long long> (block, total);
            e.process (chans, 2, n);
            total -= n;
        }
    }

    void testLights()
    {
        const double sr = 48000.0;
        mph::Engine e;
        e.prepare (sr, 512, 2);
        expect (e.getHostBpm() == 0.0f, "no tempo heard yet");
        mph::core::HostInfo host;
        host.tempoValid = true;
        host.bpm = 133.0;
        host.beatsPerBar = 3.0;
        e.setHost (host);
        expect (std::fabs (e.getHostBpm() - 133.0f) < 1e-4f && std::fabs (e.getHostBeatsPerBar() - 3.0f) < 1e-4f,
                "the tempo and bar length from Logic are kept for the window");
        host.tempoValid = false;
        e.setHost (host);
        expect (e.getHostBpm() == 0.0f, "no tempo from Logic reads as 0");

        // Tempo clock: 120 beats per minute, ticks every quarter note = 2 per second
        host = {};
        host.tempoValid = true;
        host.bpm = 120.0;
        e.setHost (host);
        mph::Params p;
        p.clockDivision = 3;                     // 1/4
        e.setParams (p);
        runSeconds (e, 10.0, sr, 512);
        const int ticks = e.getClockTickCount();
        expect (ticks >= 19 && ticks <= 21, "10 s at 120 BPM with a 1/4 clock gives about 20 ticks");

        // Rate light, free running: Zone 4 with Rate in the middle is about 653 ms, so about 15 in 10 s
        e.prepare (sr, 512, 2);
        e.setHost (host);
        p = {};
        p.zone = 4;
        p.rate = 0.5f;
        e.setParams (p);
        runSeconds (e, 10.0, sr, 512);
        expect (e.getRateTickCount() >= 14 && e.getRateTickCount() <= 16, "the rate light counts about 15 times in 10 s (Zone 4, Rate in the middle)");

        // Same total with another block size: the counters do not depend on how Logic cuts the audio
        e.prepare (sr, 4096, 2);
        e.setHost (host);
        e.setParams (p);
        runSeconds (e, 10.0, sr, 4096);
        expect (e.getRateTickCount() >= 14 && e.getRateTickCount() <= 16, "... and the same with 4096-sample blocks");

        // Very short delay (Zone 0, shortest): many counts, and no overflow trouble in a huge block
        e.prepare (sr, 8192, 2);
        e.setHost (host);
        p = {};
        p.zone = 0;
        p.rate = 0.0f;
        e.setParams (p);
        runSeconds (e, 1.0, sr, 8192);
        expect (e.getRateTickCount() >= 700 && e.getRateTickCount() <= 800, "Zone 0 at the shortest time counts about 770 times a second");

        // Tempo sync: 1/1 in the best Zone is the clock period (500 ms at 120 BPM, 1/4), so 2 a second
        e.prepare (sr, 512, 2);
        e.setHost (host);
        p = {};
        p.zone = 4;
        p.rate = 0.5f;
        p.tempoSync = true;
        e.setParams (p);
        runSeconds (e, 10.0, sr, 512);
        expect (e.getRateTickCount() >= 19 && e.getRateTickCount() <= 21, "with Tempo Sync, 1/1 repeats once per clock tick");

        // Preparing again starts the counters from 0
        e.prepare (sr, 512, 2);
        expect (e.getClockTickCount() == 0 && e.getRateTickCount() == 0, "prepare starts the counters again from 0");
    }
}

int main()
{
    testParams();
    testZones();
    testPassThrough();
    testClear();
    testClock();
    testGain();
    testZoneMap();
    testLights();

    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
