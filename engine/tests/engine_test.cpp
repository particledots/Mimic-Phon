// Self-test of the engine (control list, tempo clock, level helpers, the Zone / Rate time map and the counters behind the window's lights;
// milestone 4 adds the repeater: dry path, levels, Mix, echoes in every Zone, Repeats, saturation, Zone jumps, Rate glide, Clear, tail;
// milestone 5 adds Color: the knob's law, unity bit for bit, dark and thin, each echo shaped once more, stability, no clicks;
// milestone 6 adds Skew, Ping Pong and Swap: the rules, the echo times heard on each side, the routing, stability, no clicks;
// milestone 7 adds uRate and its LFO: the rules, the steady offset in every Zone, the LFO's swing and speed, the sync lock, stereo opposite, stability;
// milestone 8 adds Tempo Sync of Rate: the clock, the 13 steps heard, Zones, Clock Division, tempo, Skew sides, jumps instead of glides, stability;
// milestone 9 adds Halo: the rules, off bit for bit, the halo with Repeats fully left (Filter-Verb), size and tail, width, Color's timbre, no run-away;
// milestone 10 adds Flip: the rules, off bit for bit, every repeat reversed, chunk length, no doppler from Rate, uRate still bends, Skew / Ping Pong / Tempo Sync / Clear / Color / Halo with Flip, stability).
// Prints "engine tests: N checks, F failed" and exits with 0 only if nothing failed.

#include <mph/Engine.h>
#include <mph/Params.h>
#include <mph/core/Color.h>
#include <mph/core/Flip.h>
#include <mph/core/Gain.h>
#include <mph/core/Halo.h>
#include <mph/core/HostClock.h>
#include <mph/core/Skew.h>
#include <mph/core/URate.h>
#include <mph/core/ZoneMap.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <functional>
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
            expect (m[9600 * 60] > 0.3f && m[9600 * 60] < 0.6f && m[9600 * 60] > 0.9f * m[9600 * 30], "Repeats at infinite: a full-scale sound is levelled to about 0.4 and stays");

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
            expect (peakLate > 0.3f && peakLate < 0.62f, "Beyond infinite, 20 ms: after 30 s the loop never goes above the leveller ceiling (0.6): no loud start of an echo slips past");
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

        // Tempo Sync (milestone 8): the light follows the clocked time (500 ms at 120 BPM, Zone 4, 1/1) instead of the free-running 653 ms
        e.prepare (sr, 512, 2);
        e.setHost (host);
        p = {};
        p.zone = 4;
        p.rate = 0.5f;
        p.tempoSync = true;
        e.setParams (p);
        runSeconds (e, 10.0, sr, 512);
        expect (e.getRateTickCount() >= 19 && e.getRateTickCount() <= 21, "with Tempo Sync on the rate light counts the clocked time (about 20 in 10 s)");

        // Preparing again starts the counters from 0
        e.prepare (sr, 512, 2);
        expect (e.getClockTickCount() == 0 && e.getRateTickCount() == 0, "prepare starts the counters again from 0");
    }

    // ---- Color (milestone 5) ----
    // Simple measures of a stretch of audio: energy of the first difference (weighted to high frequencies) and of a 32-sample
    // moving average of 128 samples (low frequencies, below about 190 Hz at 48 kHz).
    double highEnergy (const std::vector<float>& v, int from, int to)
    {
        double e = 0.0;
        for (int i = std::max (1, from); i < to && i < (int) v.size(); ++i)
        {
            const double d = (double) v[(size_t) i] - (double) v[(size_t) i - 1];
            e += d * d;
        }
        return e;
    }

    double lowEnergy (const std::vector<float>& v, int from, int to)
    {
        double e = 0.0, sum = 0.0;
        for (int i = std::max (128, from); i < to && i < (int) v.size(); ++i)
        {
            sum = 0.0;
            for (int k = 0; k < 128; ++k)
                sum += (double) v[(size_t) (i - k)];
            e += (sum / 128.0) * (sum / 128.0);
        }
        return e;
    }

    std::vector<float> colourNoise (float color, int zone, double delaySeconds, float repeats, const std::vector<float>& input, int block = 512,
                                    double sampleRate = 48000.0)
    {
        mph::Engine e;
        e.prepare (sampleRate, block, 2);
        mph::Params p = repeatsOnly (zone, rateForDelay (zone, delaySeconds), repeats);
        p.color = color;
        e.setParams (p);
        std::vector<float> l = input;
        processAll (e, l, nullptr, block);
        return l;
    }

    void testColor()
    {
        // The law
        {
            expect (std::strcmp (mph::core::colorWord (mph::kDefaultColor), "clean") == 0, "the starting Color position reads \"clean\"");
            expect (mph::kDefaultColor == mph::core::kColorUnity, "the starting Color position is unity (about 3:00)");
            expect (std::strcmp (mph::core::colorWord (0.0f), "oil can") == 0, "Color fully left reads \"oil can\"");
            expect (std::strcmp (mph::core::colorWord (1.0f), "thin") == 0, "Color fully right reads \"thin\"");
            expect (std::strcmp (mph::core::colorWord (0.4f), "crunch BBD") == 0 && std::strcmp (mph::core::colorWord (0.25f), "warm BBD") == 0
                    && std::strcmp (mph::core::colorWord (0.6f), "tape") == 0 && std::strcmp (mph::core::colorWord (0.88f), "crisp") == 0,
                    "the words in between");

            bool nothingAtUnity = true;
            for (float c = 0.78f; c <= 0.8201f; c += 0.002f)
                nothingAtUnity = nothingAtUnity && mph::core::colorShape (c).amount == 0.0f;
            expect (nothingAtUnity, "Color within 0.02 of unity does nothing at all (amount 0)");
            expect (mph::core::colorShape (0.7f).amount > 0.0f && mph::core::colorShape (0.9f).amount > 0.0f, "away from unity the effect is there");
            expect (mph::core::colorShape (0.0f).amount == 1.0f && mph::core::colorShape (1.0f).amount == 1.0f, "fully left and fully right: the full effect");

            const auto left = mph::core::colorShape (0.0f), right = mph::core::colorShape (1.0f);
            expect (std::fabs (left.lowpassHz - 700.0f) < 1.0f, "fully left: the low-pass is at 700 Hz");
            expect (std::fabs (right.highpassHz - 1500.0f) < 1.0f, "fully right: the high-pass is at 1.5 kHz");
            expect (left.highpassHz == 0.0f && right.lowpassHz == 0.0f, "dark has no high-pass, thin has no low-pass");

            bool darkening = true, thinning = true, drivingLeft = true;
            for (float c = 0.0f; c < 0.77f; c += 0.01f)
            {
                const auto a = mph::core::colorShape (c), b = mph::core::colorShape (c + 0.01f);
                darkening = darkening && a.lowpassHz < b.lowpassHz;              // further left = lower low-pass
                drivingLeft = drivingLeft && a.drive >= b.drive;
            }
            for (float c = 0.83f; c < 0.99f; c += 0.01f)
                thinning = thinning && mph::core::colorShape (c).highpassHz < mph::core::colorShape (c + 0.01f).highpassHz;
            expect (darkening, "left of unity: the further left, the lower the low-pass");
            expect (drivingLeft, "left of unity: the further left, the more saturation");
            expect (thinning, "right of unity: the further right, the higher the high-pass");

            const auto below = mph::core::colorShape (-5.0f), above = mph::core::colorShape (9.0f);
            expect (below.lowpassHz == left.lowpassHz && above.highpassHz == right.highpassHz, "a position outside 0..1 is clamped");
            expect (std::isfinite (mph::core::colorShape (std::nanf ("")).lowpassHz), "not-a-number Color gives a usable shape");
        }

        std::mt19937 rng (77);
        std::uniform_real_distribution<float> small (-0.1f, 0.1f);
        std::vector<float> noise (24000);
        for (auto& s : noise) s = small (rng);

        // Unity (and a little each side): bit for bit the same as no Color at all, through echoes
        {
            const auto reference = colourNoise (0.80f, 2, 0.1, 0.6f, noise);
            bool same = true;
            for (float c : { 0.78f, 0.79f, 0.805f, 0.81f, 0.82f })
                same = same && identical (reference, colourNoise (c, 2, 0.1, 0.6f, noise));
            expect (same, "Color at and around unity: the sound is exactly the same");
            expect (! identical (reference, colourNoise (0.7f, 2, 0.1, 0.6f, noise)), "Color away from unity changes the sound");
        }

        // Dark and thin, measured on one echo of noise (Mix fully right, Repeats fully left, Zone 2 at 100 ms)
        {
            const int from = 4800 + 3000, to = (int) noise.size();
            const auto clean = colourNoise (0.8f, 2, 0.1, 0.0f, noise);
            const double hClean = highEnergy (clean, from, to), lClean = lowEnergy (clean, from, to);

            const auto dark = colourNoise (0.0f, 2, 0.1, 0.0f, noise);
            expect (highEnergy (dark, from, to) < 0.05 * hClean, "Color fully left: the highs are mostly gone");
            expect (lowEnergy (dark, from, to) > 0.25 * lClean && lowEnergy (dark, from, to) < 1.1 * lClean, "Color fully left: the lows are still there");

            const auto thin = colourNoise (1.0f, 2, 0.1, 0.0f, noise);
            expect (lowEnergy (thin, from, to) < 0.05 * lClean, "Color fully right: the lows are mostly gone");
            expect (highEnergy (thin, from, to) > 0.7 * hClean, "Color fully right: the highs are kept");

            double previous = 0.0;
            bool rising = true;
            for (float c : { 0.0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f })
            {
                const double h = highEnergy (colourNoise (c, 2, 0.1, 0.0f, noise), from, to);
                rising = rising && h > previous;
                previous = h;
            }
            expect (rising, "turning Color from the left towards unity: the highs come back steadily");

            // At one echo, Color shapes the echo (the first pass) but the live signal in Mix is untouched
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p;
            p.mix = 0.0f;
            p.color = 0.0f;
            e.setParams (p);
            std::vector<float> l = noise;
            processAll (e, l, nullptr, 512);
            expect (identical (l, noise), "Mix fully left: Color does not touch the live signal");
        }

        // Each echo is shaped once more than the one before: with a dark Color the echoes get darker and darker
        {
            std::vector<float> burst (9600 * 6, 0.0f);
            for (int i = 0; i < 1500; ++i) burst[(size_t) i] = small (rng);
            const auto out = colourNoise (0.3f, 3, 0.2, 0.83f, burst);        // 0.83 = infinite
            auto ratio = [&] (int echo)
            {
                const int a = echo * 9600 + 100, b = echo * 9600 + 1500;
                return highEnergy (out, a, b) / std::max (1.0e-30, lowEnergy (out, a, b));
            };
            expect (ratio (1) > 0.0 && ratio (4) > 0.0, "the echoes are there");
            expect (ratio (4) < 0.6 * ratio (1), "dark Color: the 4th echo is much darker than the 1st (the shaping builds up)");

            const auto outThin = colourNoise (0.95f, 3, 0.2, 0.83f, burst);
            auto lowShare = [&] (int echo)
            {
                const int a = echo * 9600 + 100, b = echo * 9600 + 1500;
                return lowEnergy (outThin, a, b) / std::max (1.0e-30, highEnergy (outThin, a, b));
            };
            expect (lowShare (4) < 0.6 * lowShare (1), "thin Color: the 4th echo is much thinner than the 1st");
        }

        // Stable at the extremes: infinite and beyond, loud noise, short and long delays, 20 seconds, all finite and bounded
        {
            std::uniform_real_distribution<float> loud (-1.0f, 1.0f);
            std::vector<float> input (960000);
            for (auto& s : input) s = loud (rng);
            bool good = true;
            float biggest = 0.0f;
            for (float c : { 0.0f, 0.5f, 0.95f, 1.0f })
                for (float repeats : { 0.83f, 1.0f })
                    for (int zone : { 0, 3 })
                    {
                        mph::Engine e;
                        e.prepare (48000.0, 512, 2);
                        mph::Params p = repeatsOnly (zone, 0.5f, repeats);
                        p.mix = 0.5f;
                        p.color = c;
                        e.setParams (p);
                        std::vector<float> l = input;
                        processAll (e, l, nullptr, 512);
                        for (float s : l)
                        {
                            good = good && std::isfinite (s);
                            biggest = std::max (biggest, std::fabs (s));
                        }
                    }
            expect (good, "Color at the extremes with infinite and beyond: everything stays a real number");
            expect (biggest < 2.5f, "Color at the extremes with infinite and beyond: the level stays bounded");
        }

        // Turning Color: no click
        {
            mph::Engine e;
            e.prepare (48000.0, 256, 2);
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            p.color = 0.8f;
            std::vector<float> l (30000);
            for (size_t i = 0; i < l.size(); ++i)
                l[i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 200.0 * (double) i / 48000.0);
            float biggestStep = 0.0f;
            float last = 0.0f;
            for (int done = 0; done < 30000; done += 256)
            {
                p.color = done < 12000 ? 0.8f : (done < 21000 ? 0.0f : 1.0f);     // jumps: unity -> oil can -> thin
                e.setParams (p);
                const int n = std::min (256, 30000 - done);
                float* chans[2] = { l.data() + done, l.data() + done };
                e.process (chans, 1, n);
                for (int i = 0; i < n; ++i)
                {
                    if (done + i > 200)
                        biggestStep = std::max (biggestStep, std::fabs (l[(size_t) (done + i)] - last));
                    last = l[(size_t) (done + i)];
                }
            }
            expect (biggestStep < 0.03f, "jumping Color from unity to dark to thin: no click (a smooth glide)");
        }

        // Block size makes no difference; other sample rates stay stable and keep the same character
        {
            const auto reference = colourNoise (0.3f, 2, 0.1, 0.5f, noise, 512);
            bool same = true;
            for (int block : { 1, 7, 100, 4096 })
                same = same && identical (reference, colourNoise (0.3f, 2, 0.1, 0.5f, noise, block));
            expect (same, "Color: the sound does not depend on the block size");

            for (double sr : { 44100.0, 96000.0, 192000.0 })
            {
                std::vector<float> in ((size_t) (0.5 * sr));
                for (auto& s : in) s = small (rng);
                const int from = (int) (0.2 * sr), to = (int) in.size();
                const auto clean = colourNoise (0.8f, 2, 0.1, 0.0f, in, 512, sr);
                const auto dark = colourNoise (0.0f, 2, 0.1, 0.0f, in, 512, sr);
                const auto thin = colourNoise (1.0f, 2, 0.1, 0.0f, in, 512, sr);
                // the same corner frequencies in Hertz whatever the sample rate: compare the share of a fixed band, so use the low energy over a
                // window that is 128 samples at 48 kHz (a proportionally longer one at higher rates)
                const double hClean = highEnergy (clean, from, to), lClean = lowEnergy (clean, from, to);
                expect (highEnergy (dark, from, to) < 0.2 * hClean && lowEnergy (thin, from, to) < 0.5 * lClean,
                        "Color works the same way at 44.1 / 96 / 192 kHz (darker on the left, thinner on the right)");
            }
        }

        // Clear empties Color's memory too: nothing rings on after Clear
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p = repeatsOnly (1, rateForDelay (1, 0.03), 0.83f);
            p.color = 0.2f;
            e.setParams (p);
            std::vector<float> l (48000);
            for (auto& s : l) s = small (rng) * 3.0f;
            processAll (e, l, nullptr, 512);
            p.clear = true;
            e.setParams (p);
            std::vector<float> quiet (512, 0.0f);
            processAll (e, quiet, nullptr, 512);
            p.clear = false;
            e.setParams (p);
            std::vector<float> after (48000, 0.0f);
            processAll (e, after, nullptr, 512);
            float biggest = 0.0f;
            for (float s : quiet) biggest = std::max (biggest, std::fabs (s));
            for (float s : after) biggest = std::max (biggest, std::fabs (s));
            expect (biggest == 0.0f, "Clear with a dark Color: nothing rings on afterwards");
        }
    }
    // ---- milestone 6: Skew, Ping Pong, Swap ----
    // Where the strongest sound in a window of a signal is (sample index), or -1 if it is silent.
    int echoAt (const std::vector<float>& v, int from, int to, float threshold = 0.05f)
    {
        float best = 0.0f;
        int index = -1;
        for (int i = from; i < to && i < (int) v.size(); ++i)
            if (std::fabs (v[(size_t) i]) > best)
            {
                best = std::fabs (v[(size_t) i]);
                index = i;
            }
        return best >= threshold ? index : -1;
    }

    // A click at sample 100 on the left and/or the right, Mix fully right, then everything is read back.
    struct Heard { std::vector<float> l, r; };
    Heard clickThrough (mph::Params p, bool clickLeft, bool clickRight, int samples, int block = 512, double sr = 48000.0)
    {
        mph::Engine e;
        e.prepare (sr, block, 2);
        e.setParams (p);
        Heard h;
        h.l.assign ((size_t) samples, 0.0f);
        h.r.assign ((size_t) samples, 0.0f);
        if (clickLeft) h.l[100] = 0.5f;
        if (clickRight) h.r[100] = 0.5f;
        // the parameters are set once; setParams is repeated every block as the plugin does
        for (int done = 0; done < samples; done += block)
        {
            const int n = std::min (block, samples - done);
            e.setParams (p);
            float* chans[2] = { h.l.data() + done, h.r.data() + done };
            e.process (chans, 2, n);
        }
        return h;
    }

    void testSkewRules()
    {
        using mph::core::SkewState;
        using mph::core::ChannelRates;

        // Not engaged: both sides are the Rate knob.
        {
            SkewState s;
            s.update (false, 0.3f);
            const auto r = s.ratesNow (0.3f);
            expect (r.left == r.right && std::fabs (r.left - 0.3f) < 1.0e-6f, "Skew off: both sides follow the Rate knob");
            expect (! s.skewed, "Skew off: not skewed");
        }
        // Engaged at 12:00: no split; Rate clockwise: left shorter (faster), right longer (slower)
        {
            SkewState s;
            s.update (true, 0.5f);
            auto r = s.ratesNow (0.5f);
            expect (std::fabs (r.left - 0.5f) < 1.0e-6f && std::fabs (r.right - 0.5f) < 1.0e-6f && ! s.skewed, "Skew on at 12:00: no skew");
            s.update (true, 0.8f);
            r = s.ratesNow (0.8f);
            expect (r.left < 0.5f && r.right > 0.5f, "Skew on, Rate clockwise: left faster (shorter), right slower (longer)");
            expect (std::fabs ((0.5f - r.left) - (r.right - 0.5f)) < 1.0e-6f, "the two sides move apart by the same amount");
            expect (s.skewed, "a 0.3 turn is skewed");
            s.update (true, 0.2f);
            r = s.ratesNow (0.2f);
            expect (r.left > 0.5f && r.right < 0.5f, "Skew on, Rate counter-clockwise: left slower, right faster");
            s.update (true, 1.0f);
            r = s.ratesNow (1.0f);
            expect (std::fabs (r.left - 0.0f) < 1.0e-6f && std::fabs (r.right - 1.0f) < 1.0e-6f, "Skew fully clockwise from the middle: left at the shortest time, right at the longest");
            s.update (true, 0.0f);
            r = s.ratesNow (0.0f);
            expect (std::fabs (r.left - 1.0f) < 1.0e-6f && std::fabs (r.right - 0.0f) < 1.0e-6f, "Skew fully counter-clockwise: the other way round");
        }
        // Skew off after turning Rate: nothing jumps, the split stays, Rate then moves both sides together
        {
            SkewState s;
            s.update (true, 0.5f);
            s.update (true, 0.7f);
            const auto before = s.ratesNow (0.7f);
            s.update (false, 0.7f);
            const auto after = s.ratesNow (0.7f);
            expect (std::fabs (before.left - after.left) < 1.0e-5f && std::fabs (before.right - after.right) < 1.0e-5f, "Skew off: the delay times do not move");
            expect (s.skewed, "Skew off after turning Rate: the split is kept");
            const float gap = after.right - after.left;
            const auto later = s.ratesNow (0.55f);
            expect (std::fabs ((later.right - later.left) - gap) < 1.0e-5f, "Rate moves both sides together, the gap stays the same");
            expect (later.left < after.left && later.right < after.right, "Rate counter-clockwise moves both sides shorter");
            // the jump flag: engaging at 12:00 and turning is no jump, a pure hold is no jump
            expect (! s.takeJump(), "no jump for engaging at 12:00, turning and letting go");
        }
        // On and off with Rate untouched clears the split and brings the sides together
        {
            SkewState s;
            s.update (true, 0.5f);
            s.update (true, 0.8f);
            s.update (false, 0.8f);
            expect (s.skewed, "a split exists before clearing");
            s.takeJump();
            s.update (true, 0.8f);                 // on again ...
            s.update (false, 0.8f);                // ... and off with Rate untouched
            expect (! s.skewed, "on and off with Rate untouched: the split is cleared");
            const auto r = s.ratesNow (0.8f);
            expect (std::fabs (r.left - r.right) < 1.0e-6f && std::fabs (r.left - 0.8f) < 1.0e-6f, "after clearing both sides follow the Rate knob again");
            expect (s.takeJump(), "clearing a split is a jump (the engine crossfades)");
        }
        // Engaging with Rate away from 12:00 is skewed at once and is a jump
        {
            SkewState s;
            s.update (false, 0.8f);
            s.update (true, 0.8f);
            expect (s.skewed, "Skew on with Rate at 0.8: skewed at once (12:00 = no skew, absolute)");
            expect (s.takeJump(), "... and that is a jump");
        }
        // The two sides stay inside the knob range
        {
            SkewState s;
            s.update (true, 0.9f);
            s.update (true, 1.0f);
            const auto r = s.ratesNow (1.0f);
            expect (r.left >= 0.0f && r.right <= 1.0f && r.right > r.left, "the sides never leave the knob range");
        }
        // Hysteresis: tiny skew near 12:00 does not flutter between Ping Pong and Swap
        {
            SkewState s;
            s.update (true, 0.5f);
            s.update (true, 0.52f);
            expect (s.skewed, "a 0.02 turn is skewed");
            s.update (true, 0.507f);
            expect (s.skewed, "0.007 is still skewed (hysteresis)");
            s.update (true, 0.502f);
            expect (! s.skewed, "0.002 is not skewed");
            s.update (true, 0.508f);
            expect (! s.skewed, "0.008 is not skewed yet (hysteresis)");
        }
        // The button's meaning
        {
            SkewState plain, skewed;
            skewed.update (true, 0.5f);
            skewed.update (true, 0.9f);
            expect (mph::core::pingMode (false, true, skewed) == mph::core::PingMode::off, "button off: no Ping Pong, no Swap");
            expect (mph::core::pingMode (true, true, plain) == mph::core::PingMode::pingPong, "button on, sides together: Ping Pong");
            expect (mph::core::pingMode (true, true, skewed) == mph::core::PingMode::swap, "button on, sides skewed: Swap");
            expect (mph::core::pingMode (true, false, plain) == mph::core::PingMode::off, "mono: no Ping Pong");
        }
    }

    void testSkewSound()
    {
        const double sr = 48000.0;

        // Skew off: both sides are the same delay, as before (a click on each side comes back on its own side)
        {
            mph::Params p = repeatsOnly (2, rateForDelay (2, 0.2), 0.0f);
            const auto h = clickThrough (p, true, false, 48000);
            const int il = echoAt (h.l, 0, (int) h.l.size()), ir = echoAt (h.r, 0, (int) h.r.size());
            expect (il > 0 && std::fabs ((il - 100) / sr - 0.2) < 0.001 && ir < 0, "Skew off: a left click echoes on the left only, after the delay");
        }

        // Skew on, Rate clockwise: left echo comes sooner than right
        {
            mph::Params p = repeatsOnly (2, 0.8f, 0.0f);       // Rate at 0.8 with Skew engaged at once: centre 0.8, skew 0.3
            p.skew = true;
            const auto left = clickThrough (p, true, false, 96000);
            const auto right = clickThrough (p, false, true, 96000);
            const int il = echoAt (left.l, 0, (int) left.l.size()), ir = echoAt (right.r, 0, (int) right.r.size());
            const double wantLeft = mph::core::freeDelaySeconds (2, 0.8f - 0.3f), wantRight = mph::core::freeDelaySeconds (2, 1.0f);
            expect (il > 0 && std::fabs ((il - 100) / sr - wantLeft) < 0.002, "Skew on: the left echo is at the left side's time");
            expect (ir > 0 && std::fabs ((ir - 100) / sr - wantRight) < 0.002, "Skew on: the right echo is at the right side's time");
            expect (il < ir, "Rate clockwise: left sooner (faster), right later (slower)");
        }

        // Skew on at 12:00: both sides equal
        {
            mph::Params p = repeatsOnly (3, 0.5f, 0.0f);
            p.skew = true;
            const auto left = clickThrough (p, true, false, 96000);
            const auto right = clickThrough (p, false, true, 96000);
            const int il = echoAt (left.l, 0, (int) left.l.size()), ir = echoAt (right.r, 0, (int) right.r.size());
            expect (il > 0 && il == ir, "Skew on at 12:00: both sides have the same time");
        }

        // Skew then Skew off: the split stays and Rate moves both together (measured by following the engine through the button presses)
        {
            mph::Engine e;
            e.prepare (sr, 512, 2);
            mph::Params p = repeatsOnly (3, 0.5f, 0.0f);
            std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
            p.skew = true;
            e.setParams (p);
            for (int i = 0; i < 4; ++i) { processAll (e, l, &r, 512); }
            p.rate = 0.8f;                             // turn Rate clockwise with Skew on
            e.setParams (p);
            processAll (e, l, &r, 512);
            p.skew = false;                            // Skew off: split stays
            e.setParams (p);
            processAll (e, l, &r, 512);
            const auto before = e.getSkewState().ratesNow (p.rate);
            p.rate = 0.7f;                             // Rate now moves both sides together (a small move, so neither side meets the end of the knob)
            e.setParams (p);
            const auto after = e.getSkewState().ratesNow (p.rate);
            expect (std::fabs ((after.right - after.left) - (before.right - before.left)) < 1.0e-5f, "engine: with Skew off the split stays when Rate moves");
            expect (after.left < before.left && after.right < before.right, "engine: with Skew off Rate moves both sides together");
        }

        // The reported delay (what the window shows) follows the sides
        {
            mph::Engine e;
            e.prepare (sr, 512, 2);
            mph::Params p = repeatsOnly (4, 0.8f, 0.0f);
            p.skew = true;
            e.setParams (p);
            const auto s = e.getSkewState();
            const auto rr = s.ratesNow (p.rate);
            expect (s.engaged && s.skewed && rr.left < rr.right, "engine: the published Skew memory is what the window needs");
        }
    }

    void testPingPong()
    {
        const double sr = 48000.0;

        // Ping Pong: both inputs summed to mono into the left; repeats bounce left, right, left ... each hop half the delay time
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.4), 0.83f);       // delay 0.4 s, infinite
            p.pingPong = true;
            const auto h = clickThrough (p, true, false, 192000);
            const double hop = 0.2;
            // first echo: left, after half the delay time
            const int first = echoAt (h.l, 0, (int) (0.3 * sr), 0.02f);
            expect (first > 0 && std::fabs ((first - 100) / sr - hop) < 0.002, "Ping Pong: the first echo is on the left after half the delay time");
            expect (echoAt (h.r, 0, (int) (0.3 * sr), 0.02f) < 0, "Ping Pong: nothing on the right before the second hop");
            // second echo: right, one delay time after the click
            const int second = echoAt (h.r, (int) (0.3 * sr), (int) (0.5 * sr), 0.02f);
            expect (second > 0 && std::fabs ((second - 100) / sr - 2 * hop) < 0.002, "Ping Pong: the second echo is on the right, one hop later");
            // third: left again
            const int third = echoAt (h.l, (int) (0.5 * sr), (int) (0.7 * sr), 0.02f);
            expect (third > 0 && std::fabs ((third - 100) / sr - 3 * hop) < 0.002, "Ping Pong: the third echo is back on the left");
            expect (echoAt (h.l, (int) (0.3 * sr), (int) (0.5 * sr), 0.02f) < 0, "Ping Pong: nothing on the left while the echo is on the right");
        }

        // A click on the RIGHT input only is also heard, starting on the left (inputs are summed to mono, left side first)
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.4), 0.0f);
            p.pingPong = true;
            const auto h = clickThrough (p, false, true, 96000);
            const int first = echoAt (h.l, 0, 96000, 0.02f);
            expect (first > 0 && std::fabs ((first - 100) / sr - 0.2) < 0.002 && echoAt (h.r, 0, 96000, 0.02f) < 0,
                    "Ping Pong: a right-only click is summed to mono and the echo starts on the left");
        }

        // Mono sum is a mean, so the same click on both inputs comes back at the same level as one echo of a mono signal
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.4), 0.0f);
            p.pingPong = true;
            const auto both = clickThrough (p, true, true, 96000);
            const auto one = clickThrough (p, true, false, 96000);
            const int ib = echoAt (both.l, 0, 96000, 0.02f), io = echoAt (one.l, 0, 96000, 0.02f);
            expect (ib > 0 && io > 0 && std::fabs (both.l[(size_t) ib]) > 1.9f * std::fabs (one.l[(size_t) io]) * 0.5f
                    && std::fabs (both.l[(size_t) ib] - 0.5f * std::fabs (both.l[(size_t) ib]) * 2.0f) < 1.0f,
                    "Ping Pong: inputs are averaged into the left side");
            expect (std::fabs (std::fabs (both.l[(size_t) ib]) - 0.5f) < 0.02f && std::fabs (std::fabs (one.l[(size_t) io]) - 0.25f) < 0.02f,
                    "Ping Pong: the same click on both inputs echoes at full level, a click on one input at half (a mean)");
        }

        // The dry signal stays stereo (Mix fully left is still bit for bit)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p;
            p.mix = 0.0f;
            p.pingPong = true;
            e.setParams (p);
            std::vector<float> l (4096), r (4096);
            std::mt19937 rng (3);
            std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
            for (auto& s : l) s = noise (rng);
            for (auto& s : r) s = noise (rng);
            const auto l0 = l, r0 = r;
            processAll (e, l, &r, 512);
            expect (identical (l, l0) && identical (r, r0), "Ping Pong with Mix fully left: the stereo input comes out exactly as it went in");
        }

        // Swap: skewed, the feedback paths are swapped (left echo goes on to the right side, whose time is longer), inputs stay stereo
        {
            mph::Params p = repeatsOnly (3, 0.8f, 0.83f);
            p.skew = true;
            p.pingPong = true;                         // skewed: Swap
            const auto h = clickThrough (p, true, false, 192000);
            const double dl = mph::core::freeDelaySeconds (3, 0.5f), dr = mph::core::freeDelaySeconds (3, 1.0f);
            const int first = echoAt (h.l, 0, (int) (dl * sr + 2000), 0.02f);
            expect (first > 0 && std::fabs ((first - 100) / sr - dl) < 0.002, "Swap: the first echo is on the left at the left side's time (inputs stay stereo)");
            const int second = echoAt (h.r, 0, (int) ((dl + dr) * sr + 4000), 0.02f);
            expect (second > 0 && std::fabs ((second - 100) / sr - (dl + dr)) < 0.003, "Swap: the left echo feeds the right side: it comes back on the right after both times");
            expect (echoAt (h.r, 0, (int) (dl * sr + 1000), 0.02f) < 0, "Swap: nothing reaches the right before both times have passed");
        }

        // Without Ping Pong a click stays on its own side, forever (no crossing)
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.2), 0.83f);
            const auto h = clickThrough (p, true, false, 96000);
            float biggestRight = 0.0f;
            for (float s : h.r) biggestRight = std::max (biggestRight, std::fabs (s));
            expect (biggestRight == 0.0f, "Ping Pong off: nothing leaks to the other side");
        }

        // Mono: Skew and Ping Pong / Swap do nothing and do not crash
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 1);
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.2), 0.0f);
            p.skew = true;
            p.pingPong = true;
            p.rate = 0.8f;
            e.setParams (p);
            std::vector<float> l (48000, 0.0f);
            l[100] = 0.5f;
            processAll (e, l, nullptr, 512);
            const int i = echoAt (l, 0, 48000, 0.05f);
            expect (i > 0 && std::fabs ((i - 100) / 48000.0 - mph::core::freeDelaySeconds (3, 0.8f)) < 0.003, "mono: Skew and Ping Pong do nothing (the delay is the Rate knob's)");
        }
    }

    // Stability and smoothness
    void testSkewStability()
    {
        std::mt19937 rng (777);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::uniform_real_distribution<float> knob (0.0f, 1.0f);

        // Random button presses and knob moves at infinite and beyond: finite, bounded, always
        for (int zone : { 0, 1, 3, 5 })
            for (float repeats : { 0.83f, 1.0f })
            {
                mph::Engine e;
                e.prepare (48000.0, 256, 2);
                std::vector<float> l (256), r (256);
                float biggest = 0.0f;
                bool finite = true;
                mph::Params p = repeatsOnly (zone, 0.5f, repeats);
                for (int block = 0; block < 1500; ++block)
                {
                    if (block % 40 == 0) p.skew = (rng() & 1);
                    if (block % 55 == 0) p.pingPong = (rng() & 1);
                    if (block % 17 == 0) p.rate = knob (rng);
                    if (block % 90 == 0) p.zone = (int) (knob (rng) * 7.99f);
                    e.setParams (p);
                    for (int i = 0; i < 256; ++i)
                    {
                        l[(size_t) i] = block < 40 ? 0.5f * noise (rng) : 0.0f;
                        r[(size_t) i] = block < 40 ? 0.5f * noise (rng) : 0.0f;
                    }
                    float* chans[2] = { l.data(), r.data() };
                    e.process (chans, 2, 256);
                    for (int i = 0; i < 256; ++i)
                    {
                        finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                        biggest = std::max ({ biggest, std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]) });
                    }
                }
                expect (finite && biggest < 1.5f, "random Skew / Ping Pong / Rate / Zone moves at infinite and beyond: finite and bounded");
            }

        // No click when Ping Pong goes on and off with a steady sound running through the repeats
        {
            const double sr = 48000.0;
            mph::Engine e;
            e.prepare (sr, 480, 2);
            mph::Params p = repeatsOnly (2, rateForDelay (2, 0.25), 0.5f);
            p.mix = 0.5f;
            std::vector<float> l (480), r (480);
            double phase = 0.0;
            float biggestStep = 0.0f, previous = 0.0f;
            for (int block = 0; block < 400; ++block)
            {
                if (block == 120) p.pingPong = true;
                if (block == 260) p.pingPong = false;
                e.setParams (p);
                for (int i = 0; i < 480; ++i)
                {
                    const float s = (float) (0.3 * std::sin (phase));
                    phase += 2.0 * 3.14159265358979 * 220.0 / sr;
                    l[(size_t) i] = r[(size_t) i] = s;
                }
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, 2, 480);
                if (block >= 100)
                    for (int i = 0; i < 480; ++i)
                    {
                        biggestStep = std::max (biggestStep, std::fabs (l[(size_t) i] - previous));
                        previous = l[(size_t) i];
                    }
            }
            expect (biggestStep < 0.2f, "Ping Pong on and off over a steady tone: no click");
        }

        // No click when Skew goes on (away from 12:00) and when it is cleared: the engine crossfades
        {
            const double sr = 48000.0;
            mph::Engine e;
            e.prepare (sr, 480, 2);
            mph::Params p = repeatsOnly (2, 0.8f, 0.5f);
            std::vector<float> l (480), r (480);
            double phase = 0.0;
            float biggestStep = 0.0f, previous = 0.0f;
            for (int block = 0; block < 400; ++block)
            {
                if (block == 120) p.skew = true;
                if (block == 220) p.skew = false;      // untouched: clears
                e.setParams (p);
                for (int i = 0; i < 480; ++i)
                {
                    const float s = (float) (0.3 * std::sin (phase));
                    phase += 2.0 * 3.14159265358979 * 220.0 / sr;
                    l[(size_t) i] = r[(size_t) i] = s;
                }
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, 2, 480);
                if (block >= 100)
                    for (int i = 0; i < 480; ++i)
                    {
                        biggestStep = std::max (biggestStep, std::fabs (l[(size_t) i] - previous));
                        previous = l[(size_t) i];
                    }
            }
            expect (biggestStep < 0.2f, "Skew on away from 12:00 and cleared again over a steady tone: no click");
        }

        // Block size makes no difference to where the echoes are
        {
            mph::Params p = repeatsOnly (3, 0.8f, 0.5f);
            p.skew = true;
            p.pingPong = true;
            const auto a = clickThrough (p, true, true, 96000, 64);
            const auto b = clickThrough (p, true, true, 96000, 1000);
            const int ia = echoAt (a.l, 0, 96000, 0.01f), ib = echoAt (b.l, 0, 96000, 0.01f);
            expect (ia > 0 && ia == ib, "block size 64 or 1000: the same Swap echo time");
        }

        // The sample rate makes no difference to the time
        for (double sr : { 44100.0, 96000.0, 192000.0 })
        {
            mph::Params p = repeatsOnly (3, 0.8f, 0.0f);
            p.skew = true;
            const auto h = clickThrough (p, true, true, (int) (sr * 1.2), 512, sr);
            const int il = echoAt (h.l, 0, (int) (sr * 1.2), 0.05f), ir = echoAt (h.r, 0, (int) (sr * 1.2), 0.05f);
            expect (il > 0 && ir > 0 && std::fabs ((il - 100) / sr - mph::core::freeDelaySeconds (3, 0.5f)) < 0.003
                    && std::fabs ((ir - 100) / sr - mph::core::freeDelaySeconds (3, 1.0f)) < 0.003,
                    "Skew gives the same times at 44.1 / 96 / 192 kHz");
        }

        // Clear empties both sides, also with Ping Pong running
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p = repeatsOnly (1, rateForDelay (1, 0.03), 0.83f);
            p.pingPong = true;
            e.setParams (p);
            std::vector<float> l (48000), r (48000);
            for (auto& s : l) s = noise (rng) * 0.3f;
            processAll (e, l, &r, 512);
            p.clear = true;
            e.setParams (p);
            std::vector<float> ql (512, 0.0f), qr (512, 0.0f);
            processAll (e, ql, &qr, 512);
            p.clear = false;
            e.setParams (p);
            std::vector<float> al (48000, 0.0f), ar (48000, 0.0f);
            processAll (e, al, &ar, 512);
            float biggest = 0.0f;
            for (float s : ql) biggest = std::max (biggest, std::fabs (s));
            for (float s : qr) biggest = std::max (biggest, std::fabs (s));
            for (float s : al) biggest = std::max (biggest, std::fabs (s));
            for (float s : ar) biggest = std::max (biggest, std::fabs (s));
            expect (biggest == 0.0f, "Clear with Ping Pong running: nothing rings on afterwards");
        }

        // The RATE light counts both sides when they differ (more blinks than with one side)
        {
            auto blinks = [] (bool skew)
            {
                mph::Engine e;
                e.prepare (48000.0, 512, 2);
                mph::Params p = repeatsOnly (3, 0.8f, 0.0f);
                p.skew = skew;
                e.setParams (p);
                std::vector<float> l (96000, 0.0f), r (96000, 0.0f);
                processAll (e, l, &r, 512);
                return e.getRateTickCount();
            };
            expect (blinks (true) > blinks (false), "the RATE light blinks for both sides when Skew splits them");
        }
    }
    // ---- milestone 7: uRate and its LFO ----

    // A slow ramp goes in on both sides (Mix fully right, one echo), so what comes out says exactly how long the delay was at every
    // moment: delay (samples) = n - out / step. A cubic read of a straight line is exact, so this measures the engine itself.
    struct Trace { std::vector<double> l, r; };
    constexpr double kRampStep = 5.0e-6;
    Trace delayTrace (mph::Params p, int samples, bool playing = false, double ppqStart = 0.0, double bpm = 120.0, int from = 48000)
    {
        mph::Engine e;
        e.prepare (48000.0, 512, 2);
        std::vector<float> l ((size_t) samples), r ((size_t) samples);
        for (int i = 0; i < samples; ++i)
            l[(size_t) i] = r[(size_t) i] = (float) (i * kRampStep);
        std::vector<float> outL = l, outR = r;
        double ppq = ppqStart;
        for (int done = 0; done < samples; done += 512)
        {
            const int n = std::min (512, samples - done);
            mph::core::HostInfo host;
            host.tempoValid = true; host.bpm = bpm; host.playing = playing; host.ppqValid = true; host.ppq = ppq;
            e.setParams (p);
            e.setHost (host);
            float* chans[2] = { outL.data() + done, outR.data() + done };
            e.process (chans, 2, n);
            ppq += n * bpm / 60.0 / 48000.0;
        }
        Trace t;
        for (int i = from; i < samples; ++i)
        {
            t.l.push_back (i - outL[(size_t) i] / kRampStep);
            t.r.push_back (i - outR[(size_t) i] / kRampStep);
        }
        return t;
    }
    double lowest (const std::vector<double>& v) { double m = 1.0e30; for (double x : v) m = std::min (m, x); return m; }
    double highest (const std::vector<double>& v) { double m = -1.0e30; for (double x : v) m = std::max (m, x); return m; }
    double meanOf (const std::vector<double>& v) { double s = 0.0; for (double x : v) s += x; return v.empty() ? 0.0 : s / (double) v.size(); }
    double together (const std::vector<double>& a, const std::vector<double>& b)      // > 0: the two swing the same way
    {
        const double ma = meanOf (a), mb = meanOf (b);
        double s = 0.0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) s += (a[i] - ma) * (b[i] - mb);
        return s;
    }

    void testURateRules()
    {
        using namespace mph::core;
        expect (uRateBipolar (0.5f) == 0.0 && uRateBipolar (0.504f) == 0.0 && uRateBipolar (0.496f) == 0.0, "uRate: exactly zero around the middle");
        expect (std::fabs (uRateBipolar (1.0f) - 1.0) < 1.0e-9 && std::fabs (uRateBipolar (0.0f) + 1.0) < 1.0e-9, "uRate: the far ends are -1 and +1");
        bool rising = true;
        for (int i = 1; i <= 100; ++i)
            rising = rising && uRateBipolar ((float) i / 100.0f) >= uRateBipolar ((float) (i - 1) / 100.0f);
        expect (rising, "uRate: turning clockwise never goes down");
        expect (uRateStaticDelay (2, 0.2, 0.5f, 0, false) == 0.2, "uRate in the middle: the delay is untouched");
        expect (std::fabs (uRateStaticDelay (2, 0.2, 1.0f, 0, false) - 0.194) < 1.0e-9, "Zones 1-7: clockwise = 6 ms shorter");
        expect (std::fabs (uRateStaticDelay (2, 0.2, 0.0f, 0, false) - 0.206) < 1.0e-9, "Zones 1-7: counter-clockwise = 6 ms longer");
        expect (std::fabs (uRateStaticDelay (2, 0.2, 1.0f, 1, true) - 0.206) < 1.0e-9, "Zones 1-7 with Skew: the right side is opposite");
        expect (std::fabs (uRateStaticDelay (2, 0.2, 1.0f, 1, false) - 0.194) < 1.0e-9, "Zones 1-7 without Skew: both sides alike");
        expect (std::fabs (uRateStaticDelay (0, 0.01, 1.0f, 0, false) - 0.005) < 1.0e-9, "Zone 0: clockwise = an octave up (half the delay)");
        expect (std::fabs (uRateStaticDelay (0, 0.01, 0.0f, 0, false) - 0.02) < 1.0e-9, "Zone 0: counter-clockwise = an octave down");
        expect (std::fabs (uRateStaticDelay (0, 0.01, 1.0f, 1, true) - 0.005) < 1.0e-9, "Zone 0: Skew does not touch uRate");

        expect (std::fabs (lfoAmount (0.5f) - 0.25) < 1.0e-9 && lfoAmount (0.0f) == 0.0 && lfoAmount (1.0f) == 1.0, "LFO Depth: a squared curve");
        expect (std::fabs (lfoFreeHz (0.0f) - 0.1) < 1.0e-9 && std::fabs (lfoFreeHz (1.0f) - 20.0) < 1.0e-9, "LFO Rate: 0.1 Hz to 20 Hz");
        expect (lfoSyncStep (0.0f) == 0 && lfoSyncStep (1.0f) == kNumClockDivisions - 1 && lfoSyncStep (0.35f) == 3, "LFO Sync: ten bands, slowest on the left");
        expect (std::fabs (lfoHz (0.35f, true, 120.0, 4.0) - 2.0) < 1.0e-9, "LFO Sync: a quarter note at 120 BPM is 2 Hz");
        expect (std::fabs (lfoHz (0.0f, true, 120.0, 4.0) - 0.25) < 1.0e-9, "LFO Sync: two bars of 4/4 at 120 BPM is 0.25 Hz");
        expect (std::fabs (lfoHz (0.35f, true, 0.0, 4.0) - 2.0) < 1.0e-9, "LFO Sync: no tempo from Logic counts as 120 BPM");
        expect (std::fabs (lfoPeakSeconds (1.0f, 1.0) - 0.012) < 1.0e-9, "LFO Depth at the far end: +-12 ms at ordinary speeds");
        expect (std::fabs (lfoPeakSeconds (1.0f, 20.0) - 0.6 / (kTwoPi * 20.0)) < 1.0e-9, "LFO Depth: held back at fast speeds");
        expect (lfoPeakSeconds (0.0f, 5.0) == 0.0 && lfoPeakSemitones (0.0f) == 0.0, "LFO Depth at zero: no swing");
        expect (lfoPeakSeconds (0.3f, 1.0) < 0.2 * lfoPeakSeconds (1.0f, 1.0), "LFO Depth: shallow over the first part of the knob");
        expect (std::fabs (lfoValue (mph::kLfoSine, 0.25) - 1.0) < 1.0e-9 && std::fabs (lfoValue (mph::kLfoSine, 0.75) + 1.0) < 1.0e-9, "LFO: sine peaks");
        expect (lfoValue (mph::kLfoTriangle, 0.0) == 0.0 && lfoValue (mph::kLfoTriangle, 0.25) == 1.0 && lfoValue (mph::kLfoTriangle, 0.75) == -1.0
                    && std::fabs (lfoValue (mph::kLfoTriangle, 0.5)) < 1.0e-12, "LFO: triangle peaks and zero crossings");
    }

    void testURateSound()
    {
        const double sr = 48000.0;

        // Middle position, Depth at zero (the defaults): nothing touched. A knob inside the dead zone gives bit-identical audio.
        {
            std::mt19937 rng (777);
            std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
            auto run = [&] (float knob)
            {
                mph::Engine e;
                e.prepare (sr, 512, 2);
                mph::Params p = repeatsOnly (3, 0.6f, 0.5f);
                p.mix = 0.6f;
                p.uRate = knob;
                std::mt19937 local (42);
                std::vector<float> l (30000), r (30000);
                for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (local); r[i] = noise (local); }
                for (int done = 0; done < 30000; done += 512)
                {
                    e.setParams (p);
                    float* chans[2] = { l.data() + done, r.data() + done };
                    e.process (chans, 2, std::min (512, 30000 - done));
                }
                l.insert (l.end(), r.begin(), r.end());
                return l;
            };
            (void) rng;
            expect (identical (run (0.5f), run (0.502f)), "uRate around the middle with Depth at zero: audio bit for bit the same");
            expect (! identical (run (0.5f), run (0.8f)), "uRate away from the middle changes the sound");
        }

        // A steady offset: the delay is the Zone's plus the knob's, in every Zone, the same on both sides
        {
            struct Case { int zone; double seconds; float knob; };
            const Case cases[] = { { 2, 0.2, 1.0f }, { 2, 0.2, 0.0f }, { 4, 0.8, 1.0f }, { 1, 0.04, 0.0f }, { 0, 0.01, 1.0f }, { 0, 0.01, 0.0f }, { 0, 0.004, 0.75f } };
            for (const auto& c : cases)
            {
                mph::Params p = repeatsOnly (c.zone, rateForDelay (c.zone, c.seconds), 0.0f);
                p.uRate = c.knob;
                const auto t = delayTrace (p, 150000);
                const double want = mph::core::uRateStaticDelay (c.zone, c.seconds, c.knob, 0, false) * sr;
                expect (std::fabs (meanOf (t.l) - want) < 0.5 && highest (t.l) - lowest (t.l) < 0.15, "uRate: the steady offset is the delay heard (left)");
                expect (std::fabs (meanOf (t.r) - want) < 0.5, "uRate: the steady offset is the delay heard (right)");
            }
        }

        // With Skew: Zones 1-7 the right side is opposite, Zone 0 is not touched by Skew
        {
            for (int zone : { 2, 0 })
            {
                mph::Params p = repeatsOnly (zone, 0.8f, 0.0f);          // Skew engaged at once: left at 0.5, right at 1.0
                p.skew = true;
                p.uRate = 1.0f;
                const auto t = delayTrace (p, 150000);
                const double baseL = mph::core::freeDelaySeconds (zone, 0.5f), baseR = mph::core::freeDelaySeconds (zone, 1.0f);
                const double wantL = mph::core::uRateStaticDelay (zone, baseL, 1.0f, 0, true) * sr;
                const double wantR = mph::core::uRateStaticDelay (zone, baseR, 1.0f, 1, true) * sr;
                expect (std::fabs (meanOf (t.l) - wantL) < 1.0 && std::fabs (meanOf (t.r) - wantR) < 1.0,
                        zone == 0 ? "uRate with Skew, Zone 0: both sides the same offset (octave up)" : "uRate with Skew, Zones 1-7: the right side is opposite");
            }
        }

        // The LFO's swing (read off the delay itself): peak = Depth's curve, in time (Zones 1-7) or semitones (Zone 0), sine and triangle
        {
            const float oneHz = (float) (std::log (1.0 / 0.1) / std::log (20.0 / 0.1));
            for (int shape : { mph::kLfoSine, mph::kLfoTriangle })
            {
                mph::Params p = repeatsOnly (4, rateForDelay (4, 0.4), 0.0f);
                p.uRateLfoRate = oneHz;
                p.uRateLfoDepth = 1.0f;
                p.uRateLfoShape = shape;
                const auto t = delayTrace (p, 144000);
                const double want = 0.012 * sr;
                expect (std::fabs (meanOf (t.l) - 0.4 * sr) < 20.0, "LFO: swings around the steady delay");
                expect (std::fabs ((highest (t.l) - lowest (t.l)) / 2.0 - want) < 0.03 * want,
                        shape == mph::kLfoSine ? "LFO sine, full Depth: +-12 ms" : "LFO triangle, full Depth: +-12 ms");

                p.uRateLfoDepth = 0.5f;
                const auto half = delayTrace (p, 144000);
                expect (std::fabs ((highest (half.l) - lowest (half.l)) / 2.0 - 0.25 * want) < 0.05 * 0.25 * want, "LFO, half Depth: a quarter of the swing (squared curve)");
            }
            mph::Params z = repeatsOnly (0, rateForDelay (0, 0.01), 0.0f);
            z.uRateLfoRate = oneHz;
            z.uRateLfoDepth = 0.5f;
            const auto t0 = delayTrace (z, 144000);
            expect (std::fabs (highest (t0.l) - 0.01 * sr * std::exp2 (3.0 / 12.0)) < 0.02 * 0.01 * sr
                        && std::fabs (lowest (t0.l) - 0.01 * sr * std::exp2 (-3.0 / 12.0)) < 0.02 * 0.01 * sr,
                    "LFO in Zone 0: a pitch swing in semitones (half Depth = +-3 st)");
        }

        // Fast LFO: the swing is held back (the pitch never swings wilder than the limit)
        {
            mph::Params p = repeatsOnly (4, rateForDelay (4, 0.4), 0.0f);
            p.uRateLfoRate = 1.0f;                      // 20 Hz
            p.uRateLfoDepth = 1.0f;
            const auto t = delayTrace (p, 144000);
            const double want = 0.6 / (mph::core::kTwoPi * 20.0) * sr;
            expect (std::fabs ((highest (t.l) - lowest (t.l)) / 2.0 - want) < 0.06 * want, "LFO at 20 Hz: the swing is held back to the pitch limit");
        }

        // Stereo: with Skew splitting the sides the LFO swings opposite on the right (Zones 1-7); not in Zone 0; not without Skew
        {
            const float oneHz = (float) (std::log (1.0 / 0.1) / std::log (20.0 / 0.1));
            auto together01 = [&] (int zone, bool skew)
            {
                mph::Params p = repeatsOnly (zone, skew ? 0.8f : 0.5f, 0.0f);
                p.skew = skew;
                p.uRateLfoRate = oneHz;
                p.uRateLfoDepth = zone == 0 ? 0.6f : 0.8f;
                const auto t = delayTrace (p, 144000);
                return together (t.l, t.r);
            };
            expect (together01 (2, false) > 0.0, "LFO without Skew: both sides swing together");
            expect (together01 (2, true) < 0.0, "LFO with Skew, Zones 1-7: the right side swings opposite (stereo chorus)");
            expect (together01 (0, true) > 0.0, "LFO with Skew, Zone 0: both sides still swing together");
        }

        // LFO Sync: locked to the song position while Logic plays, and at the tempo's speed while it is stopped
        {
            mph::Params p = repeatsOnly (4, rateForDelay (4, 0.4), 0.0f);
            p.uRateLfoSync = true;
            p.uRateLfoRate = 0.35f;                     // 1/4: one cycle per beat = 2 Hz at 120 BPM
            p.uRateLfoDepth = 1.0f;
            const auto t = delayTrace (p, 144000, true, 0.25, 120.0, 0);
            bool locked = true;
            for (int n : { 60000, 70000, 90000, 100000, 130000 })
            {
                const double phase = 0.25 + (double) n * 2.0 / sr;
                const double want = 0.4 * sr - 0.012 * sr * std::sin (mph::core::kTwoPi * phase);
                locked = locked && std::fabs (t.l[(size_t) n] - want) < 25.0;
            }
            expect (locked, "LFO Sync while Logic plays: the swing follows the song position");

            const auto stopped = delayTrace (p, 144000, false, 0.0, 120.0, 48000);
            const double m = meanOf (stopped.l);
            int up = 0;
            for (size_t i = 1; i < stopped.l.size(); ++i)
                if (stopped.l[i - 1] < m && stopped.l[i] >= m)
                    ++up;
            expect (up >= 3 && up <= 5, "LFO Sync while Logic is stopped: runs at the tempo's speed (2 Hz = 4 cycles in 2 s)");

            const auto slower = [&]
            {
                mph::Params q = p;
                const auto t2 = delayTrace (q, 144000, true, 0.0, 60.0, 48000);       // 60 BPM: 1 Hz
                const double m2 = meanOf (t2.l);
                int c = 0;
                for (size_t i = 1; i < t2.l.size(); ++i)
                    if (t2.l[i - 1] < m2 && t2.l[i] >= m2)
                        ++c;
                return c;
            }();
            expect (slower >= 1 && slower <= 3, "LFO Sync: half the tempo, half the speed");
        }
    }

    void testURateStability()
    {
        const double sr = 48000.0;
        std::mt19937 rng (2026);
        std::uniform_real_distribution<float> knob (0.0f, 1.0f);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
        bool allFinite = true, bounded = true;
        for (int trial = 0; trial < 60; ++trial)
        {
            const int channels = trial % 6 == 5 ? 1 : 2;
            mph::Engine e;
            e.prepare (sr, 512, channels);
            std::vector<float> l (48000), r (48000);
            for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
            for (int done = 0; done < 48000; done += 512)
            {
                mph::Params p;
                p.mix = 0.3f + 0.7f * knob (rng);
                p.repeats = knob (rng); p.color = knob (rng); p.rate = knob (rng);
                p.uRate = knob (rng); p.uRateLfoRate = knob (rng); p.uRateLfoDepth = knob (rng);
                p.zone = (int) (knob (rng) * 7.99f);
                p.uRateLfoShape = trial & 1;
                p.skew = (trial % 3) == 0; p.pingPong = (trial % 4) == 0; p.uRateLfoSync = (trial % 2) == 0;
                mph::core::HostInfo host;
                host.tempoValid = true; host.bpm = 40.0 + 200.0 * knob (rng); host.playing = (trial % 5) != 0;
                host.ppqValid = true; host.ppq = 50.0 * knob (rng);
                e.setParams (p);
                e.setHost (host);
                float* chans[2] = { l.data() + done, r.data() + done };
                e.process (chans, channels, std::min (512, 48000 - done));
            }
            for (size_t i = 0; i < l.size(); ++i)
            {
                allFinite = allFinite && std::isfinite (l[i]) && std::isfinite (r[i]);
                bounded = bounded && std::fabs (l[i]) < 8.0f && std::fabs (r[i]) < 8.0f;
            }
        }
        expect (allFinite, "uRate and LFO with random controls, Zones, Skew, Ping Pong, sync: always finite");
        expect (bounded, "uRate and LFO with random controls: output stays bounded");

        // Turning uRate from the middle to a far end while a tone plays: smooth, no click
        {
            mph::Engine e;
            e.prepare (sr, 512, 2);
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.3), 0.0f);
            std::vector<float> l (96000), r (96000);
            for (size_t i = 0; i < l.size(); ++i)
                l[i] = r[i] = 0.4f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * (double) i / sr);
            for (int done = 0; done < 96000; done += 512)
            {
                p.uRate = done < 48000 ? 0.5f : 1.0f;
                e.setParams (p);
                float* chans[2] = { l.data() + done, r.data() + done };
                e.process (chans, 2, std::min (512, 96000 - done));
            }
            float biggest = 0.0f;
            for (size_t i = 30001; i < l.size(); ++i)
                biggest = std::max (biggest, std::fabs (l[i] - l[i - 1]));
            expect (biggest < 0.12f, "turning uRate: no click (sample to sample steps stay small)");
        }

        // A Zone change while the LFO swings: no crash, finite, and the new Zone's own rule is used afterwards
        {
            mph::Engine e;
            e.prepare (sr, 512, 2);
            std::vector<float> l (96000), r (96000);
            for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = l[i]; }
            bool finite = true;
            for (int done = 0; done < 96000; done += 512)
            {
                mph::Params p = repeatsOnly ((done / 2048) % 3, 0.5f, 0.6f);
                p.uRateLfoDepth = 0.9f; p.uRateLfoRate = 0.6f; p.uRate = 0.9f;
                e.setParams (p);
                float* chans[2] = { l.data() + done, r.data() + done };
                e.process (chans, 2, std::min (512, 96000 - done));
            }
            for (size_t i = 0; i < l.size(); ++i)
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            expect (finite, "Zone changes while uRate and the LFO work: finite");
        }
    }

    // ---- milestone 8: Tempo Sync of Rate ----

    // Like delayTrace, but the settings and the host can change from block to block (change gets the block number, 512 samples each).
    Trace delayTraceChanging (int samples, const std::function<void (int, mph::Params&, mph::core::HostInfo&)>& change)
    {
        mph::Engine e;
        e.prepare (48000.0, 512, 2);
        std::vector<float> l ((size_t) samples), r ((size_t) samples);
        for (int i = 0; i < samples; ++i)
            l[(size_t) i] = r[(size_t) i] = (float) (i * kRampStep);
        std::vector<float> outL = l, outR = r;
        mph::Params p;
        mph::core::HostInfo host;
        host.tempoValid = true;
        host.bpm = 120.0;
        for (int done = 0; done < samples; done += 512)
        {
            change (done / 512, p, host);
            e.setParams (p);
            e.setHost (host);
            float* chans[2] = { outL.data() + done, outR.data() + done };
            e.process (chans, 2, std::min (512, samples - done));
        }
        Trace t;
        for (int i = 0; i < samples; ++i)
        {
            t.l.push_back (i - outL[(size_t) i] / kRampStep);
            t.r.push_back (i - outR[(size_t) i] / kRampStep);
        }
        return t;
    }

    void testTempoSyncRules()
    {
        using namespace mph::core;
        expect (mph::kTempoSyncAudible, "Tempo Sync is audible (milestone 8)");
        expect (std::fabs (clockPeriodSeconds (3, 4.0, 120.0) - 0.5) < 1e-12, "clock: a quarter note at 120 BPM is 500 ms");
        expect (std::fabs (clockPeriodSeconds (7, 4.0, 120.0) - 0.125) < 1e-12, "clock: a 16th at 120 BPM is 125 ms");
        expect (std::fabs (clockPeriodSeconds (1, 4.0, 120.0) - 2.0) < 1e-12 && std::fabs (clockPeriodSeconds (1, 3.0, 120.0) - 1.5) < 1e-12,
                "clock: a bar follows the host's bar length");
        expect (std::fabs (clockPeriodSeconds (3, 4.0, 0.0) - 0.5) < 1e-12 && std::fabs (clockPeriodSeconds (3, 4.0, -5.0) - 0.5) < 1e-12
                    && std::fabs (clockPeriodSeconds (3, 4.0, std::nan ("")) - 0.5) < 1e-12, "clock: no tempo from Logic counts as 120 BPM");
        expect (std::fabs (clockPeriodSeconds (3, 4.0, 5.0) - 6.0) < 1e-12 && std::fabs (clockPeriodSeconds (3, 4.0, 5000.0) - 60.0 / 999.0) < 1e-12,
                "clock: a tempo is kept between 10 and 999 BPM");
        expect (bestZoneForPeriod (clockPeriodSeconds (7, 4.0, 120.0)) == 2, "a 16th at 120 BPM fits Zone 2 (yellow), as on the module at 120 BPM");
        expect (bestZoneForPeriod (clockPeriodSeconds (3, 4.0, 120.0)) == 4, "a quarter at 120 BPM fits Zone 4");
        // the 13 steps, left to right: x2 ... 1/1 ... /2, each one slower than the one before
        bool slower = true;
        for (int i = 1; i < kNumSyncSteps; ++i)
            slower = slower && syncDelaySeconds (4, ((float) i + 0.5f) / 13.0f, 0.5) > syncDelaySeconds (4, ((float) i - 0.5f) / 13.0f, 0.5);
        expect (slower, "Tempo Sync: turning Rate clockwise never makes the time shorter, one step at a time");
        expect (std::strcmp (kSyncNames[0], "x2") == 0 && std::strcmp (kSyncNames[6], "1/1") == 0 && std::strcmp (kSyncNames[12], "/2") == 0, "the steps are named as in the manual");
    }

    void testTempoSyncSound()
    {
        const double sr = 48000.0;
        auto syncOnly = [] (int zone, float rate)
        {
            mph::Params p = repeatsOnly (zone, rate, 0.0f);
            p.tempoSync = true;
            return p;
        };

        // The delay heard is the clock's: 1/1 in the middle of Rate is exactly one clock period (500 ms at 120 BPM, a quarter note, Zone 4)
        {
            const auto t = delayTrace (syncOnly (4, 0.5f), 150000);
            expect (std::fabs (meanOf (t.l) - 0.5 * sr) < 0.5 && highest (t.l) - lowest (t.l) < 0.15, "Tempo Sync: 1/1 in Zone 4 at 120 BPM is 500 ms (left)");
            expect (std::fabs (meanOf (t.r) - 0.5 * sr) < 0.5, "Tempo Sync: 1/1 in Zone 4 at 120 BPM is 500 ms (right)");
        }
        // All 13 steps, heard
        {
            bool all = true;
            for (int i = 0; i < mph::core::kNumSyncSteps; ++i)
            {
                const auto t = delayTrace (syncOnly (4, ((float) i + 0.5f) / 13.0f), 150000);
                const double want = 0.5 / mph::core::kSyncFactors[i] * sr;
                all = all && std::fabs (meanOf (t.l) - want) < 0.5 && std::fabs (meanOf (t.r) - want) < 0.5 && highest (t.l) - lowest (t.l) < 0.15;
            }
            expect (all, "Tempo Sync: all 13 steps give the clock period divided by the step's factor, on both sides");
        }
        // The Zone doubles or halves the time per Zone
        {
            struct Case { int zone; double seconds; };
            const Case cases[] = { { 5, 1.0 }, { 3, 0.25 }, { 2, 0.125 }, { 1, 0.0625 } };
            for (const auto& c : cases)
            {
                const auto t = delayTrace (syncOnly (c.zone, 0.5f), 150000);
                expect (std::fabs (meanOf (t.l) - c.seconds * sr) < 0.5, "Tempo Sync: each Zone away from the best one doubles or halves the time");
            }
        }
        // The Clock Division and Logic's tempo are the clock
        {
            mph::Params p = syncOnly (3, 0.5f);
            p.clockDivision = 5;                                     // 1/8: 250 ms at 120 BPM, which fits Zone 3
            expect (std::fabs (meanOf (delayTrace (p, 150000).l) - 0.25 * sr) < 0.5, "Tempo Sync: Clock Division 1/8 at 120 BPM, Zone 3: 250 ms");
            p = syncOnly (4, 0.5f);
            p.clockDivision = 4;                                     // 1/4T: 333 ms, which fits Zone 3, so Zone 4 doubles it
            expect (std::fabs (meanOf (delayTrace (p, 150000).l) - (2.0 / 3.0) * sr) < 0.5, "Tempo Sync: Clock Division 1/4T, Zone 4: 667 ms");
            expect (std::fabs (meanOf (delayTrace (syncOnly (4, 0.5f), 150000, false, 0.0, 90.0).l) - (2.0 / 3.0) * sr) < 0.5, "Tempo Sync: 90 BPM quarter notes, Zone 4: 667 ms");
            mph::Engine e;
            e.prepare (sr, 512, 2);
            e.setParams (syncOnly (4, 0.5f));
            expect (std::fabs (e.getDelaySeconds() - 0.5) < 1e-6, "Tempo Sync with no tempo from Logic: counted as 120 BPM, 500 ms");
        }
        // Tempo Sync off: the clock means nothing
        {
            mph::Params p = repeatsOnly (4, 0.5f, 0.0f);
            const auto a = delayTrace (p, 100000);
            p.clockDivision = 7;
            const auto b = delayTrace (p, 100000, false, 0.0, 77.0);
            expect (std::fabs (meanOf (a.l) - meanOf (b.l)) < 1e-9 && std::fabs (meanOf (a.l) - mph::core::freeDelaySeconds (4, 0.5f) * sr) < 0.5, "Tempo Sync off: Clock Division and tempo change nothing (653 ms)");
        }
        // Rate inside one step: nothing moves
        {
            const auto t = delayTrace (syncOnly (4, 0.50f), 100000);
            const auto u = delayTrace (syncOnly (4, 0.53f), 100000);
            expect (std::fabs (meanOf (t.l) - meanOf (u.l)) < 1e-9, "Tempo Sync: Rate anywhere inside one step gives the same time");
        }

        // A step is a jump, not a bend: after 30 ms the new time is there (a glide would still be on its way)
        {
            auto stepAt = [&] (bool sync)
            {
                return delayTraceChanging (96000, [&] (int block, mph::Params& p, mph::core::HostInfo&)
                {
                    p = repeatsOnly (4, block < 100 ? 0.5f : 0.05f, 0.0f);      // step 6 (500 ms) to step 0 (250 ms)
                    p.tempoSync = sync;
                });
            };
            const int at = 100 * 512, later = at + 1600;                        // 33 ms on
            const auto synced = stepAt (true), free = stepAt (false);
            expect (std::fabs (synced.l[(size_t) (at - 10)] - 0.5 * sr) < 0.5, "Tempo Sync step: before the step, 500 ms");
            expect (std::fabs (synced.l[(size_t) later] - 0.25 * sr) < 0.5 && std::fabs (synced.r[(size_t) later] - 0.25 * sr) < 0.5, "Tempo Sync step: 33 ms after it the new time (250 ms) is there");
            expect (std::fabs (free.l[(size_t) later] - synced.l[(size_t) later]) > 1000.0, "(free-running Rate glides: still on its way after 33 ms)");
            bool between = true;                                                 // the crossfade moves in one direction only
            for (int i = at; i < later; ++i)
                between = between && synced.l[(size_t) i] <= 0.5 * sr + 0.5 && synced.l[(size_t) i] >= 0.25 * sr - 0.5;
            expect (between, "Tempo Sync step: the time goes straight from old to new (no overshoot)");
        }
        // The same for a new Clock Division and for the button itself
        {
            auto run = [&] (int what)
            {
                return delayTraceChanging (96000, [&] (int block, mph::Params& p, mph::core::HostInfo&)
                {
                    p = repeatsOnly (4, 0.5f, 0.0f);
                    p.tempoSync = true;
                    if (block >= 100)
                    {
                        if (what == 0) p.clockDivision = 4;       // 1/4T: 500 ms to 667 ms
                        if (what == 1) p.tempoSync = false;       // 500 ms to the free-running 653 ms
                    }
                });
            };
            const int later = 100 * 512 + 1600;
            const auto division = run (0), button = run (1);
            expect (std::fabs (division.l[(size_t) later] - (2.0 / 3.0) * sr) < 0.5, "a new Clock Division jumps to its time within 33 ms");
            expect (std::fabs (button.l[(size_t) later] - mph::core::freeDelaySeconds (4, 0.5f) * sr) < 0.5, "Tempo Sync switched off jumps to the free-running time within 33 ms");
        }
        // The best-fitting Zone moving with the tempo is a jump too (an octave), not a bend
        {
            const auto t = delayTraceChanging (96000, [&] (int block, mph::Params& p, mph::core::HostInfo& host)
            {
                p = repeatsOnly (4, 0.5f, 0.0f);
                p.tempoSync = true;
                host.bpm = block < 100 ? 128.0 : 132.0;               // 469 ms (best Zone 4) to 455 ms (best Zone 3: Zone 4 doubles it)
            });
            const int later = 100 * 512 + 1600;
            const double before = 60.0 / 128.0, after = 2.0 * 60.0 / 132.0;
            expect (std::fabs (t.l[(size_t) (100 * 512 - 10)] - before * sr) < 0.5, "tempo across the Zone boundary: before, 469 ms");
            expect (std::fabs (t.l[(size_t) later] - after * sr) < 0.5, "tempo across the Zone boundary: the new time (909 ms) is there within 33 ms (a jump)");
        }
        // A tempo that drifts inside one Zone is followed by the normal Rate glide (no jump)
        {
            const auto t = delayTraceChanging (96000, [&] (int block, mph::Params& p, mph::core::HostInfo& host)
            {
                p = repeatsOnly (4, 0.5f, 0.0f);
                p.tempoSync = true;
                host.bpm = block < 100 ? 100.0 : 110.0;                // 600 ms to 545 ms
            });
            const int soon = 100 * 512 + 1600;
            expect (t.l[(size_t) soon] > 0.545 * sr + 300.0 && t.l[(size_t) soon] < 0.6 * sr - 100.0 && t.l[(size_t) 95999] < 0.5457 * sr, "a tempo change inside one Zone glides to the new time (no jump)");
        }

        // With Skew the two sides pick their own steps: two multiples at once
        {
            const auto t = delayTraceChanging (200000, [&] (int block, mph::Params& p, mph::core::HostInfo&)
            {
                p = repeatsOnly (4, block < 40 ? 0.5f : 0.8f, 0.0f);
                p.tempoSync = true;
                p.skew = true;                                        // Rate 12:00 then 0.8: left 0.2 (x1 2/3), right 0.8 (/1 2/3)
            });
            const double left = 0.5 / mph::core::kSyncFactors[2], right = 0.5 / mph::core::kSyncFactors[10];
            expect (std::fabs (meanOf (std::vector<double> (t.l.end() - 20000, t.l.end())) - left * sr) < 0.5, "Tempo Sync with Skew: the left side is on x1 2/3 (300 ms)");
            expect (std::fabs (meanOf (std::vector<double> (t.r.end() - 20000, t.r.end())) - right * sr) < 0.5, "Tempo Sync with Skew: the right side is on /1 2/3 (833 ms)");
        }
        // Ping Pong: each hop is half the clocked time
        {
            mph::Params p = syncOnly (4, 0.5f);
            p.pingPong = true;
            const auto t = delayTrace (p, 150000);
            expect (std::fabs (meanOf (t.l) - 0.25 * sr) < 0.5, "Tempo Sync with Ping Pong: each hop is half the clocked time");
        }
        // uRate still bends the pitch while Tempo Sync is on (the manual: doppler stays on the microRate input)
        {
            mph::Params p = syncOnly (4, 0.5f);
            p.uRate = 1.0f;
            const auto t = delayTrace (p, 150000);
            expect (std::fabs (meanOf (t.l) - (0.5 - 0.006) * sr) < 0.5, "Tempo Sync with uRate: the steady offset is still added");
        }
        // The RATE light follows the clocked time: a blink every 500 ms
        {
            mph::Engine e;
            e.prepare (sr, 512, 2);
            mph::core::HostInfo host;
            host.tempoValid = true;
            host.bpm = 120.0;
            host.playing = true;
            host.ppqValid = true;
            e.setHost (host);
            mph::Params p = syncOnly (4, 0.5f);
            e.setParams (p);
            std::vector<float> l (480000, 0.0f), r (480000, 0.0f);
            processAll (e, l, &r, 512);
            expect (e.getRateTickCount() >= 19 && e.getRateTickCount() <= 21, "Tempo Sync: the RATE light blinks every 500 ms (about 20 in 10 s)");
        }
        // No click on a step: a steady tone, a step, and the output never changes faster than the tone itself
        {
            auto run = [&] (bool step)
            {
                mph::Engine e;
                e.prepare (sr, 512, 2);
                std::vector<float> l (96000), r (96000);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] = r[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / sr);
                for (int done = 0; done < 96000; done += 512)
                {
                    mph::Params p = repeatsOnly (4, (step && done >= 60 * 512) ? 0.05f : 0.5f, 0.0f);
                    p.tempoSync = true;
                    e.setParams (p);
                    float* chans[2] = { l.data() + done, r.data() + done };
                    e.process (chans, 2, std::min (512, 96000 - done));
                }
                double worst = 0.0;
                for (size_t i = 40000; i + 1 < l.size(); ++i)
                    worst = std::max (worst, (double) std::fabs (l[i + 1] - l[i]));
                return worst;
            };
            expect (run (true) <= run (false) * 1.05 + 0.002, "Tempo Sync: a step under a playing tone does not click");
        }
    }

    void testTempoSyncStability()
    {
        std::mt19937 rng (4242);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        std::uniform_real_distribution<float> noise (-0.9f, 0.9f);
        bool finite = true, bounded = true;
        for (int run = 0; run < 40; ++run)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, (run % 5 == 0) ? 1 : 2);
            const int channels = (run % 5 == 0) ? 1 : 2;
            std::vector<float> l (512), r (512);
            for (int block = 0; block < 250; ++block)
            {
                mph::Params p;
                p.tempoSync = true;
                p.zone = (int) (rng() % 8);
                p.rate = unit (rng);                                   // a new step almost every block: the jumps pile up
                p.repeats = run % 2 ? 1.0f : unit (rng);
                p.mix = unit (rng);
                p.skew = rng() & 1;
                p.pingPong = rng() & 1;
                p.clockDivision = (int) (rng() % mph::core::kNumClockDivisions);
                p.uRate = unit (rng);
                p.uRateLfoDepth = unit (rng);
                mph::core::HostInfo host;
                host.tempoValid = (rng() % 4) != 0;
                host.bpm = 20.0 + 280.0 * unit (rng);
                host.playing = rng() & 1;
                host.ppqValid = true;
                host.ppq = block * 0.1;
                e.setParams (p);
                e.setHost (host);
                for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, channels, 512);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                    bounded = bounded && std::fabs (l[i]) < 8.0f && std::fabs (r[i]) < 8.0f;
                }
            }
        }
        expect (finite && bounded, "Tempo Sync: 40 random runs (steps changing every block, Zones, Skew, Ping Pong, tempo, mono too, infinite repeats) stay finite and bounded");
    }


    // ======================= milestone 9: Halo =======================

    // Runs the engine over a whole signal with fixed settings (48 kHz, 512-sample blocks); returns what it made (left, and right for stereo).
    struct HaloRun { std::vector<float> l, r; double tail = 0.0; };
    HaloRun runHalo (const mph::Params& p, std::vector<float> inL, std::vector<float> inR, int channels = 2)
    {
        mph::Engine e;
        e.prepare (48000.0, 512, channels);
        e.setParams (p);
        HaloRun out;
        out.l = std::move (inL);
        out.r = inR.empty() ? out.l : std::move (inR);
        const int total = (int) out.l.size();
        for (int done = 0; done < total; done += 512)
        {
            const int n = std::min (512, total - done);
            e.setParams (p);
            float* chans[2] = { out.l.data() + done, out.r.data() + done };
            e.process (chans, channels, n);
        }
        out.tail = e.getTailSeconds();
        return out;
    }

    std::vector<float> impulse (int length, int at, float amount)
    {
        std::vector<float> v ((size_t) length, 0.0f);
        v[(size_t) at] = amount;
        return v;
    }

    double energy (const std::vector<float>& v, int from, int to)
    {
        double e = 0.0;
        for (int i = from; i < to && i < (int) v.size(); ++i)
            e += (double) v[(size_t) i] * v[(size_t) i];
        return e;
    }

    double peakAbs (const std::vector<float>& v, int from, int to)
    {
        double m = 0.0;
        for (int i = from; i < to && i < (int) v.size(); ++i)
            m = std::max (m, (double) std::fabs (v[(size_t) i]));
        return m;
    }

    void testHaloRules()
    {
        using namespace mph::core;
        expect (haloShape (0.0f).level == 0.0f && haloShape (kHaloDeadZone).level == 0.0f, "Halo fully left (and within the dead zone) gives no halo");
        expect (haloShape (0.01f).level > 0.0f, "Halo just above the dead zone gives some halo");

        bool levelRises = true, sizeRises = true, feedbackBelowOne = true, crossRises = true, tailRises = true;
        HaloShape last = haloShape (0.02f);
        double lastTail = haloTailSeconds (0.02f);
        for (int i = 3; i <= 100; ++i)
        {
            const float k = (float) i / 100.0f;
            const HaloShape s = haloShape (k);
            levelRises = levelRises && s.level >= last.level - 1.0e-6f;
            sizeRises = sizeRises && s.tankSeconds > last.tankSeconds && s.diffScale > last.diffScale;
            crossRises = crossRises && s.crossRadians > last.crossRadians;
            feedbackBelowOne = feedbackBelowOne && s.feedback < 0.9f && s.feedback >= kHaloFeedbackMin - 1.0e-6f;
            const double tail = haloTailSeconds (k);
            tailRises = tailRises && tail >= lastTail && tail <= kHaloTailCapSeconds;
            last = s;
            lastTail = tail;
        }
        expect (levelRises, "Halo level rises with the knob");
        expect (sizeRises, "Halo's size (tank and allpass times) grows with the knob");
        expect (crossRises, "the stereo rotation grows with the knob (wider)");
        expect (feedbackBelowOne, "the tank's loop gain stays below 0.9 over the whole knob, so it cannot run away");
        expect (tailRises, "Halo's tail grows with the knob and stays under its cap");
        expect (haloShape (1.0f).level > 0.999f, "Halo fully right is the full level");
        expect (haloShape (0.5f).tankSeconds < 0.025f && haloShape (1.0f).tankSeconds > 0.055f, "middle = a short tank (tight), fully right = a long one (hall)");
        expect (haloShape (0.5f).inject == 0.0f && haloShape (0.55f).inject == 0.0f && haloShape (0.8f).inject > 0.0f && haloShape (1.0f).inject > haloShape (0.8f).inject,
                "the halo is injected into the feedback only at high settings");
        expect (haloTailSeconds (0.0f) == 0.0 && haloTailSeconds (1.0f) > 1.5, "no tail with Halo off, a few seconds fully right");

        expect (haloMix (0.7f, 123.0f, 0.0f) == 0.7f, "haloMix with no level is the repeat, exactly");
        expect (std::fabs (haloMix (0.8f, 0.3f, 1.0f) - (0.4f + 0.3f)) < 1.0e-6f, "haloMix at full level = half the repeat plus the halo");

        const HaloTimbre unity = haloTimbre (colorShape (kColorUnity));
        const HaloTimbre dark = haloTimbre (colorShape (0.0f));
        const HaloTimbre thin = haloTimbre (colorShape (1.0f));
        expect (std::fabs (unity.dampHz - kHaloDampHz) < 1.0e-6 && unity.highpassHz == 0.0, "Color at unity leaves Halo's timbre at its default");
        expect (dark.dampHz < 1000.0 && dark.highpassHz == 0.0, "a dark Color closes Halo's damping");
        expect (thin.highpassHz > 500.0 && std::fabs (thin.dampHz - kHaloDampHz) < 1.0e-6, "a thin Color high-passes what Halo hears");

        expect (std::strcmp (haloWord (0.0f), "off") == 0 && std::strcmp (haloWord (0.1f), "glint") == 0 && std::strcmp (haloWord (0.3f), "string") == 0
                    && std::strcmp (haloWord (0.5f), "tight verb") == 0 && std::strcmp (haloWord (0.7f), "room") == 0 && std::strcmp (haloWord (1.0f), "wide hall") == 0,
                "the window's words for Halo");
    }

    void testHaloSound()
    {
        const int total = 48000 * 3;

        // Off, and within the dead zone: bit for bit as without Halo
        {
            std::mt19937 rng (4242);
            std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
            std::vector<float> a ((size_t) 48000), b ((size_t) 48000);
            for (size_t i = 0; i < a.size(); ++i) { a[i] = noise (rng); b[i] = noise (rng); }
            mph::Params p;
            p.repeats = 0.6f;
            p.mix = 0.6f;
            p.zone = 2;
            p.halo = 0.0f;
            const auto off = runHalo (p, a, b);
            p.halo = 0.003f;
            const auto near = runHalo (p, a, b);
            expect (identical (off.l, near.l) && identical (off.r, near.r), "Halo within its dead zone changes nothing, bit for bit");
            p.halo = 0.3f;
            const auto on = runHalo (p, a, b);
            expect (! identical (off.l, on.l), "Halo on changes the sound");
        }

        // Filter-Verb: Repeats fully left, Mix fully right. One echo, and with Halo its halo.
        {
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            p.halo = 0.0f;
            const auto plain = runHalo (p, impulse (total, 100, 0.5f), {});
            expect (energy (plain.l, 600, total) == 0.0, "no Halo, Repeats fully left: one echo and then silence");
            p.halo = 0.5f;
            const auto verb = runHalo (p, impulse (total, 100, 0.5f), {});
            expect (energy (verb.l, 600, total) > 1.0e-4, "Halo acts with Repeats fully left and Mix fully right (Filter-Verb)");
            expect (energy (verb.l, 600, total) > 1000.0 * energy (plain.l, 600, total) + 1.0e-6, "the Filter-Verb halo is a clear tail, not noise");
        }

        // Size and tail: more Halo rings longer
        {
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            double tails[4];
            const float knobs[4] = { 0.15f, 0.4f, 0.7f, 1.0f };
            for (int i = 0; i < 4; ++i)
            {
                p.halo = knobs[i];
                const auto r = runHalo (p, impulse (total, 100, 0.5f), {});
                tails[i] = energy (r.l, 12000, 60000) / std::max (1.0e-12, energy (r.l, 0, 12000));
            }
            expect (tails[0] < tails[1] && tails[1] < tails[2] && tails[2] < tails[3], "the later part of the halo grows with the knob (longer tail)");
        }

        // It does not run away: fully right, one echo, the tail dies
        {
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            p.halo = 1.0f;
            const auto r = runHalo (p, impulse (48000 * 8, 100, 0.5f), {});
            const double early = peakAbs (r.l, 0, 24000);
            const double late = peakAbs (r.l, 48000 * 7, 48000 * 8);
            expect (early > 0.05, "Halo fully right: a clear start");
            expect (late < 1.0e-3 * early, "Halo fully right dies away (under -60 dB after 7 s): it does not run away");
        }

        // Width: the same mono signal in both channels comes out different on the two sides
        {
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            p.halo = 1.0f;
            const auto r = runHalo (p, impulse (total, 100, 0.5f), {});
            double ll = 0.0, rr = 0.0, lr = 0.0;
            for (int i = 4000; i < 100000; ++i)
            {
                ll += (double) r.l[(size_t) i] * r.l[(size_t) i];
                rr += (double) r.r[(size_t) i] * r.r[(size_t) i];
                lr += (double) r.l[(size_t) i] * r.r[(size_t) i];
            }
            const double correlation = lr / std::sqrt (std::max (1.0e-30, ll * rr));
            expect (ll > 1.0e-4 && rr > 1.0e-4, "both sides carry the halo");
            expect (std::fabs (correlation) < 0.5, "Halo fully right: a wide stereo image (the two sides are not alike)");
            p.halo = 0.0f;
            const auto off = runHalo (p, impulse (total, 100, 0.5f), {});
            expect (identical (off.l, off.r), "without Halo a mono signal stays the same on both sides");
        }

        // Color sets Halo's timbre: dark = dull, thin = bright
        {
            std::mt19937 rng (777);
            std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
            std::vector<float> burst ((size_t) 48000, 0.0f);
            for (int i = 0; i < 2000; ++i)
                burst[(size_t) (100 + i)] = noise (rng);
            auto brightness = [&] (float color)
            {
                mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
                p.halo = 0.8f;
                p.color = color;
                const auto r = runHalo (p, burst, {});
                double d = 0.0, e = 1.0e-30;
                for (int i = 6000; i < 40000; ++i)
                {
                    const double dv = (double) r.l[(size_t) i] - (double) r.l[(size_t) i - 1];
                    d += dv * dv;
                    e += (double) r.l[(size_t) i] * r.l[(size_t) i];
                }
                return d / e;
            };
            const double dark = brightness (0.05f), unity = brightness (0.8f), thin = brightness (1.0f);
            expect (dark < 0.5 * unity, "a dark Color makes a dark halo");
            expect (thin > 1.5 * unity, "a thin Color makes a thin, bright halo");
        }

        // Repeats fully left: no loop through the delay buffer (the halo is not fed back); with Repeats up, the repeats come round again
        {
            mph::Params p = repeatsOnly (2, 0.0f, 0.0f);
            p.halo = 0.2f;
            const int delaySamples = (int) std::lround (mph::kZones[2].minSeconds * 48000.0);
            const auto one = runHalo (p, impulse (total, 100, 0.5f), {});
            const double first = peakAbs (one.l, 0, 2 * delaySamples - 500);
            const double second = peakAbs (one.l, 2 * delaySamples + 2500, 2 * delaySamples + 20000);
            expect (first > 0.1 && second < 0.001, "Repeats fully left with Halo: one echo and its halo, no second echo");
            p.repeats = 0.55f;
            const auto more = runHalo (p, impulse (total, 100, 0.5f), {});
            expect (peakAbs (more.l, 2 * delaySamples - 50, 2 * delaySamples + 400) > 0.02, "Repeats up with Halo: the second echo comes");
        }

        // Mono works
        {
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            p.halo = 0.7f;
            const auto r = runHalo (p, impulse (total, 100, 0.5f), {}, 1);
            bool finite = true;
            for (float v : r.l)
                finite = finite && std::isfinite (v);
            expect (finite && energy (r.l, 600, total) > 1.0e-4, "Halo works on a mono track");
        }

        // Clear empties the halo too
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            p.halo = 1.0f;
            std::vector<float> l = impulse (4096, 10, 0.5f), r = l;
            e.setParams (p);
            float* chans[2] = { l.data(), r.data() };
            e.process (chans, 2, 512);
            p.clear = true;
            e.setParams (p);
            std::vector<float> zl (512, 0.0f), zr (512, 0.0f);
            float* clearChans[2] = { zl.data(), zr.data() };
            e.process (clearChans, 2, 512);
            p.clear = false;
            std::vector<float> sl (4096, 0.0f), sr (4096, 0.0f);
            e.setParams (p);
            for (int done = 0; done < 4096; done += 512)
            {
                float* c2[2] = { sl.data() + done, sr.data() + done };
                e.process (c2, 2, 512);
            }
            expect (peakAbs (sl, 0, 4096) < 1.0e-9 && peakAbs (sr, 0, 4096) < 1.0e-9, "Clear empties the halo's ringing too");
        }

        // Turned off again: the halo goes, with nothing left over
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p = repeatsOnly (0, 0.0f, 0.0f);
            std::mt19937 rng (99);
            std::uniform_real_distribution<float> noise (-0.5f, 0.5f);
            p.halo = 0.8f;
            for (int b = 0; b < 30; ++b)
            {
                std::vector<float> l (512), r (512);
                for (int i = 0; i < 512; ++i) { l[(size_t) i] = noise (rng); r[(size_t) i] = noise (rng); }
                float* chans[2] = { l.data(), r.data() };
                e.setParams (p);
                e.process (chans, 2, 512);
            }
            p.halo = 0.0f;
            std::vector<float> after;
            for (int b = 0; b < 120; ++b)
            {
                std::vector<float> l (512, 0.0f), r (512, 0.0f);
                float* chans[2] = { l.data(), r.data() };
                e.setParams (p);
                e.process (chans, 2, 512);
                after.insert (after.end(), l.begin(), l.end());
            }
            expect (peakAbs (after, 24000, (int) after.size()) < 1.0e-6, "Halo turned off fades away (in about a third of a second) and leaves no ringing behind");
        }

        // The host's tail includes the halo
        {
            mph::Params p;
            p.halo = 0.0f;
            const double without = runHalo (p, std::vector<float> (512, 0.0f), {}).tail;
            p.halo = 1.0f;
            const double with = runHalo (p, std::vector<float> (512, 0.0f), {}).tail;
            expect (with > without + 1.0, "the tail told to the host grows with Halo");
            p.halo = 0.0f;
            expect (std::fabs (runHalo (p, std::vector<float> (512, 0.0f), {}).tail - without) < 1.0e-9, "and is unchanged with Halo off");
        }

        // No click when the knob is swept
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p = repeatsOnly (3, 0.4f, 0.3f);
            const int blocks = 400;
            std::vector<float> out;
            for (int b = 0; b < blocks; ++b)
            {
                std::vector<float> l (512), r (512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) (b * 512 + i) / 48000.0);
                    l[(size_t) i] = r[(size_t) i] = v;
                }
                p.halo = std::min (1.0f, (float) b / 300.0f);
                float* chans[2] = { l.data(), r.data() };
                e.setParams (p);
                e.process (chans, 2, 512);
                out.insert (out.end(), l.begin(), l.end());
            }
            double worst = 0.0;
            for (size_t i = 1; i < out.size(); ++i)
                worst = std::max (worst, (double) std::fabs (out[i] - out[i - 1]));
            expect (worst < 0.35, "sweeping Halo from 0 to 1 makes no click");
        }
    }

    void testHaloStability()
    {
        std::mt19937 rng (31337);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        bool finite = true, bounded = true;
        for (int run = 0; run < 40; ++run)
        {
            const int channels = (run % 5 == 4) ? 1 : 2;
            mph::Engine e;
            e.prepare (48000.0, 512, channels);
            std::vector<float> l (512), r (512);
            for (int block = 0; block < 60; ++block)
            {
                mph::Params p;
                p.halo = (run % 3 == 0) ? 1.0f : unit (rng);
                p.repeats = (run % 2 == 0) ? 1.0f : unit (rng);
                p.color = unit (rng);
                p.mix = unit (rng);
                p.zone = (int) (rng() % 8);
                p.rate = unit (rng);
                p.skew = rng() & 1;
                p.pingPong = rng() & 1;
                p.tempoSync = rng() & 1;
                p.uRate = unit (rng);
                p.uRateLfoDepth = unit (rng);
                p.inputGainStep = 3;
                p.inputLevel = 1.0f;
                p.clear = (rng() % 40) == 0;
                e.setParams (p);
                for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, channels, 512);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                    bounded = bounded && std::fabs (l[i]) < 8.0f && std::fabs (r[i]) < 8.0f;
                }
            }
        }
        expect (finite && bounded, "Halo: 40 random runs (Halo up to fully right, infinite repeats, Zones, Skew, Ping Pong, Clear, mono too) stay finite and bounded");

        // Everything at its worst for a long time: loud noise, Halo fully right, Repeats beyond infinite, short Zone, Color thin, then dark
        for (int colorCase = 0; colorCase < 2; ++colorCase)
        {
            mph::Engine e;
            e.prepare (48000.0, 512, 2);
            mph::Params p;
            p.halo = 1.0f;
            p.repeats = 1.0f;
            p.zone = 1;
            p.rate = 0.3f;
            p.mix = 0.7f;
            p.color = colorCase == 0 ? 1.0f : 0.0f;
            p.inputGainStep = 3;
            p.inputLevel = 1.0f;
            double worst = 0.0;
            bool ok = true;
            std::vector<float> l (512), r (512);
            for (int block = 0; block < 48000 * 20 / 512; ++block)
            {
                for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
                float* chans[2] = { l.data(), r.data() };
                e.setParams (p);
                e.process (chans, 2, 512);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]);
                    worst = std::max (worst, (double) std::max (std::fabs (l[i]), std::fabs (r[i])));
                }
            }
            expect (ok && worst < 4.0, colorCase == 0 ? "Halo fully right with beyond-infinite Repeats for 20 s, thin Color: bounded" : "the same with dark Color: bounded");
        }
    }

    // ======================= milestone 10: Flip =======================

    constexpr double kSr = 48000.0;

    // Runs the engine over a whole signal with the settings (and a host tempo if given), 512-sample blocks; "change" may alter the settings per block.
    HaloRun runFlip (mph::Params p, std::vector<float> inL, std::vector<float> inR, int channels = 2, const mph::core::HostInfo* host = nullptr,
                     const std::function<void (int, mph::Params&)>& change = {})
    {
        mph::Engine e;
        e.prepare (kSr, 512, channels);
        if (host != nullptr)
            e.setHost (*host);
        HaloRun out;
        out.l = std::move (inL);
        out.r = inR.empty() ? out.l : std::move (inR);
        const int total = (int) out.l.size();
        int block = 0;
        for (int done = 0; done < total; done += 512, ++block)
        {
            const int n = std::min (512, total - done);
            if (change)
                change (block, p);
            e.setParams (p);
            float* chans[2] = { out.l.data() + done, out.r.data() + done };
            e.process (chans, channels, n);
        }
        out.tail = e.getTailSeconds();
        return out;
    }

    std::vector<float> rampSignal (int length, float slope)
    {
        std::vector<float> v ((size_t) length);
        for (int i = 0; i < length; ++i)
            v[(size_t) i] = slope * (float) i;
        return v;
    }

    std::vector<float> sineSignal (int length, double hz, float amplitude)
    {
        std::vector<float> v ((size_t) length);
        for (int i = 0; i < length; ++i)
            v[(size_t) i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / kSr);
        return v;
    }

    // A sharp attack at startSeconds, then a 20 ms decay of a 1 kHz tone: played backwards it swells up to a sudden stop.
    std::vector<float> burstSignal (int length, double startSeconds, float amplitude)
    {
        std::vector<float> v ((size_t) length, 0.0f);
        for (int i = (int) (startSeconds * kSr); i < length; ++i)
        {
            const double t = ((double) i - startSeconds * kSr) / kSr;
            v[(size_t) i] = amplitude * (float) (std::exp (-t / 0.02) * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * t));
        }
        return v;
    }

    // A ramp played backwards falls at the same slope; where a chunk joins the next one the slope breaks. The first index of every break after "from".
    std::vector<int> rampBreaks (const std::vector<float>& out, float slope, int from)
    {
        std::vector<int> breaks;
        int last = -100000;
        for (int n = from; n + 1 < (int) out.size(); ++n)
        {
            const double d = (double) out[(size_t) n + 1] - (double) out[(size_t) n];
            if (std::fabs (d + (double) slope) > 0.5 * (double) slope)
            {
                if (n - last > 200)
                    breaks.push_back (n);
                last = n;
            }
        }
        return breaks;
    }

    double medianSpacing (const std::vector<int>& breaks)
    {
        std::vector<double> gaps;
        for (size_t i = 1; i < breaks.size(); ++i)
            gaps.push_back ((double) (breaks[i] - breaks[i - 1]));
        if (gaps.empty())
            return 0.0;
        std::sort (gaps.begin(), gaps.end());
        return gaps[gaps.size() / 2];
    }

    // The share of samples in [from, to) where the output's slope is "wanted" times the ramp's slope.
    double slopeShare (const std::vector<float>& out, float slope, double wanted, int from, int to)
    {
        int good = 0, all = 0;
        for (int n = from; n + 1 < to && n + 1 < (int) out.size(); ++n, ++all)
            if (std::fabs ((double) out[(size_t) n + 1] - (double) out[(size_t) n] - wanted * (double) slope) < 0.05 * (double) slope)
                ++good;
        return all > 0 ? (double) good / (double) all : 0.0;
    }

    double zeroCrossHz (const std::vector<float>& v, int from, int to)
    {
        int crossings = 0;
        for (int i = from + 1; i < to && i < (int) v.size(); ++i)
            if ((v[(size_t) i - 1] < 0.0f) != (v[(size_t) i] < 0.0f))
                ++crossings;
        return 0.5 * (double) crossings / ((double) (to - from) / kSr);
    }

    // The median of |measured pitch / wanted pitch - 1| over 100 ms windows from "from" to "to": robust against the few windows that hold a join.
    double medianPitchError (const std::vector<float>& v, double hz, int from, int to)
    {
        std::vector<double> errors;
        for (int a = from; a + 4800 <= to; a += 4800)
            errors.push_back (std::fabs (zeroCrossHz (v, a, a + 4800) / hz - 1.0));
        if (errors.empty())
            return 1.0;
        std::sort (errors.begin(), errors.end());
        return errors[errors.size() / 2];
    }

    double highShare (const std::vector<float>& v, int from, int to)
    {
        double diff = 0.0, all = 1.0e-30;
        for (int i = from + 1; i < to && i < (int) v.size(); ++i)
        {
            const double d = (double) v[(size_t) i] - (double) v[(size_t) i - 1];
            diff += d * d;
            all += (double) v[(size_t) i] * v[(size_t) i];
        }
        return diff / all;
    }

    int atSeconds (double seconds) { return (int) std::lround (seconds * kSr); }

    // A burst: the energy in the 40 ms before the reversed peak and in the 40 ms after it
    struct Swell { double before, after; };
    Swell swellAround (const std::vector<float>& out, double peakSeconds)
    {
        return { energy (out, atSeconds (peakSeconds - 0.04), atSeconds (peakSeconds)), energy (out, atSeconds (peakSeconds + 0.015), atSeconds (peakSeconds + 0.055)) };
    }

    void testFlipRules()
    {
        using namespace mph::core;
        expect (std::fabs (flipChunkSeconds (4, 0.6531, 0.5f) - 0.6531) < 1.0e-12, "Flip: the chunk is as long as the delay time (Zone 4)");
        expect (std::fabs (flipChunkSeconds (2, 0.2, 1.0f) - 0.2) < 1.0e-12 && std::fabs (flipChunkSeconds (2, 0.2, 0.0f) - 0.2) < 1.0e-12,
                "Flip: uRate does not change the chunk in Zones 1 to 7 (it only bends the pitch)");
        expect (std::fabs (flipChunkSeconds (0, 0.01, 1.0f) - 0.005) < 1.0e-9 && std::fabs (flipChunkSeconds (0, 0.01, 0.0f) - 2.0 * 0.01) < 1.0e-9,
                "Flip: in Zone 0 uRate sets the pitch of the loop, so it sets the chunk (one octave up = half as long, one octave down = twice)");
        expect (flipChunkSeconds (7, 41.796, 0.5f) == kFlipMaxChunkSeconds && flipChunkSeconds (7, 30.0, 0.5f) == kFlipMaxChunkSeconds,
                "Flip: a chunk is held to what the memory can read backwards (the top half of Zone 7)");
        expect (kFlipMaxChunkSeconds > 20.0 && kFlipMaxChunkSeconds < 20.9 && 2.0 * (kFlipMaxChunkSeconds + kFlipFadeMaxSeconds) < mph::kLongestSeconds,
                "Flip: twice the longest chunk and its join fit inside the 41.8 s memory");
    }

    void testFlipSound()
    {
        using namespace mph;
        std::mt19937 rng (91);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        // ---- with Flip off nothing changes: pressed, released, the repeat loop is exactly as if it had never been touched ----
        {
            const int total = (int) (kSr * 5.0);
            std::vector<float> inL ((size_t) total), inR ((size_t) total);
            for (int i = 0; i < total; ++i) { inL[(size_t) i] = noise (rng); inR[(size_t) i] = noise (rng); }
            mph::Params p;
            p.zone = 3;
            p.rate = 0.4f;
            p.repeats = 0.6f;
            p.mix = 0.6f;
            p.color = 0.5f;
            const auto never = runFlip (p, inL, inR);
            const auto pressed = runFlip (p, inL, inR, 2, nullptr, [] (int block, mph::Params& q) { q.flip = block >= 94 && block < 188; });   // about 1 s to 2 s
            const int from = atSeconds (2.5);
            const bool same = std::memcmp (never.l.data() + from, pressed.l.data() + from, (size_t) (total - from) * sizeof (float)) == 0
                              && std::memcmp (never.r.data() + from, pressed.r.data() + from, (size_t) (total - from) * sizeof (float)) == 0;
            const bool sameBefore = std::memcmp (never.l.data(), pressed.l.data(), (size_t) atSeconds (0.9) * sizeof (float)) == 0;
            const bool differsDuring = ! std::memcmp (never.l.data() + atSeconds (1.2), pressed.l.data() + atSeconds (1.2), (size_t) atSeconds (0.5) * sizeof (float));
            expect (sameBefore, "Flip never pressed: the output is bit for bit what it was before milestone 10 (same until the press)");
            expect (! differsDuring, "Flip pressed: the output is different while it is on");
            expect (same, "Flip pressed and released: afterwards the output is bit for bit the same as if Flip had never been touched (the repeat loop runs forwards all along)");
        }

        // ---- the repeat that is heard is read backwards: a ramp falls, a click lands mirrored round the chunk start ----
        {
            const double delay = core::freeDelaySeconds (2, rateForDelay (2, 0.2));
            const float slope = 5.0e-6f;
            mph::Params p = repeatsOnly (2, rateForDelay (2, 0.2));
            p.flip = true;
            const auto flipped = runFlip (p, rampSignal (atSeconds (2.0), slope), {});
            p.flip = false;
            const auto forward = runFlip (p, rampSignal (atSeconds (2.0), slope), {});
            const double fallShare = slopeShare (flipped.l, slope, -1.0, atSeconds (0.5), atSeconds (2.0));
            expect (fallShare > 0.80, "Flip: the repeat heard runs backwards (a rising ramp comes out falling at the same slope in the body of each chunk)");
            expect (slopeShare (forward.l, slope, 1.0, atSeconds (0.5), atSeconds (2.0)) > 0.95 && slopeShare (forward.l, slope, -1.0, atSeconds (0.5), atSeconds (2.0)) < 0.01,
                    "Flip off: the same ramp comes out rising (as before)");
            const double spacing = medianSpacing (rampBreaks (flipped.l, slope, atSeconds (0.25)));
            expect (std::fabs (spacing - delay * kSr) <= 2.0, "Flip: a chunk is as long as the delay time the window shows (200 ms)");
        }
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.25));
            p.flip = true;
            const auto out = runFlip (p, impulse (atSeconds (1.5), atSeconds (0.05), 0.5f), {});
            const int peak = peakIndex (out.l, 0, (int) out.l.size());
            expect (std::abs (peak - atSeconds (0.45)) <= 2 && std::fabs (out.l[(size_t) peak]) > 0.2f,
                    "Flip: a click at 50 ms comes back at 450 ms (mirrored round the start of the second 250 ms chunk)");
        }

        // ---- every repeat is reversed (no alternating), and Repeats fully left gives one reversed echo ----
        {
            const double delay = 0.25;
            mph::Params p = repeatsOnly (3, rateForDelay (3, delay), 0.6f);
            p.flip = true;
            const auto flipped = runFlip (p, burstSignal (atSeconds (2.0), 0.05, 0.5f), {});
            const Swell e1 = swellAround (flipped.l, 0.45), e2 = swellAround (flipped.l, 0.70), e3 = swellAround (flipped.l, 0.95);
            expect (e1.before > 20.0 * e1.after + 1.0e-9 && e2.before > 20.0 * e2.after + 1.0e-9 && e3.before > 20.0 * e3.after + 1.0e-9,
                    "Flip: the first three echoes all swell up and stop short (every repeat is reversed, none plays forwards)");
            expect (e2.before > 0.1 * e1.before && e2.before < 1.0 * e1.before && e3.before > 0.1 * e2.before && e3.before < 1.0 * e2.before,
                    "Flip: the reversed echoes die away by the Repeats amount");
            p.flip = false;
            const auto forward = runFlip (p, burstSignal (atSeconds (2.0), 0.05, 0.5f), {});
            const Swell f1 = swellAround (forward.l, 0.30);      // the forward echo starts at 300 ms
            expect (f1.after > 20.0 * f1.before, "Flip off: the same echo is a sharp start and a decay");
            p.flip = true;
            p.repeats = 0.0f;
            const auto once = runFlip (p, burstSignal (atSeconds (2.0), 0.05, 0.5f), {});
            const Swell o1 = swellAround (once.l, 0.45), o2 = swellAround (once.l, 0.70);
            expect (o1.before > 1.0e-3 && o2.before < 1.0e-6 * o1.before, "Flip with Repeats fully left: one reversed echo and then silence");
        }

        // ---- Zones 0 and 1: total protonic reversal (a flipped chunk of a few milliseconds makes new tones) ----
        {
            auto residualShare = [] (const std::vector<float>& v, double hz, int from, int to)
            {
                double sinSum = 0.0, cosSum = 0.0, all = 1.0e-30;
                const int n = to - from;
                for (int i = from; i < to; ++i)
                {
                    const double ph = 2.0 * 3.14159265358979323846 * hz * (double) i / kSr;
                    sinSum += v[(size_t) i] * std::sin (ph);
                    cosSum += v[(size_t) i] * std::cos (ph);
                    all += (double) v[(size_t) i] * v[(size_t) i];
                }
                const double a = 2.0 * sinSum / n, b = 2.0 * cosSum / n;
                const double fitted = 0.5 * (a * a + b * b) * n;
                return std::max (0.0, 1.0 - fitted / all);
            };
            for (int zone = 0; zone <= 1; ++zone)
            {
                mph::Params p = repeatsOnly (zone, 0.5f);
                p.flip = true;
                const auto flipped = runFlip (p, sineSignal (atSeconds (1.0), 1000.0, 0.3f), {});
                p.flip = false;
                const auto forward = runFlip (p, sineSignal (atSeconds (1.0), 1000.0, 0.3f), {});
                const double newTones = residualShare (flipped.l, 1000.0, atSeconds (0.3), atSeconds (0.9));
                const double none = residualShare (forward.l, 1000.0, atSeconds (0.3), atSeconds (0.9));
                expect (newTones > 0.03 && none < 1.0e-4,
                        zone == 0 ? "Flip in Zone 0: a pure tone gets harmonics and sidebands (total protonic reversal); without Flip it stays pure"
                                  : "Flip in Zone 1: a pure tone gets harmonics and sidebands; without Flip it stays pure");
            }
        }

        // ---- Rate moving gives no doppler with Flip (and does without it) ----
        {
            const int total = atSeconds (5.0);
            auto sweep = [] (int block, mph::Params& q)
            {
                const double t = (double) block * 512.0 / kSr;
                const double x = std::min (1.0, std::max (0.0, (t - 1.5) / 2.0));
                q.rate = rateForDelay (4, 0.5 * std::pow (0.7 / 0.5, x));      // 500 ms to 700 ms over two seconds
            };
            mph::Params p = repeatsOnly (4, rateForDelay (4, 0.5));
            p.flip = true;
            const auto flipped = runFlip (p, sineSignal (total, 1000.0, 0.3f), {}, 2, nullptr, sweep);
            p.flip = false;
            const auto forward = runFlip (p, sineSignal (total, 1000.0, 0.3f), {}, 2, nullptr, sweep);
            const double flipError = medianPitchError (flipped.l, 1000.0, atSeconds (1.7), atSeconds (3.5));
            const double forwardError = medianPitchError (forward.l, 1000.0, atSeconds (1.7), atSeconds (3.5));
            expect (flipError < 0.01, "Flip: Rate moving from 500 to 700 ms leaves the pitch alone (no doppler)");
            expect (forwardError > 0.05, "Flip off: the same move bends the pitch by about 10 % (the doppler that Flip leaves out)");
        }

        // ---- uRate and its LFO still bend the reversed sound ----
        {
            mph::Params p = repeatsOnly (5, rateForDelay (5, 1.5));
            p.flip = true;
            p.uRateLfoDepth = 0.0f;
            const auto still = runFlip (p, sineSignal (atSeconds (6.0), 1000.0, 0.3f), {});
            p.uRateLfoDepth = 1.0f;
            p.uRateLfoRate = 0.5f;
            const auto wobble = runFlip (p, sineSignal (atSeconds (6.0), 1000.0, 0.3f), {});
            const double quiet = medianPitchError (still.l, 1000.0, atSeconds (1.7), atSeconds (5.5));
            const double bent = medianPitchError (wobble.l, 1000.0, atSeconds (1.7), atSeconds (5.5));
            expect (quiet < 0.01 && bent > 0.03, "Flip: the uRate LFO still bends the pitch of the reversed sound (about 7 % at the median at full Depth), and without it the pitch is exact");
        }
        {
            // clockwise and positive LFO = pitch up, as for the forward repeats: the right side (Skew off) is not inverted
            mph::Params p = repeatsOnly (2, rateForDelay (2, 0.2));
            p.flip = true;
            p.uRate = 1.0f;
            const double delay = core::freeDelaySeconds (2, p.rate);
            const auto out = runFlip (p, rampSignal (atSeconds (2.0), 5.0e-6f), {});
            const double spacing = medianSpacing (rampBreaks (out.l, 5.0e-6f, atSeconds (0.25)));
            expect (std::fabs (spacing - delay * kSr) <= 2.0, "Flip with uRate fully clockwise: the chunk is still as long as the delay time (uRate only moves the reading point)");
        }

        // ---- Skew: each side has its own chunk length ----
        {
            const int total = atSeconds (8.0);
            const float slope = 1.5e-6f;
            mph::Params p = repeatsOnly (4, 0.5f);
            p.flip = true;
            p.skew = true;
            mph::Engine e;
            e.prepare (kSr, 512, 2);
            std::vector<float> l = rampSignal (total, slope), r = l;
            for (int done = 0, block = 0; done < total; done += 512, ++block)
            {
                p.rate = block == 0 ? 0.5f : 0.9f;
                e.setParams (p);
                float* chans[2] = { l.data() + done, r.data() + done };
                e.process (chans, 2, std::min (512, total - done));
            }
            const auto sides = e.getSkewState().ratesNow (0.9f);
            const double wantLeft = core::freeDelaySeconds (4, sides.left) * kSr, wantRight = core::freeDelaySeconds (4, sides.right) * kSr;
            const double haveLeft = medianSpacing (rampBreaks (l, slope, atSeconds (3.5))), haveRight = medianSpacing (rampBreaks (r, slope, atSeconds (3.5)));
            expect (std::fabs (haveLeft - wantLeft) <= 3.0 && std::fabs (haveRight - wantRight) <= 3.0 && wantRight > 2.5 * wantLeft,
                    "Flip with Skew: the left and right chunks are as long as the two delay times the window shows (375 ms and 1.14 s)");
        }

        // ---- Ping Pong: each hop is half the delay time, the repeats still bounce ----
        {
            const double hop = 0.5 * core::freeDelaySeconds (4, 0.5f) * kSr;
            const float slope = 5.0e-6f;
            mph::Params p = repeatsOnly (4, 0.5f);
            p.flip = true;
            p.pingPong = true;
            const auto out = runFlip (p, rampSignal (atSeconds (3.0), slope), {});
            expect (std::fabs (medianSpacing (rampBreaks (out.l, slope, atSeconds (0.5))) - hop) <= 3.0, "Flip with Ping Pong: the chunk is half the delay time (the hop the window shows)");

            p.repeats = 0.6f;
            std::vector<float> burstL = burstSignal (atSeconds (4.0), 0.1, 0.5f), burstR = burstL;
            const auto bounce = runFlip (p, burstL, burstR);
            expect (energy (bounce.r, atSeconds (0.5), atSeconds (4.0)) > 1.0e-3 && energy (bounce.l, atSeconds (0.5), atSeconds (4.0)) > 1.0e-3,
                    "Flip with Ping Pong: the reversed repeats still bounce to the right side");
        }

        // ---- Tempo Sync: the chunk is the clocked time ----
        {
            mph::core::HostInfo host;
            host.tempoValid = true;
            host.bpm = 120.0;
            const float slope = 5.0e-6f;
            for (int zone = 3; zone <= 4; ++zone)
            {
                mph::Params p = repeatsOnly (zone, 0.5f);
                p.flip = true;
                p.tempoSync = true;
                const auto out = runFlip (p, rampSignal (atSeconds (3.0), slope), {}, 2, &host);
                const double want = (zone == 4 ? 0.5 : 0.25) * kSr;
                expect (std::fabs (medianSpacing (rampBreaks (out.l, slope, atSeconds (0.6))) - want) <= 3.0,
                        zone == 4 ? "Flip with Tempo Sync: 1/1 at 120 BPM in Zone 4 reverses in 500 ms chunks" : "Flip with Tempo Sync: 1/1 at 120 BPM in Zone 3 reverses in 250 ms chunks");
            }
        }

        // ---- Clear, Color and Halo with Flip ----
        {
            const int total = atSeconds (3.0);
            std::vector<float> inL ((size_t) total, 0.0f), inR ((size_t) total, 0.0f);
            for (int i = 0; i < atSeconds (1.0); ++i) { inL[(size_t) i] = noise (rng); inR[(size_t) i] = noise (rng); }
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.3), 0.7f);
            p.flip = true;
            const auto out = runFlip (p, inL, inR, 2, nullptr, [] (int block, mph::Params& q) { q.clear = block == 120; });
            expect (energy (out.l, atSeconds (0.9), atSeconds (1.2)) > 1.0e-3, "Flip: there is sound to clear");
            expect (energy (out.l, 120 * 512 + 512, total) < 1.0e-12 && energy (out.r, 120 * 512 + 512, total) < 1.0e-12, "Flip with Clear: everything stops at once (the reversed reader finds only silence)");
        }
        {
            const int total = atSeconds (4.0);
            std::vector<float> inL ((size_t) total), inR ((size_t) total);
            for (int i = 0; i < total; ++i) { inL[(size_t) i] = noise (rng); inR[(size_t) i] = noise (rng); }
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.3), 0.7f);
            p.flip = true;
            p.color = 0.8f;
            const auto clean = runFlip (p, inL, inR);
            p.color = 0.1f;
            const auto dark = runFlip (p, inL, inR);
            expect (highShare (dark.l, atSeconds (1.5), total) < 0.5 * highShare (clean.l, atSeconds (1.5), total),
                    "Flip with a dark Color: the reversed repeats are darker (Color is written into the repeats, so the reversed sound has it)");
            // a low tone: a thin Color takes it away from every echo
            p.color = 0.8f;
            const auto lowClean = runFlip (p, sineSignal (total, 200.0, 0.3f), {});
            p.color = 1.0f;
            const auto lowThin = runFlip (p, sineSignal (total, 200.0, 0.3f), {});
            expect (energy (lowThin.l, atSeconds (1.5), total) < 0.1 * energy (lowClean.l, atSeconds (1.5), total) && energy (lowThin.l, atSeconds (1.5), total) > 1.0e-3,
                    "Flip with a thin Color: the reversed repeats lose their lows and still sound");
        }
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.25));
            p.flip = true;
            const auto dry = runFlip (p, burstSignal (atSeconds (2.0), 0.05, 0.5f), {});
            p.halo = 0.6f;
            const auto verb = runFlip (p, burstSignal (atSeconds (2.0), 0.05, 0.5f), {});
            const double after = energy (verb.l, atSeconds (0.47), atSeconds (0.62)), none = energy (dry.l, atSeconds (0.47), atSeconds (0.62));
            expect (none < 1.0e-9 && after > 1.0e-4 && energy (verb.r, atSeconds (0.47), atSeconds (0.62)) > 1.0e-5,
                    "Flip with Halo (Repeats fully left): the reversed echo swells into the reverb and the halo rings on after it stops, on both sides");
        }

        // ---- mono, host tail ----
        {
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.25));
            p.flip = true;
            const auto mono = runFlip (p, burstSignal (atSeconds (2.0), 0.05, 0.5f), {}, 1);
            const Swell m = swellAround (mono.l, 0.45);
            expect (m.before > 20.0 * m.after + 1.0e-9 && m.before > 1.0e-3, "Flip on a mono track: the echo is reversed too");
        }
        {
            mph::Engine on, off;
            on.prepare (kSr, 512, 2);
            off.prepare (kSr, 512, 2);
            mph::Params p = repeatsOnly (4, 0.5f, 0.6f);
            p.flip = true;
            on.setParams (p);
            p.flip = false;
            off.setParams (p);
            const double chunk = core::freeDelaySeconds (4, 0.5f);
            expect (std::fabs ((on.getTailSeconds() - off.getTailSeconds()) - chunk) < 1.0e-3, "Flip: Logic is told the tail is one chunk longer (the last echo is heard backwards after it)");
        }

        // ---- a click-free run: pressing and releasing Flip, chunk joins, and Zones changing while flipped ----
        {
            auto biggestStep = [] (const std::vector<float>& v, int from)
            {
                double m = 0.0;
                for (int i = from + 1; i < (int) v.size(); ++i)
                    m = std::max (m, (double) std::fabs (v[(size_t) i] - v[(size_t) i - 1]));
                return m;
            };
            mph::Params p = repeatsOnly (3, rateForDelay (3, 0.35), 0.3f);
            p.mix = 0.7f;
            const auto presses = runFlip (p, sineSignal (atSeconds (5.0), 440.0, 0.3f), {}, 2, nullptr,
                                          [] (int block, mph::Params& q) { q.flip = (block / 94) % 2 == 1; });         // on and off about every second
            expect (biggestStep (presses.l, atSeconds (0.5)) < 0.06, "Flip pressed and released again and again under a tone: no click");
            p.mix = 1.0f;
            p.flip = true;
            const auto joins = runFlip (p, sineSignal (atSeconds (6.0), 440.0, 0.3f), {});
            expect (biggestStep (joins.l, atSeconds (0.5)) < 0.06, "Flip: the joins between chunks do not click (Zone 3)");
            const auto zones = runFlip (p, sineSignal (atSeconds (6.0), 440.0, 0.3f), {}, 2, nullptr,
                                        [] (int block, mph::Params& q) { static const int order[5] = { 3, 2, 4, 1, 5 }; q.zone = order[(block / 94) % 5]; });
            expect (biggestStep (zones.l, atSeconds (0.5)) < 0.06, "Flip: Zone changes while it is on do not click (a new chunk with a crossfade)");
            std::mt19937 zoneRng (5);
            const auto fast = runFlip (p, sineSignal (atSeconds (3.0), 440.0, 0.3f), {}, 2, nullptr,
                                       [&zoneRng] (int, mph::Params& q) { q.zone = (int) (zoneRng() % 8); });                // a new Zone every 10 ms
            bool finite = true;
            for (float v : fast.l)
                finite = finite && std::isfinite (v) && std::fabs (v) < 2.0f;
            expect (finite, "Flip: a new Zone every block stays finite and bounded");
        }

        // ---- the top of Zone 7: the chunk is held to what the memory can read backwards ----
        {
            mph::Params p = repeatsOnly (7, 1.0f);
            p.flip = true;
            const int total = atSeconds (44.0);
            const auto out = runFlip (p, impulse (total, atSeconds (1.0), 0.5f), {});
            const int want = 2 * (int) std::ceil (core::kFlipMaxChunkSeconds * kSr) - atSeconds (1.0);
            const int peak = peakIndex (out.l, atSeconds (10.0), total);
            expect (std::abs (peak - want) <= 3 && std::fabs (out.l[(size_t) peak]) > 0.2f,
                    "Flip in Zone 7 at 41.8 s: the chunk is held to about 20.8 s (a click at 1 s is heard at about 40.7 s)");
        }
    }

    void testFlipStability()
    {
        std::mt19937 rng (123);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f), unit (0.0f, 1.0f);
        bool finite = true, bounded = true;
        for (int run = 0; run < 40; ++run)
        {
            const int channels = (run % 5 == 4) ? 1 : 2;
            mph::Engine e;
            e.prepare (kSr, 512, channels);
            std::vector<float> l (512), r (512);
            for (int block = 0; block < 120; ++block)
            {
                mph::Params p;
                p.flip = (rng() % 3) != 0;
                p.halo = unit (rng);
                p.repeats = (run % 2 == 0) ? 1.0f : unit (rng);
                p.color = unit (rng);
                p.mix = unit (rng);
                p.zone = (int) (rng() % 8);
                p.rate = unit (rng);
                p.skew = rng() & 1;
                p.pingPong = rng() & 1;
                p.tempoSync = rng() & 1;
                p.uRate = unit (rng);
                p.uRateLfoDepth = unit (rng);
                p.uRateLfoRate = unit (rng);
                p.clockDivision = (int) (rng() % 10);
                p.inputGainStep = 3;
                p.inputLevel = 1.0f;
                p.clear = (rng() % 40) == 0;
                mph::core::HostInfo host;
                host.tempoValid = (rng() & 1) != 0;
                host.bpm = 40.0 + 200.0 * (double) unit (rng);
                e.setHost (host);
                e.setParams (p);
                for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
                float* chans[2] = { l.data(), r.data() };
                e.process (chans, channels, 512);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                    bounded = bounded && std::fabs (l[i]) < 8.0f && std::fabs (r[i]) < 8.0f;
                }
            }
        }
        expect (finite && bounded, "Flip: 40 random runs (Flip on and off, every Zone, Skew, Ping Pong, Tempo Sync, uRate and its LFO, Halo, Clear, mono too) stay finite and bounded");

        // The worst case for the reverse reader: the longest chunk, beyond-infinite Repeats, Halo fully right, uRate and its LFO at the far ends, for 50 seconds
        {
            mph::Engine e;
            e.prepare (kSr, 512, 2);
            mph::Params p;
            p.flip = true;
            p.zone = 7;
            p.rate = 1.0f;
            p.repeats = 1.0f;
            p.halo = 1.0f;
            p.uRate = 1.0f;
            p.uRateLfoDepth = 1.0f;
            p.inputGainStep = 3;
            p.inputLevel = 1.0f;
            double worst = 0.0;
            bool ok = true;
            std::vector<float> l (512), r (512);
            for (int block = 0; block < (int) (kSr * 50.0) / 512; ++block)
            {
                for (size_t i = 0; i < l.size(); ++i) { l[i] = noise (rng); r[i] = noise (rng); }
                float* chans[2] = { l.data(), r.data() };
                e.setParams (p);
                e.process (chans, 2, 512);
                for (size_t i = 0; i < l.size(); ++i)
                {
                    ok = ok && std::isfinite (l[i]) && std::isfinite (r[i]);
                    worst = std::max (worst, (double) std::max (std::fabs (l[i]), std::fabs (r[i])));
                }
            }
            expect (ok && worst < 4.0, "Flip in Zone 7 at 41.8 s with beyond-infinite Repeats, Halo fully right, uRate and its LFO at the far ends, 50 s of loud noise: bounded");
        }
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
    testColor();
    testSkewRules();
    testSkewSound();
    testPingPong();
    testSkewStability();
    testURateRules();
    testURateSound();
    testURateStability();
    testTempoSyncRules();
    testTempoSyncSound();
    testTempoSyncStability();
    testHaloRules();
    testHaloSound();
    testHaloStability();
    testFlipRules();
    testFlipSound();
    testFlipStability();

    std::printf ("engine tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
