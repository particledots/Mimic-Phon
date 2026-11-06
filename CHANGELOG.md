# Mimic-Phon: version history

The detailed notes for each milestone, newest first. A version is listed as tested only after it has been tested in Logic on both
test Macs (the M4 Mac mini and the Intel Mac) and worked as described. The README describes the current version.

Version 0.3.1 (milestone 3, small fixes after Bryn's first Logic test; **not yet tested in Logic**). Bryn tested 0.3.0 on both Macs: everything worked as described, with two things to fix.

- **Clear now shows itself:** Clear switches itself off the instant the engine has handled it, so the button was never seen lit. In the styled panel it now stays lit for about a third of a second after each click. (The control, its id and its behaviour are unchanged.)
- **Easier to read:** the small grey texts (notes under the knobs, the DARK / unity / THIN marks, the Zone staircase ranges, the DELAY / TICK / RATE labels, the tempo line, the LFO RATE / DEPTH labels, the footer) are larger, and the grey itself is lighter. The note under Repeats is shortened to "ONE ECHO ... INFINITE+" so it fits at the bigger size.
- Checks on the Linux test computer: engine 159, window 197, all passing. **GitHub's Mac build (run #6, commit 8441fb1, both jobs green):** 159 engine checks, `auval` AU VALIDATION SUCCEEDED, `pluginval` strictness 5 passed for AU and VST3, archs `x86_64 arm64`.

Version 0.3.0 (milestone 3, the styled panel; **not yet tested in Logic**). The window from the approved picture. Still no repeater.

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
- **Not tested in Logic yet.**

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
