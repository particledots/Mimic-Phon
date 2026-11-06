// Self-test of the engine (milestone 2: pass-through, control list, tempo clock, level helpers).
// Prints "engine tests: N checks, F failed" and exits with 0 only if nothing failed.

#include <mph/Engine.h>
#include <mph/Params.h>
#include <mph/core/Gain.h>
#include <mph/core/HostClock.h>

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
}

int main()
{
    testParams();
    testZones();
    testPassThrough();
    testClear();
    testClock();
    testGain();

    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
