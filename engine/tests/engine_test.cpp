// Self-test of the engine (control list, tempo clock, level helpers, the Zone / Rate time map and the counters behind the window's lights;
// milestone 4 adds the repeater: dry path, levels, Mix, echoes in every Zone, Repeats, saturation, Zone jumps, Rate glide, Clear, tail).
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

    // ---- helpers for the repeater tests ----
    bool identical (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
    }

    // The Rate knob position that gives a wanted delay time in a Zone (the inverse of core::freeDelaySeconds).
    float rateForDelay (int zone, double seconds)
    {
        const auto& z = mph::kZones[zone];
        return (float) (std::log (seconds / z.minSeconds) / std::log (z.maxSeconds / z.minSeconds));
    }

    // A starting point for the repeater tests: only the repeats are heard (Mix fully right), one echo, levels at 0 dB.
    mph::Params repeatsOnly (int zone, float rate, float repeats = 0.0f)
    {
        mph::Params p;
        p.zone = zone;
        p.rate = rate;
        p.repeats = repeats;
        p.mix = 1.0f;
        return p;
    }

    // Processes a whole signal (left, and right if given) in blocks of the given size, in place.
    void processAll (mph::Engine& e, std::vector<float>& l, std::vector<float>* r, int block)
    {
        const int total = (int) l.size();
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            float* chans[2] = { l.data() + done, r != nullptr ? r->data() + done : l.data() + done };
            e.process (chans, r != nullptr ? 2 : 1, n);
        }
    }

    int peakIndex (const std::vector<float>& v, int from, int to)
    {
        int best = from;
        for (int i = from; i < to && i < (int) v.size(); ++i)
            if (std::fabs (v[(size_t) i]) > std::fabs (v[(size_t) best]))
                best = i;
        return best;
    }

    // ---- Mix fully left and the levels at 0 dB: the live signal comes out exactly as it went in ----
    void testDryPath()
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
                        // Random settings of everything except the three that decide the live signal's level: it must still be exact.
                        mph::Params p;
                        p.mix = 0.0f;
                        p.repeats = knob (rng); p.halo = knob (rng); p.color = knob (rng); p.rate = knob (rng); p.uRate = knob (rng);
                        p.uRateLfoRate = knob (rng); p.uRateLfoDepth = knob (rng);
                        p.zone = (int) (knob (rng) * 7.99f);
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
                    expect (allSame, "Mix fully left, levels at 0 dB: audio comes out exactly as it went in, random other controls");
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

    // ---- the level controls ----
    void testLevels()
    {
        auto steady = [] (const mph::Params& p, float value)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (p);
            std::vector<float> l (2000, value), r (2000, value);
            processAll (e, l, &r, 500);
            return l.back();
        };

        mph::Params p;
        p.mix = 0.0f;
        expect (std::fabs (steady (p, 0.1f) - 0.1f) < 1.0e-7f, "0 dB in, 0 dB out: 0.1 stays 0.1");

        const float stepDb[4] = { -3.0f, 0.0f, 3.0f, 6.0f };
        for (int s = 0; s < 4; ++s)
        {
            p.inputGainStep = s;
            const float expected = 0.1f * std::pow (10.0f, stepDb[s] / 20.0f);
            expect (std::fabs (steady (p, 0.1f) - expected) < 1.0e-6f, "the input gain steps are -3 / 0 / +3 / +6 dB");
        }
        p.inputGainStep = 1;

        p.inputLevel = 0.0f;
        expect (steady (p, 0.1f) == 0.0f, "Input Level at 0 silences the live signal");
        p.inputLevel = 1.0f;
        expect (std::fabs (steady (p, 0.1f) - 0.1f * 1.9953f) < 1.0e-4f, "Input Level at the top is +6 dB");
        p.inputLevel = mph::kDefaultInputLevel;

        p.outputLevel = 0.0f;
        expect (steady (p, 0.1f) == 0.0f, "Output Level at 0 is silent");
        p.outputLevel = 1.0f;
        expect (std::fabs (steady (p, 0.1f) - 0.1f * 1.9953f) < 1.0e-4f, "Output Level at the top is +6 dB");
        p.outputLevel = mph::kDefaultOutputLevel;

        // Mix is an equal-power balance: at the middle the live signal is 0.7071 (the repeats are still silent at the first sample)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            p.mix = 0.5f;
            e.setParams (p);
            std::vector<float> l (8, 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 8);
            expect (std::fabs (l[0] - 0.70711f) < 1.0e-4f, "Mix in the middle: the live signal is at 0.7071 (-3 dB)");
        }
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            p.mix = 1.0f;
            e.setParams (p);
            std::vector<float> l (8, 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 8);
            expect (std::fabs (l[0]) < 1.0e-6f, "Mix fully right: no live signal");
        }
    }

    // ---- the repeats: where they fall in time, how loud, and the Repeats law ----
    void testRepeatsLaw()
    {
        using namespace mph::core;
        expect (repeatsToFeedback (0.0f) == 0.0f, "Repeats fully left: nothing fed back (one echo)");
        expect (repeatsToFeedback (kRepeatsInfiniteStart) == 1.0f && repeatsToFeedback (kRepeatsInfiniteEnd) == 1.0f
                && repeatsToFeedback (0.83f) == 1.0f, "Repeats has a flat stretch at exactly infinite");
        expect (std::fabs (repeatsToFeedback (1.0f) - kRepeatsBeyondMax) < 1.0e-6f && kRepeatsBeyondMax > 1.0f, "Repeats fully right is beyond infinite");
        bool rising = true;
        float last = -1.0f, biggestStep = 0.0f;
        for (int i = 0; i <= 10000; ++i)
        {
            const float fb = repeatsToFeedback ((float) i / 10000.0f);
            rising = rising && fb >= last;
            if (i > 0)
                biggestStep = std::max (biggestStep, fb - last);
            last = fb;
        }
        expect (rising, "turning Repeats up never gives fewer repeats");
        expect (biggestStep < 0.005f, "Repeats has no jumps");
        expect (std::fabs (repeatsToFeedback (0.4f) - 0.35355f) < 1.0e-4f, "Repeats at 0.4 (the starting position) feeds back 0.354");
        expect (repeatsToFeedback (-1.0f) == 0.0f && repeatsToFeedback (5.0f) == repeatsToFeedback (1.0f), "Repeats outside 0..1 is clamped");

        // The tail the host is told
        expect (std::fabs (tailSeconds (0.5, 0.0f) - 0.5) < 1.0e-9, "one echo: the tail is one delay time");
        expect (std::fabs (tailSeconds (0.5, 0.5f) - 0.5 * 11.0) < 1.0e-9, "feedback 0.5: 10 echoes to -60 dB, plus the first one");
        expect (tailSeconds (0.5, 1.0f) == kTailCapSeconds && tailSeconds (0.5, 1.12f) == kTailCapSeconds, "infinite and beyond: the tail is the cap");
        expect (tailSeconds (41.0, 0.9f) == kTailCapSeconds, "a long tail is held to the cap");
        expect (std::isfinite (tailSeconds (1.0, 0.9999f)) && tailSeconds (1.0, 0.998f) <= kTailCapSeconds, "feedback just below infinite gives a finite tail");

        mph::Engine e;
        e.prepare (48000.0, 512, 2);
        mph::Params p;                                   // starting positions: Zone 4, Rate in the middle (653 ms), Repeats 0.4
        e.setParams (p);
        expect (std::fabs (e.getDelaySeconds() - 0.6531f) < 0.001f, "the engine reports the delay time it aims at (653 ms at the starting positions)");
        expect (std::fabs (e.getFeedback() - 0.35355f) < 1.0e-4f, "... and the feedback");
        expect (std::fabs (e.getTailSeconds() - 8.0 * 0.6531) < 0.02, "... and the tail (8 echoes of 653 ms at the starting positions)");
        p.repeats = 0.0f;
        e.setParams (p);
        expect (std::fabs (e.getTailSeconds() - 0.6531) < 0.002, "Repeats fully left: the tail is one echo");
        p.repeats = 0.83f;
        e.setParams (p);
        expect (e.getTailSeconds() == kTailCapSeconds, "Repeats at infinite: the tail is the cap");
    }

    void testEchoes()
    {
        // Where the first echo falls, in every Zone, at the middle of the Rate knob (the delay is not a whole number of samples
        // here, so the peak may be half a sample off)
        for (int zone = 0; zone < 7; ++zone)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (zone, 0.5f));
            const double delay = mph::core::freeDelaySeconds (zone, 0.5f) * 48000.0;
            std::vector<float> l ((size_t) (delay + 2000.0), 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 512);
            const int peak = peakIndex (l, 20, (int) l.size());
            expect (std::fabs ((double) peak - delay) <= 1.0, "the first echo falls at the Zone's delay time (Rate in the middle)");
            expect (std::fabs (l[(size_t) peak]) > 0.5f, "... and is clearly there");
        }

        // A delay of exactly 9600 samples (0.2 s at 48 kHz): the echo is the whole impulse, and the next ones fall by the feedback
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (2, rateForDelay (2, 0.2), 0.4f));
            std::vector<float> l (9600 * 5, 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 512);
            const float e1 = l[9600], e2 = l[19200], e3 = l[28800];
            const float fb = mph::core::repeatsToFeedback (0.4f);
            expect (std::fabs (e1 - 1.0f) < 0.01f, "echo 1 is as loud as the input (Mix fully right)");
            expect (std::fabs (e2 / e1 - fb) < 0.02f, "echo 2 is echo 1 times the feedback");
            expect (std::fabs (e3 / e2 - fb) < 0.02f, "echo 3 is echo 2 times the feedback");
        }

        // Repeats fully left: one echo only
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (2, rateForDelay (2, 0.2), 0.0f));
            std::vector<float> l (9600 * 4, 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 512);
            expect (std::fabs (l[9600]) > 0.98f && std::fabs (l[19200]) < 1.0e-3f && std::fabs (l[28800]) < 1.0e-3f, "Repeats fully left: one echo, then nothing");
        }

        // Infinite: ten echoes later the echo is still there
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (2, rateForDelay (2, 0.2), 0.83f));
            std::vector<float> l (9600 * 12, 0.0f);
            l[0] = 0.5f;                                   // below the soft limiter's knee (0.8), so nothing is squashed
            processAll (e, l, nullptr, 512);
            expect (l[9600 * 10] > 0.47f && l[9600 * 10] < 0.51f, "Repeats at infinite: the tenth echo is still at full strength");

            // A louder sound is held down a little at every pass by the soft limiter until it sits just under the knee
            mph::Engine loud;
            loud.prepare (48000.0, 512, 2);
            loud.setParams (repeatsOnly (2, rateForDelay (2, 0.2), 0.83f));
            std::vector<float> m (9600 * 62, 0.0f);
            m[0] = 1.0f;
            processAll (loud, m, nullptr, 512);
            expect (m[9600 * 60] > 0.4f && m[9600 * 60] < 0.6f && m[9600 * 60] > 0.9f * m[9600 * 30], "Repeats at infinite: a full-scale sound is levelled to about 0.5 and stays");

            // Beyond infinite, short delay (Bryn's Logic test): the repeats grow slowly, then are held at a steady level - no harsh runaway
            mph::Engine beyond;
            beyond.prepare (48000.0, 512, 2);
            beyond.setParams (repeatsOnly (2, rateForDelay (2, 0.02), 1.0f));
            std::vector<float> b (48000 * 30, 0.0f);
            b[0] = 0.3f;
            processAll (beyond, b, nullptr, 512);
            float peakLate = 0.0f;
            for (size_t i = b.size() - 24000; i < b.size(); ++i)
                peakLate = std::max (peakLate, std::fabs (b[i]));
            expect (peakLate > 0.3f && peakLate < 0.7f, "Beyond infinite, 20 ms: after 30 s the loop sits at a steady level (about 0.6), not clipping");
            expect (b[48000 * 3] < 0.45f, "Beyond infinite, 20 ms: growth is slow (still quiet after 3 s)");
        }

        // The longest delay of the module: Zone 7, Rate fully right, 41.796 s
        for (double sr : { 44100.0, 96000.0 })
        {
            mph::Engine e;
            e.prepare (sr, 4096, 2);
            e.setParams (repeatsOnly (7, 1.0f));
            const double delay = mph::kLongestSeconds * sr;
            std::vector<float> l ((size_t) (delay + 1000.0), 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 4096);
            const int peak = peakIndex (l, 100, (int) l.size());
            expect (std::fabs ((double) peak - delay) <= 1.5 && std::fabs (l[(size_t) peak]) > 0.5f, "Zone 7, Rate fully right: the echo falls at 41.8 s (44.1 and 96 kHz)");
        }

        // The shortest: Zone 0, Rate fully left, 1.3 ms
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (0, 0.0f));
            std::vector<float> l (400, 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 100);
            const double delay = 0.0013 * 48000.0;
            const int peak = peakIndex (l, 20, 400);
            expect (std::fabs ((double) peak - delay) <= 1.0 && std::fabs (l[(size_t) peak]) > 0.5f, "Zone 0, Rate fully left: the echo falls at 1.3 ms");
        }
    }

    // ---- beyond infinite saturates, and bad samples never get into the feedback ----
    void testSaturation()
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> noise (-0.9f, 0.9f);

        for (int zone : { 0, 2 })
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (zone, 0.3f, 1.0f));
            bool finite = true;
            float biggest = 0.0f;
            std::vector<float> l (512), r (512);
            for (int block = 0; block < 48000 * 12 / 512; ++block)
            {
                for (auto& s : l) s = noise (rng);
                for (auto& s : r) s = noise (rng);
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, 2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                    biggest = std::max ({ biggest, std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]) });
                }
            }
            expect (finite, "Repeats beyond infinite with loud noise for 12 s: always a real number");
            expect (biggest < 3.0f, "... and it saturates: the output never runs away");
            expect (biggest > 0.5f, "... and it does fill up with sound");
        }

        // Not-a-number and infinity at the input are dropped, and never get into the repeats
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (0, 0.3f, 0.83f));
            std::vector<float> l (4800, 0.5f), r (4800, 0.5f);
            l[10] = std::nanf (""); l[11] = INFINITY; r[12] = -INFINITY;
            processAll (e, l, &r, 480);
            std::vector<float> l2 (48000, 0.0f), r2 (48000, 0.0f);
            processAll (e, l2, &r2, 480);
            bool finite = true;
            for (size_t i = 0; i < l.size(); ++i) finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            for (size_t i = 0; i < l2.size(); ++i) finite = finite && std::isfinite (l2[i]) && std::isfinite (r2[i]);
            expect (finite, "not-a-number and infinity at the input never get into the repeats");
        }
    }

    // ---- Zone jumps are crossfaded (no pitch effect), Rate glides (a doppler effect) ----
    int risingZeroCrossings (const std::vector<float>& v, int from, int to)
    {
        int count = 0;
        for (int i = std::max (1, from); i < to && i < (int) v.size(); ++i)
            if (v[(size_t) (i - 1)] < 0.0f && v[(size_t) i] >= 0.0f)
                ++count;
        return count;
    }

    void testZoneAndRateMoves()
    {
        const double sr = 48000.0;
        const double hz = 440.0;
        auto sine = [&] (int from, int count, std::vector<float>& out)
        {
            for (int i = 0; i < count; ++i)
                out.push_back (0.5f * (float) std::sin (2.0 * 3.14159265358979 * hz * (double) (from + i) / sr));
        };

        // Zone 2 -> Zone 3 in the middle of a steady tone
        {
            mph::Engine e;
            e.prepare (sr, 480, 2);
            e.setParams (repeatsOnly (2, 0.5f));
            std::vector<float> first, second;
            sine (0, 48000, first);
            processAll (e, first, nullptr, 480);

            e.setParams (repeatsOnly (3, 0.5f));
            sine (48000, 48000, second);
            processAll (e, second, nullptr, 480);

            std::vector<float> all = first;
            all.insert (all.end(), second.begin(), second.end());
            float biggestStep = 0.0f;
            for (int i = 48000 - 200; i < 48000 + 3000; ++i)
                biggestStep = std::max (biggestStep, std::fabs (all[(size_t) i] - all[(size_t) i - 1]));
            expect (biggestStep < 0.15f, "a Zone jump gives no click (the largest step between samples stays small)");

            // After the crossfade the tone is still 440 Hz: no pitch effect. 0.1 s .. 0.6 s after the jump = 220 cycles.
            const int crossings = risingZeroCrossings (all, 48000 + 4800, 48000 + 28800);
            expect (crossings >= 219 && crossings <= 221, "a Zone jump does not change the pitch of the repeats");
        }

        // Rate: moving it glides the delay time, so the pitch of the repeats bends (down while the delay grows)
        {
            mph::Engine e;
            e.prepare (sr, 480, 2);
            e.setParams (repeatsOnly (3, 0.5f));
            std::vector<float> first, second;
            sine (0, 48000, first);
            processAll (e, first, nullptr, 480);

            e.setParams (repeatsOnly (3, 0.6f));
            sine (48000, 48000, second);
            processAll (e, second, nullptr, 480);

            std::vector<float> all = first;
            all.insert (all.end(), second.begin(), second.end());
            const int whileGliding = risingZeroCrossings (all, 48000, 48000 + 2400);       // the first 50 ms: 22 cycles if the pitch were steady
            expect (whileGliding < 18, "turning Rate up bends the pitch of the repeats down while the delay time glides");
            const int settled = risingZeroCrossings (all, 48000 + 48000 - 24000, 48000 + 48000 - 2400);   // well after: steady again
            expect (settled >= 195 && settled <= 199, "... and the pitch is steady again once Rate has settled");
        }
    }

    // ---- Clear ----
    void testClearRepeats()
    {
        std::mt19937 rng (99);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        for (int zone : { 3, 7 })
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (zone, zone == 7 ? 0.5f : 0.5f, 0.7f));
            const double delaySamples = mph::core::freeDelaySeconds (zone, 0.5f) * 48000.0;

            // Fill the repeater with noise, then run on in silence until the echoes are coming back
            std::vector<float> l ((size_t) (delaySamples * 1.5)), r (l.size());
            for (auto& s : l) s = noise (rng);
            for (auto& s : r) s = noise (rng);
            processAll (e, l, &r, 512);
            std::vector<float> l2 ((size_t) (delaySamples * 0.4), 0.0f), r2 (l2.size(), 0.0f);
            processAll (e, l2, &r2, 512);
            float before = 0.0f;
            for (float s : l2) before = std::max (before, std::fabs (s));
            expect (before > 0.01f, "before Clear, the repeats are sounding");

            mph::Params p = repeatsOnly (zone, 0.5f, 0.7f);
            p.clear = true;
            e.setParams (p);
            expect (e.getParams().clear, "the engine has been given Clear");
            std::vector<float> l3 (512 * 8, 0.0f), r3 (l3.size(), 0.0f);
            processAll (e, l3, &r3, 512);
            expect (e.takeClearHandled(), "Clear is noticed");
            float after = 0.0f;
            for (size_t i = 0; i < l3.size(); ++i)
                after = std::max ({ after, std::fabs (l3[i]), std::fabs (r3[i]) });
            expect (after == 0.0f, "Clear: the repeats are gone at once, in the very block it goes on");

            // ... and the audio after Clear is not affected by it
            p.clear = false;
            e.setParams (p);
            std::vector<float> l4 ((size_t) (delaySamples * 1.2), 0.0f), r4 (l4.size(), 0.0f);
            std::fill (l4.begin(), l4.begin() + 2000, 0.4f);
            processAll (e, l4, &r4, 512);
            float echo = 0.0f;
            for (size_t i = (size_t) delaySamples + 100; i < (size_t) delaySamples + 1500; ++i)
                echo = std::max (echo, std::fabs (l4[i]));
            expect (echo > 0.3f, "after Clear, new sound goes into the repeater and comes back as usual");
        }

        // Clear held on for several blocks (as the wrapper does for a moment) clears once, when it goes on, and not again
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p = repeatsOnly (2, rateForDelay (2, 0.2), 0.0f);
            p.clear = true;
            e.setParams (p);
            std::vector<float> l (9600 + 2000, 0.0f);
            for (int i = 1000; i < 3000; ++i) l[(size_t) i] = 0.4f;          // goes in while Clear is still on
            processAll (e, l, nullptr, 512);
            expect (std::fabs (l[1000 + 9600 + 500]) > 0.3f, "sound that comes in while Clear is still held on is not cleared");
        }
    }

    // ---- stereo, mono, block sizes, sample rates ----
    void testChannelsAndBlocks()
    {
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        // Left and right are independent for now (Skew and Ping Pong arrive in milestone 6)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            e.setParams (repeatsOnly (1, 0.5f, 0.6f));
            std::vector<float> l (20000, 0.0f), r (20000, 0.0f);
            l[0] = 1.0f;
            processAll (e, l, &r, 512);
            bool rightSilent = true;
            for (float s : r) rightSilent = rightSilent && s == 0.0f;
            expect (rightSilent, "a sound in the left channel only is not heard in the right (yet)");
        }

        // Mono is the same as the left of stereo
        {
            mph::Engine stereo, mono;
            stereo.prepare (48000.0, 512, 2);
            mono.prepare (48000.0, 512, 1);
            const auto p = repeatsOnly (1, 0.4f, 0.6f);
            stereo.setParams (p);
            mono.setParams (p);
            std::vector<float> a (30000), b, r (30000, 0.0f);
            for (auto& s : a) s = noise (rng);
            b = a;
            processAll (stereo, a, &r, 512);
            processAll (mono, b, nullptr, 512);
            expect (identical (a, b), "a mono track sounds exactly like the left channel of a stereo one");
        }

        // The block size Logic chooses makes no difference to the sound
        {
            std::vector<float> input (60000);
            for (auto& s : input) s = noise (rng);
            std::vector<float> reference;
            for (int block : { 512, 1, 7, 100, 4096, 60000 })
            {
                mph::Engine e;
                e.prepare (48000.0, block, 2);
                mph::Params p = repeatsOnly (0, 0.4f, 0.7f);
                p.mix = 0.6f;
                e.setParams (p);
                std::vector<float> l = input, r = input;
                processAll (e, l, &r, block);
                if (reference.empty())
                    reference = l;
                else
                    expect (identical (reference, l), "the sound does not depend on the block size");
            }
        }

        // Other sample rates: the delay time stays the same in seconds
        for (double sr : { 44100.0, 88200.0, 96000.0, 192000.0 })
        {
            mph::Engine e;
            e.prepare (sr, 512, 2);
            e.setParams (repeatsOnly (3, rateForDelay (3, 0.25)));
            std::vector<float> l ((size_t) (0.3 * sr), 0.0f);
            l[0] = 1.0f;
            processAll (e, l, nullptr, 512);
            const int peak = peakIndex (l, 20, (int) l.size());
            expect (std::fabs ((double) peak - 0.25 * sr) <= 1.0, "the delay time is the same in seconds at 44.1 / 88.2 / 96 / 192 kHz");
        }

        // A very small block size and the sample-by-sample path while Zone changes (no crash, no bad numbers)
        {
            mph::Engine e;
            e.prepare (48000.0, 1, 2);
            bool finite = true;
            std::vector<float> l (1), r (1);
            for (int i = 0; i < 48000; ++i)
            {
                mph::Params p = repeatsOnly ((i / 700) % 8, (float) ((i / 300) % 11) / 10.0f, 0.9f);
                p.mix = 0.5f;
                e.setParams (p);
                l[0] = noise (rng); r[0] = noise (rng);
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, 2, 1);
                finite = finite && std::isfinite (l[0]) && std::isfinite (r[0]);
            }
            expect (finite, "Zone and Rate changed every few hundred samples, one sample at a time: always a real number");
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

        // Tempo Sync is built in milestone 8: until then the button changes nothing, so the light still follows the free-running time
        expect (! mph::kTempoSyncAudible, "Tempo Sync is not audible yet (milestone 8)");
        e.prepare (sr, 512, 2);
        e.setHost (host);
        p = {};
        p.zone = 4;
        p.rate = 0.5f;
        p.tempoSync = true;
        e.setParams (p);
        runSeconds (e, 10.0, sr, 512);
        expect (e.getRateTickCount() >= 14 && e.getRateTickCount() <= 16, "with Tempo Sync on (not working yet) the rate light still counts the free-running time");

        // Preparing again starts the counters from 0
        e.prepare (sr, 512, 2);
        expect (e.getClockTickCount() == 0 && e.getRateTickCount() == 0, "prepare starts the counters again from 0");
    }
}

int main()
{
    testParams();
    testZones();
    testDryPath();
    testLevels();
    testRepeatsLaw();
    testEchoes();
    testSaturation();
    testZoneAndRateMoves();
    testClearRepeats();
    testChannelsAndBlocks();
    testClear();
    testClock();
    testGain();
    testZoneMap();
    testLights();

    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
