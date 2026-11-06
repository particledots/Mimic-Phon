# Mimic-Phon: version history

The detailed notes for each milestone, newest first. A version is listed as tested only after it has been tested in Logic on both
test Macs (the M4 Mac mini and the Intel Mac) and worked as described. The README describes the current version.

Version 0.5.0 (milestone 5, Color; **not yet tested in Logic**). Color makes sound.

- **Color** (`core/Color.h`): a saturation and filter character written into the repeater along with the sound (the live signal plus the feedback), so every echo is shaped once more than the one before: a dark setting gets darker and darker along the echoes, a thin one thinner, and what is turned is baked into what comes back later. Left of unity (0.00 to 0.78 on the knob): a saturation that grows towards the left (tanh, small signals unchanged, up to a drive of 2.2) then a 2-pole Butterworth low-pass that closes from 17 kHz to 700 Hz at the far left; the words go tape, crunch BBD, warm BBD, oil can. **Unity at 0.80 (3:00): within 0.02 each side nothing at all is done and the audio is bit for bit what it was without Color.** Right of unity (0.82 to 1.00): a little saturation (drive up to 1.2) then a 2-pole Butterworth high-pass from 20 Hz to 1.5 kHz at the far right; the words go crisp, thin. The effect fades in over the 0.05 beyond the unity stretch. Butterworth filters never boost, so Color cannot add gain to the loop; the leveller and soft limiter stay as safeties.
- **Smooth:** the Color knob glides over about 30 ms and the filter coefficients follow every 16 samples (counted across blocks, so the block size makes no difference to the sound); nothing is processed or remembered while Color is on unity (no extra processor cost). Clear empties the filter's memory too. Filters run in double precision with tiny numbers flushed.
- **Mix fully left is never coloured** (the live signal does not go through Color). Mix fully right with Repeats fully left gives one coloured echo: a simple tone filter, as the Filter-Verb patch of the manual expects.
- **Window:** the Color card shows the word from the engine's own function (oil can, warm BBD, crunch BBD, tape, clean, crisp, thin), and its teal mark now sits exactly on the engine's unity position (0.80; it was at 0.83 in the first guess). The plain window's note says Color works now. Version in the footer.
- **Milestone 4 finished:** 0.4.1 (GitHub run #11, commit 89adb1f, every step green) was tested in Logic on both Macs on 6 November 2026 and everything is fixed.
- Engine self-test: 281 checks (35 new: the knob's law and words, unity bit for bit through echoes, dark and thin by measurement, each echo shaped once more, extremes at infinite and beyond for 20 s, no click when Color jumps, block sizes, 44.1 to 192 kHz, Clear). Also run under a memory-error and undefined-behaviour checker. Processor cost on the Linux test computer: about 0.4 % of one core with Color at unity, about 0.7 % turned either way (60 s stereo at 48 kHz).
- No control id changed.
- **Not tested in Logic yet.**

Version 0.4.1 (milestone 4, the core repeater; **the fixes for what 0.4.0 showed in Logic; tested in Logic on both Macs on 6 November 2026, everything fixed: milestone 4 finished**).

- **0.4.0 tested in Logic (6 November 2026) on both Macs:** one echo, Zone steps (no clicks heard), Clear and the tail all worked. Two things to fix: beyond infinite the repeats got louder and louder until they fed back harshly (worst in Zone 4 and shorter, still there with long echoes); the Rate glide was a little too responsive.
- **Beyond infinite is now tamed:** the growth beyond 100 % is counted per second rather than per echo for delays under half a second (a 20 ms delay no longer grows 25 times faster than a 500 ms one), and a leveller in the feedback path (instant attack, 250 ms release, ceiling 0.6) holds the loop at a steady, clean level when it grows, instead of the soft limiter clipping it harder and harder. The soft limiter is kept only as a last safety. Side effect: loud material at exactly infinite now settles around 0.4 instead of just above 0.8.
- **Rate glide slowed from about 80 ms to about 160 ms** (Bryn: better).
- **0.4.1 first build, tested in Logic:** the glide and the runaway were better, but a metronome-like tick at the start of each echo grew as the echoes multiplied. Cause: the leveller took 2 ms to pull the gain down, so the sharp start of every echo slipped past it and was clipped by the soft limiter. Fixed in the next build (run #11, commit 89adb1f, every step green): the leveller now pulls down instantly (only its letting-go is slow), so nothing slips past. **That build was tested on both Macs and everything is fixed.**
- Engine self-test: 246 checks (two new ones for beyond infinite at 20 ms: slow growth, then a steady level, no clipping).
- No control id changed.

Version 0.4.0 (milestone 4, the core repeater; **not yet tested in Logic**). The plugin makes sound for the first time.

- **What works now:** Input Level and the Input Gain step, Zone, Rate, Repeats, Mix, Output Level and Clear. Color, Halo, Skew, Ping Pong / Swap, uRate and its LFO, Tempo Sync, Flip and Hold are read but do nothing (milestones 5 to 11). Left and right are independent until Skew and Ping Pong / Swap.
- **One shared memory for all eight Zones** (`core/DelayBuffer.h`): a stereo buffer of 41.8 seconds plus a little (about 16 MB at 48 kHz, 64 MB at 192 kHz), read with cubic interpolation. A Zone is only a different delay time to read at, so switching to a longer Zone brings back older sound. The delay time is `core::freeDelaySeconds(zone, rate)` from `ZoneMap.h`, the same function the window uses.
- **Rate glides, Zone jumps:** Rate moves the delay time through a one-pole glide of about 80 ms (160 ms from 0.4.1) in the logarithm of the time, so the repeats bend in pitch (doppler) while it moves. A Zone change aims a second read point at the new time and crossfades to it over 20 ms: no pitch effect. A Zone change during a crossfade re-aims the incoming read point (the fade continues).
- **Repeats** (`core/Repeats.h`): the feedback is 0 at the left end (one echo), rises along t to the power 1.5 to exactly 1.0 at knob position 0.80, stays at exactly 1.0 ("infinite") until 0.86, then rises to 1.12 at the right end ("beyond"). The feedback goes through a 5 Hz DC blocker and the soft limiter (knee at 0.8, from `Gain.h`) before it is written back, so it saturates and never runs away. The limiter is on the feedback only: the input is written at full level, so one echo of a hot signal is not squashed.
- **Mix** is an equal-power balance (cosine / sine) between the live signal and the repeats; **Input Level** with the gain step and **Output Level** use the level-knob curve from `Gain.h`. Levels, Mix and Repeats glide over about 10 ms (no zipper noise) and land exactly on their setting, so Mix fully left with levels at 0 dB is bit for bit the live signal. (The glides run in double precision: a first version in single precision stalled a hair short of its target, which the checks caught.)
- **Clear** marks everything written so far as silent (all Zones at once, no pause in the audio, nothing erased) and empties the feedback path's memory; it acts once, when the button goes on.
- **Tail:** `getTailLengthSeconds()` tells Logic how long the repeats keep sounding with the current settings: the delay time times the number of echoes to fall 60 dB (plus the first), one delay time for one echo, capped at 120 s (infinite and beyond give the cap).
- **Bad samples** (not-a-number, infinity) at the input are replaced by silence so they can never get into the feedback.
- **Tempo Sync** is still not audible: `mph::kTempoSyncAudible` is false, so the sound and the window both use the free-running time whatever the Tempo Sync button says (milestone 8 switches it on). The TICK light still follows Logic's tempo; the RATE light counts at the delay time actually used.
- **Window:** Repeats reads "one echo", "x 0.35", "infinite" or "inf +12 %" from the engine's own law; the delay and Rate read-outs follow the engine; a `getReadoutText()` on the knob cards lets the checks read what a card shows. The plain window's note says what works and what is still silent. Version in the footer.
- **Checked before hand-over (Linux test computer):** 244 engine checks (85 new, and the old "audio passes through unchanged" checks replaced by the Mix-fully-left version) and 225 plugin-side checks (28 new), all passing, the engine tests also under a memory-error and undefined-behaviour checker; pictures drawn and looked at. **GitHub's Mac build (run #8, commit b0b0f3f, both jobs green):** 244 engine checks, `auval` AU VALIDATION SUCCEEDED, `pluginval` 1.0.4 strictness 5 passed for AU and VST3, archs `x86_64 arm64`, private draft release `test-build-8`. Processor cost on the Linux test computer: about 0.4 % of one core for stereo at 48 kHz and 0.8 % at 96 kHz (a 60-second run with Zone changes).
- **Not tested in Logic yet.**

Version 0.3.1 (milestone 3, small fixes after Bryn's first Logic test; **tested in Logic on both Macs on 6 November 2026 and working as described: "all good, much better"**). Bryn tested 0.3.0 on both Macs: everything worked as described, with two things to fix.

- **Clear now shows itself:** Clear switches itself off the instant the engine has handled it, so the button was never seen lit. In the styled panel it now stays lit for about a third of a second after each click. (The control, its id and its behaviour are unchanged.)
- **Easier to read:** the small grey texts (notes under the knobs, the DARK / unity / THIN marks, the Zone staircase ranges, the DELAY / TICK / RATE labels, the tempo line, the LFO RATE / DEPTH labels, the footer) are larger, and the grey itself is lighter. The note under Repeats is shortened to "ONE ECHO ... INFINITE+" so it fits at the bigger size.
- Checks on the Linux test computer: engine 159, window 197, all passing. **GitHub's Mac build (run #6, commit 8441fb1, both jobs green):** 159 engine checks, `auval` AU VALIDATION SUCCEEDED, `pluginval` strictness 5 passed for AU and VST3, archs `x86_64 arm64`.

Version 0.3.0 (milestone 3, the styled panel; **tested in Logic on both Macs on 6 November 2026: worked as described apart from the two things fixed in 0.3.1**). The window from the approved picture. Still no repeater.

- **Styled panel** (`plugin/Source/StyledPanel.*`), after `MimicPhon_panel_mockup_1.png`: title and header, the Zone band (big coloured Zone window,
  the staircase of the eight Zones with their ranges, the delay / tempo box with TICK and RATE lights), five knob cards (Input Level with
  the -3 / 0 / +3 / +6 dB chips, Mix, Repeats, Halo, Output Level), five more (Zone with eight steps and the Zone's colour on its arc,
  Color with the teal unity mark, Rate with the teal 1/1 mark, uRate, and the uRate LFO card with Rate, Depth, Sine / Triangle and Sync),
  the button row (Hold, Flip, Skew, Ping Pong / Swap, Clear, Tempo Sync in teal) and the Clock Division menu. Every control is attached
  to the same frozen id as in the plain window; **no id changed**.
- **Switching:** PLAIN SLIDERS (styled panel, top right) and Styled panel (plain window, top right). The choice is stored in the saved project as a
  property of the state (`editorView`), not as a control, so automation is unaffected; a project saved with 0.2.0 opens styled.
- **Zone window:** the eight colours (0 green, 1 lime, 2 yellow, 3 orange, 4 red, 5 magenta, 6 lilac, 7 blue); flashes half a second dim / half a second
  bright while Ping Pong / Swap is on. The Zone knob shows a coloured dot at Zone 0 so its colour is not lost.
- **Read-outs are first guesses** until each control has its sound (dB, percentages, the Color words, uRate in semitones, LFO Rate in Hertz); the delay time
  comes from the engine's new map. Skew does not split Left and Right yet (both show the same time until milestone 6).
- **Engine (window only):** `core/ZoneMap.h` (free-running delay: logarithmic across each Zone's range, Rate left = shortest; Tempo Sync: the manual's 13 multiples and
  divisions in 13 equal bands, 1/1 in the middle, Zone steps by powers of 2 from the Zone that fits the clock best), Logic's tempo and bar length kept for the window, and
  two counters (clock ticks, delay-time repeats) behind the lights. `process()` still leaves the audio exactly as it was.
- **Copied from PhenomeEon and renamed:** `PanelLookAndFeel` (without the tape-reel colours and the record dot), the five font files and their licence texts,
  the knob card and chip selector, the window-switching pattern.
- **Checked before hand-over (Linux test computer):** 159 engine checks (31 new) and 197 plugin-side checks (106 new), all passing; pictures drawn at 840, 1200 and 1800 pixels wide
  and compared with the mockup. **GitHub's Mac build (run #4, commit a792619, both jobs green):** 159 engine checks, `auval` AU VALIDATION SUCCEEDED, `pluginval` passed at strictness level 5 for AU and VST3, package archs `x86_64 arm64`.
- **Tested in Logic on both Macs (6 November 2026):** as described, apart from Clear showing no sign of being clicked and the small grey text being too small (both fixed in 0.3.1).

Version 0.2.0 (milestone 2, the empty build; **tested in Logic on both Macs on 6 November 2026 and working as described**). The first code. The plugin loads
and passes the audio through unchanged; there is no repeater yet.

- **Plugin:** AU and VST3 audio effect called MimicPhon (Logic: Audio Units > particledots > MimicPhon). Bundle id
  `com.particledots.mimicphon`, manufacturer code `Pdot`, plugin code `Mph1` (`auval -v aufx Mph1 Pdot`). Universal binary
  (arm64 + x86_64), macOS 11 or newer, ad-hoc signed, JUCE 8.0.17-security. Mono and stereo tracks.
- **All 21 controls declared and frozen** (ids never change later; a new control would be added at the end):
  10 knobs (`inputLevel`, `mix`, `repeats`, `halo`, `outputLevel`, `color`, `rate`, `uRate`, `uRateLfoRate`, `uRateLfoDepth`),
  4 menus (`zone` with its eight ranges, `inputGain` -3 / 0 / +3 / +6 dB, `uRateLfoShape` Sine / Triangle,
  `clockDivision` 2 bars ... 1/32 starting at 1/4) and 7 buttons (`hold`, `flip`, `skew`, `pingPong`, `clear`, `tempoSync`,
  `uRateLfoSync`). None of them changes the sound yet.
- **Plain slider window:** a slider, menu or button for every control, resizable with the 3:2 shape kept, with a note on its face
  that this is the empty build.
- **Clear** is a momentary button: the engine notices it, then the wrapper switches it back off; a project saved with Clear on opens
  with it off.
- **Engine skeleton** (`engine/`): `Params.h` (control values, the Zone table from the manual, input gain steps), `Engine.h` (pass-through),
  `core/HostClock.h` (Logic's tempo clock, copied from PhenomeEon without its Morphagene-only tables) and `core/Gain.h` (level knob curve,
  decibels, soft limiter, copied from PhenomeEon without the Sound On Sound law and loop fades).
- **Checked before hand-over:** on a Linux test computer, 128 engine checks and 91 plugin-side checks (see the README). On GitHub's Mac (run #1, both jobs green): 128 engine checks again, `auval` AU VALIDATION SUCCEEDED, `pluginval` passed at strictness level 5 for AU and VST3, package archs `x86_64 arm64`.
- **Tested in Logic:** on the M4 Mac mini (macOS Tahoe, Logic 11.2.2) and the Intel Mac (macOS 12.7.8, Logic 10.7.8): loads under Audio Units > particledots > MimicPhon, audio unchanged, window opens and resizes with all 21 controls, Clear switches itself off, saved projects reopen with the same settings. All as described on both.
- **Soft limiter note:** the copied soft limiter never goes above 1.0, but in single precision it reaches exactly 1.0 for very loud
  input (above about 2.6); its comment now says so (PhenomeEon's comment says it never reaches 1.0, which is true only on paper).

Version 0.0 (milestone 1, the panel picture; approved 6 November 2026). `MimicPhon_panel_mockup_1.png`: the approved 1200 x 800 panel
in charcoal, amber and teal, with the Zone window, the zone staircase, ten knobs, the buttons and the tempo readout.
