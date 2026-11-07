// Flip: every repeat is heard backwards. Plain C++17, no JUCE.
//
// What the module does (manual p. 15, and what Bryn heard on his own module):
//   * Flip on: the repeats are reversed. They only alternate forward / backward if a CV is patched to the Flip input; with the button
//     alone every repeat is reversed. In the medium and large Zones the sound plays backwards; in Zones 0 and 1 the manual calls it
//     "total protonic reversal": a buffer flipped every ~70 samples, which makes harmonics, subharmonics and other over- and undertones.
//   * Rate modulation with Flip on does not give the usual doppler shift ("a minor limitation in the Mimeophon's time travel").
//   * Color, Halo, Repeats, Skew, Ping Pong, Tempo Sync, uRate and Clear keep working.
//
// How it is built here: the repeat loop itself (the delay buffer, Repeats, Color written into it) is not touched, so it keeps going
// forwards. Flip changes what is HEARD: a second reader plays the same shared buffer backwards, in chunks as long as the delay time
// (the time the Zone and Rate give, the Left / Right figure in the window):
//
//   a chunk that starts at time T0 plays the sound from the T0 - chunk ... T0 stretch backwards, so the sound that came in at T0 - t
//   is heard at T0 + t. Its delay therefore grows from nothing to twice the chunk, two samples per sample, and the audio runs at
//   exactly -1 times the speed. The average delay is the delay time, and a clap comes back as a reversed clap, once per chunk.
//
// Because the feedback keeps going forwards, the buffer holds the sound and its echoes the right way round, and the reversed reader turns
// all of them round: every repeat is reversed (no alternating). With Repeats fully left there is one reversed echo.
//
//   * No doppler from Rate: the length of a chunk is taken when the chunk starts and kept until it ends, so Rate moving does not change
//     the playback speed. A new Zone, a Tempo Sync step, a Skew or Ping Pong jump, or Rate moving a long way inside a long chunk (more than a
//     quarter) starts a new chunk at once (a short crossfade, no bend). uRate and its LFO are still doppler: they move the reading point
//     (Zones 1 to 7) so the reversed sound bends, clockwise = higher pitch as before; in Zone 0 they set the chunk length (the pitch of the
//     loop), as they do for the forward repeats.
//   * Chunks join with a short raised-cosine crossfade, a quarter of the chunk (never more than 30 ms, never less than 4 samples). In Zones 0
//     and 1 that is a few samples: the joins are sharp, which is the protonic reversal's buzz of harmonics.
//   * The reader needs twice the chunk length of memory, so a chunk is held to about 20.8 s (only the top half of Zone 7 is affected; the
//     window shows the chunk that is heard).
//   * Pressing Flip fades the reversed sound in over about 10 ms; releasing fades it back. Everything about the forward loop is the same with
//     Flip on or off, so with Flip off nothing changes, bit for bit.
//   * Halo works on the reversed sound that is heard (so a reversed clap swells into the reverb); the part of Halo that is injected into the
//     feedback comes from that too, so at the highest settings a little of the reverb's wash travels the loop.
// All numbers are first guesses, to be tuned by ear without touching a control id.

#pragma once

#include <algorithm>
#include <cmath>

#include "../Params.h"
#include "DelayBuffer.h"
#include "URate.h"

namespace mph::core
{
    inline constexpr double kFlipFadeFraction = 0.25;        // a chunk's join is this part of the chunk ...
    inline constexpr double kFlipFadeMaxSeconds = 0.030;     // ... but never more than this ...
    inline constexpr double kFlipFadeMinSamples = 4.0;       // ... and never less than this
    inline constexpr double kFlipMinChunkSamples = 16.0;
    // Reading backwards needs twice the chunk (plus the join and a little for uRate) inside the shared memory.
    inline constexpr double kFlipMaxChunkSeconds = kLongestSeconds * 0.5 - kFlipFadeMaxSeconds - 0.02;
    inline constexpr double kFlipRestartMinSeconds = 0.05;   // only chunks longer than this are restarted when Rate moves far
    inline constexpr double kFlipRestartLog = 0.22;          // ... by more than this (log, about 25 %)

    // The length of the chunk that is heard, in seconds, from the delay time the Zone and Rate give (with Ping Pong's hop in it).
    // Zones 1-7: uRate does not change it (it only bends the pitch). Zone 0: uRate sets the pitch of the loop, so it sets the chunk too.
    inline double flipChunkSeconds (int zone, double delaySeconds, float uRateKnob)
    {
        double s = delaySeconds;
        if (zone == 0)
            s = uRateStaticDelay (0, s, uRateKnob, 0, false);
        return std::min (kFlipMaxChunkSeconds, std::max (0.0, s));
    }

    // One side's reverse reader: up to two chunks alive at once (the one ending and the one starting).
    class FlipVoice
    {
    public:
        // The sample rate sets the longest join (30 ms).
        void prepare (double sampleRate)
        {
            fadeCap = std::max (kFlipFadeMinSamples, kFlipFadeMaxSeconds * sampleRate);
            reset();
        }

        void reset()
        {
            head[0] = Head();
            head[1] = Head();
            newest = 0;
            pending = false;
        }

        bool running() const { return head[0].active || head[1].active; }

        // Start a new chunk as soon as the older one has finished its join (a Zone change, a Tempo Sync step, a Skew or Ping Pong jump).
        void restart()
        {
            if (running())
                pending = true;
        }

        // Called once per block with the length the chunk would have if it started now (samples): a long chunk is restarted when Rate has
        // moved far from the length it was started with (short chunks are replaced soon enough on their own).
        void checkLength (double wantedSamples, double sampleRate)
        {
            const Head& n = head[newest];
            if (! n.active || n.ending || pending || wantedSamples <= 0.0)
                return;
            if (n.length < kFlipRestartMinSeconds * sampleRate || n.age < n.fade)
                return;
            if (std::fabs (std::log (wantedSamples / n.length)) > kFlipRestartLog)
                pending = true;
        }

        // One sample: the sound read backwards. Call before the sample is written to the buffer. offsetSamples is uRate's steady offset and LFO
        // swing (it moves the reading point, so the sound bends). wantedChunk() gives the chunk length in samples when a chunk starts.
        template <class Length>
        float process (const dsp::DelayBuffer& buffer, double offsetSamples, double minDelay, double maxDelay, Length&& wantedChunk)
        {
            Head& n = head[newest];
            if (! n.active)
            {
                start (n, 0.0, wantedChunk());
                pending = false;
            }
            else
            {
                Head& o = head[1 - newest];
                const bool boundary = ! n.ending && n.age >= n.length;
                if ((boundary || pending) && ! o.active)
                {
                    const double length = wantedChunk();
                    const double carry = boundary ? std::min (n.age - n.length, 0.5 * length) : 0.0;
                    n.ending = true;
                    n.endAge = n.age;
                    start (o, carry, length);
                    newest = 1 - newest;
                    pending = false;
                }
            }

            float y = 0.0f;
            for (Head& h : head)
            {
                if (! h.active)
                    continue;
                // Two samples of delay per sample of time = the memory is read backwards at the normal speed.
                const double d = std::min (maxDelay, std::max (minDelay, 2.0 * h.age + offsetSamples));
                y += gain (h) * buffer.read (d);
                h.age += 1.0;
                if (h.ending && h.age - h.endAge >= h.fade)
                    h.active = false;
            }
            return y;
        }

    private:
        struct Head
        {
            bool active = false;
            bool ending = false;       // the next chunk has started: this one is fading out
            double age = 0.0;          // samples since it started
            double length = 0.0;       // samples: when the next chunk starts
            double endAge = 0.0;       // the age at which it began to fade out
            double fade = 4.0;         // samples: the join
        };

        static constexpr double kPi = 3.14159265358979323846;

        void start (Head& h, double carry, double length) const
        {
            h.active = true;
            h.ending = false;
            h.age = carry;
            h.length = std::max (kFlipMinChunkSamples, length);
            h.endAge = 0.0;
            h.fade = std::max (kFlipFadeMinSamples, std::min (kFlipFadeFraction * h.length, fadeCap));
        }

        static float gain (const Head& h)
        {
            double g = 1.0;
            if (h.age < h.fade)
                g = 0.5 - 0.5 * std::cos (kPi * h.age / h.fade);
            if (h.ending)
            {
                const double t = (h.age - h.endAge) / h.fade;
                g *= t >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos (kPi * std::max (0.0, t));
            }
            return (float) g;
        }

        Head head[2];
        int newest = 0;
        bool pending = false;
        double fadeCap = kFlipFadeMaxSeconds * 48000.0;
    };
}
