# Mimic-Phon: version history

The detailed notes for each milestone, newest first. A version is listed as tested only after it has been tested in Logic on both
test Macs (the M4 Mac mini and the Intel Mac) and worked as described. The README describes the current version.

Version 0.2.0 (milestone 2, the empty build; built, **waiting for its test in Logic on both Macs**). The first code. The plugin loads
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
- **Checked before hand-over** (on a Linux test computer): 128 engine checks and 91 plugin-side checks (see the README).
- **Soft limiter note:** the copied soft limiter never goes above 1.0, but in single precision it reaches exactly 1.0 for very loud
  input (above about 2.6); its comment now says so (PhenomeEon's comment says it never reaches 1.0, which is true only on paper).

Version 0.0 (milestone 1, the panel picture; approved 6 November 2026). `MimicPhon_panel_mockup_1.png`: the approved 1200 x 800 panel
in charcoal, amber and teal, with the Zone window, the zone staircase, ten knobs, the buttons and the tempo readout.
