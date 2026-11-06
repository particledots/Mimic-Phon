# Mimic-Phon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack stereo multi-zone colour repeater (the
Make Noise / soundhack Mimeophon): eight Zones of delay time from a millisecond to over forty seconds, a Rate that
moves smoothly inside each Zone, repeats up to infinite, and colour and stereo smear on the repeats. All DSP is our own
code; it is not a port or copy of anything, and it is not affiliated with Make Noise or soundhack.

**Status: version 0.4.0 (milestone 4: the core repeater). Checked on a Linux test computer and by GitHub's Mac build (run #8, commit b0b0f3f, both jobs green). Not yet tested in Logic.**
Version 0.3.1 (the styled panel) was tested in Logic on both test Macs on 6 November 2026 and worked as described ("all good, much better"); version 0.2.0 (the empty build) and the panel picture (milestone 1) were tested and approved the same day.
**The plugin now makes sound: Input Level and its gain step, Zone, Rate, Repeats, Mix, Output Level and Clear work.** Color, Halo, Skew, Ping Pong / Swap, uRate and its LFO, Tempo Sync, Flip and Hold are still silent: they are built in the next milestones.

## First five minutes

1. Install it (see *Getting the plugin* below) and put **MimicPhon** on an audio track: Audio FX > Audio Units >
   particledots > MimicPhon. It works on mono and stereo tracks. Use something with clear gaps in it (drums, a plucked or played
   phrase) and press Play.
2. **Starting positions:** you hear the track plus an echo about two thirds of a second later, which dies away over about five
   seconds. The window says **653 ms** (Zone 4, Rate in the middle) and Repeats **x 0.35** (each echo is a third as loud as the one before).
3. **Mix:** all the way left gives only the original sound, all the way right gives only the echoes.
4. **Repeats:** all the way left gives a single echo; turning right gives more and more. Near the top there is a short stretch that reads
   **infinite** (the echoes never die away); past it, **inf +12 %** at the very end, the echoes grow and fill up until they saturate, but they never run away. Turn Repeats back down to calm it.
5. **Rate:** turn it slowly. The delay time moves smoothly inside the Zone (the window shows it) and, with Repeats up, the echoes bend in pitch
   while it moves (doppler); it settles back to the normal pitch a moment after you stop.
6. **Zone:** step through the eight Zones. The echo time jumps at once, **without** a pitch bend (a short crossfade of about a fiftieth of a second).
   Zone 0 and 1 (1.3 to 82 ms) give buzzing, flanging and ringing tones rather than echoes; Zones 2 to 4 are ordinary echoes; Zones 5 to 7 are long (up to 41.8 seconds).
   Try Zone 7 with Rate to the right: whatever you play comes back almost 42 seconds later.
7. **Input Level**, the **-3 / 0 / +3 / +6 dB** chips under it, and **Output Level**: how loud the signal going in and coming out is.
8. **Clear:** press CLEAR while echoes are ringing: they stop at once, in every Zone, and new sound goes in and comes back as before. (The button lights for a third of a second.)
9. Stop and start Logic's playback, and bounce a short piece: Logic is told how long the echoes keep sounding with the current settings.
10. Save the Logic project, close it and open it again: the controls come back where you left them.
11. **Tempo Sync, Color, Halo, Skew, Ping Pong / Swap, uRate and its LFO, Flip and Hold are expected to do nothing yet.** (Tempo Sync even keeps showing the free-running time in the window, so the window never says something the sound does not do.)

## The window

Laid out like the approved picture (milestone 1); it can be resized (the 3:2 shape stays).

- **Zone window and staircase:** the big square shows the selected Zone in its colour (0 green, 1 lime, 2 yellow, 3 orange, 4 red,
  5 magenta, 6 lilac, 7 blue); the staircase beside it lists the eight Zones with their ranges and lights the selected one. The
  square dims and brightens, half a second each way, while **Ping Pong / Swap** is on.
- **Delay and tempo box:** the delay time that Zone and Rate give (the engine uses the very same function, so this is the time you hear), Logic's tempo, and the TICK and RATE lights.
  Until Tempo Sync is built (milestone 8) the box shows the free-running time whatever the Tempo Sync button says. If Logic has not told the plugin a tempo, the box says *TEMPO 120.0 (assumed)*.
- **Ten knob cards and the Zone knob:** Input Level (with the -3 / 0 / +3 / +6 dB chips), Mix, Repeats, Halo, Output Level; Zone
  (eight steps, the arc takes the Zone's colour), Color (teal mark at about 3:00, "unity"), Rate (teal mark at 12:00, "1/1"), uRate,
  and the uRate LFO card (Rate, Depth, Sine / Triangle, Sync). Below: Hold, Flip, Skew, Ping Pong / Swap, Clear, Tempo Sync (teal when
  on) and the Clock Division menu.
- **Read-outs:** the ones for controls that make sound now come from the engine's own functions, so the number shown is the number heard: Input Level and
  Output Level in dB, Mix as a percentage wet, **Repeats** as "one echo", "x 0.35" (how much of each echo is fed back), "infinite" and "inf +12 %", and the delay time. The others are still first guesses until their milestone
  (Halo and LFO Depth as percentages, Color as the word it will probably sound like, uRate as semitones, LFO Rate in Hertz).
- **Skew does not split Left and Right yet:** both read-outs show the same delay time until Skew works (milestone 6).

## The controls

All 21 controls exist. **Their ids are frozen:** Logic stores these ids in projects and automation, so they never
change. Some still do nothing; the table says which milestone makes each one work.

| Control | What it will do | Works from |
|---|---|---|
| **Input Level** (+ **Input Gain Step** -3 / 0 / +3 / +6 dB) | How loud the signal going into the repeater is; the step is the module's firmware input-gain choice. | **Works (0.4.0)** |
| **Mix** | Balance between the live signal and the repeats (equal power: all live, to all repeats). | **Works (0.4.0)** |
| **Repeats** | How many repeats: one echo up to infinite (100% feedback) and beyond; it saturates and never runs away. When Hold is on, it will set the start point of the loop (milestone 11). | **Works (0.4.0)** |
| **Output Level** | How loud the output is. | **Works (0.4.0)** |
| **Zone** | One of eight ranges of delay time: 0 = 1.3 - 20.4 ms, 1 = 20.4 - 81.6 ms, 2 = 81.6 - 326.5 ms, 3 = 163.3 - 653.1 ms, 4 = 326.5 ms - 1.31 s, 5 = 653 ms - 2.61 s, 6 = 1.31 - 5.23 s, 7 = 2.61 - 41.8 s. Switching Zone jumps instantly, without a pitch effect. | **Works (0.4.0)** |
| **Rate** | The delay time inside the Zone; moves smoothly (with a doppler effect on the repeats). | **Works (0.4.0)** |
| **Clear** | Empties all the Zones at once. A momentary button: it acts, then switches itself off. | **Works (0.4.0)** |
| **Color** | Character of the repeats, from dark oil-can echo to thin digital; "unity" is at about 3:00. Also colours the Halo. | 5 |
| **Skew** | Rate then pulls the left and right repeats apart (one faster, one slower). | 6 |
| **Ping Pong / Swap** | Not skewed: repeats bounce left and right. Skewed: swaps the left and right feedback paths. One button. | 6 |
| **uRate** and **uRate LFO** (Rate, Depth, Sine / Triangle, Sync) | A small doppler-only pitch control (chorus, vibrato, flange) with its own built-in LFO; in Zone 0 it acts as a pitch for plucked-string tones. | 7 |
| **Tempo Sync** and **Clock Division** | Rate steps between multiples and divisions of Logic's tempo (without a pitch effect); the Clock Division menu (2 bars ... 1/32, starting at 1/4) sets the clock. | 8 |
| **Halo** | Stereo smear around the repeats; at high settings it is fed back into the repeats. | 9 |
| **Flip** | Reversed repeats. | 10 |
| **Hold** | Freezes the input and the repeats into a loop; all the other controls stay live. | 11 |

Left out on purpose (Logic automation replaces them): the hardware's CV inputs and attenuverters, the Rate Out pulse, the gate
inputs, MIDI and other clocks, tap tempo, and the hold-the-button gestures.

## Things worth knowing

- **How the repeater works (so the controls make sense):** everything you play is written into one long stereo memory (41.8 seconds, about 16 MB at 48 kHz) that all eight Zones share; a Zone is only a different delay time to read it at. That is why switching to a longer Zone can bring back sound from many seconds ago.
- **Rate glides, Zone jumps.** Rate moves the delay time smoothly (it takes about a twelfth of a second to settle), so the echoes bend in pitch while you turn it. A Zone change jumps to the new time with a short crossfade (a fiftieth of a second) and no pitch effect.
- **Repeats beyond infinite:** the feedback goes slightly above 100 % at the far end, and a soft limiter in the feedback keeps it from running away: it fills up and saturates. At **infinite**, sound louder than about -2 dB is held down a little at each pass until it sits just under that level, while quieter sound goes round forever.
- **Mix is equal power:** at 50 % the live signal and the echoes are each 3 dB down. The percentage shown is the knob position.
- **Clear empties every Zone at once, with no pause in the audio** (it does not erase the memory, it marks everything written so far as silent). It acts once when the button goes on.
- **Processor cost** (measured on the Linux test computer, not on the Macs): about 0.4 % of one core for a stereo track at 48 kHz and 0.8 % at 96 kHz, in a 60-second run with Zone changes.
- **Tail:** Logic is told how long the echoes keep sounding with the current settings (about 60 dB of fall; at infinite and beyond it says 120 seconds, the longest it will claim).
- **Tempo Sync and Clock Division do nothing yet** (milestone 8), and the window keeps showing free-running delay times whatever the Tempo Sync button says. The TICK light still follows Logic's tempo.
- **The two read points:** if you change Zone again within a fiftieth of a second of the last change, the new change takes over the crossfade in progress (in theory a tiny discontinuity; you are unlikely to hear it).
- **The values are first guesses.** The curve of Repeats (the "infinite" stretch is 0.80 to 0.86 on the knob), the Rate glide time, the crossfade time, the feedback's DC blocker and the soft limiter's knee can all be tuned by ear later without touching a control id.
- **Window choice is saved with the project, not as a control.** It does not appear in Logic's automation list.
- **Tempo comes from Logic.** The panel shows Logic's tempo while the plugin is processing audio (Logic does not run effects while it is stopped, so the box can lag behind a tempo change until you press Play).
- **Controls keep their ids forever.** If a later milestone ever needs one more control, it is added at the end, and older projects open with it at its starting position.
- **Starting positions can still change.** A new MimicPhon opens with the values in the window above; these may be tuned by ear in later milestones. Projects you save keep their own values.
- **The Clear button switches itself off** a moment after it goes on, and a project saved while Clear was on opens with it off.
- **Mono and stereo tracks are both supported.** Other channel layouts are refused by the plugin. Left and right are independent until Skew and Ping Pong / Swap (milestone 6).
- Plugin buttons act while Logic is playing: Logic does not run effects while it is stopped.

## What has been checked

Everything below was run before this version was handed over; "tested in Logic" is listed separately because only that
is done by hand on the real Macs.

- **Engine self-tests (244 checks; 0.4.0 added 85 and replaced the old "passes through unchanged" checks):** with Mix fully left and the levels at 0 dB the live signal comes out exactly as it went in (mono and stereo, 44.1, 48 and 88.2 kHz, block sizes 1 to 4096, random settings of everything else); the input gain steps and the level knobs in decibels; Mix at the middle is -3 dB; the Repeats law (one echo, the flat stretch at exactly infinite, beyond, never a jump, never fewer repeats when turned up); the first echo falls at the Zone's delay time in all eight Zones (and at the longest, 41.8 s, at 44.1 and 96 kHz, and the shortest, 1.3 ms); echoes fall by exactly the feedback; infinite holds for ten echoes and a full-scale sound settles just above the limiter's knee; beyond infinite with loud noise for 12 seconds stays real and below 3.0; not-a-number and infinity at the input never get into the repeats; a Zone jump gives no click and keeps the pitch (440 Hz stays 440 Hz); Rate bends the pitch down while it glides and the pitch is steady again after; Clear silences every Zone at once (Zone 3 and Zone 7), in the very block it goes on, sound after it works as before, and holding the button on does not clear again; left and right independent; mono equals the left of stereo; the sound does not depend on the block size (1 to 60000 samples); the delay is the same in seconds at 44.1 to 192 kHz; Zone and Rate changed every few hundred samples one sample at a time stay real; the tail the host is told. Before: the control list and its starting positions, the Zone ranges against the manual, the Clear notice, the tempo clock, the level helpers, the Zone / Rate time map, the counters behind the lights. All pass, also under a memory-error and undefined-behaviour checker. Run on a Linux test computer.
- **Plugin side (preview tool, 225 checks; 0.4.0 added 28):** in both windows, the 21 control ids are exactly the frozen list; each control changes exactly one parameter and each parameter has exactly one control; Logic-style automation moves the controls; values survive saving and reloading; PLAIN SLIDERS and Styled panel switch and the choice survives saving. New in 0.4.0, through the real processor: with Mix fully left the live signal passes bit for bit (mono, stereo, 44.1 / 48 / 88.2 kHz, block sizes 1 to 4096, with every other control at default, maximum and minimum); with Mix fully right and Repeats fully left an impulse comes back once, at the delay time the window shows, in **all eight Zones**, and the engine's delay time is the one the window works out; every control at maximum with loud noise stays real and below 20; every control at minimum is silent; the tail told to Logic (about 5.2 s at the starting positions, one echo with Repeats at 0, the cap at infinite); Clear silences the repeats in the very block it goes on. The window read-outs say what is heard: Input and Output Level, Mix, Repeats ("x 0.35", "one echo", "infinite", "inf +12 %"), the delay in every Zone, and the free-running times with Tempo Sync on. The window was opened and closed 80 times at four sizes while audio ran. The panel was drawn to pictures and looked at. Run on a Linux test computer, not on the M4 or the Intel Mac.
- **GitHub build on a Mac for 0.4.0 (run #8, commit b0b0f3f, both jobs green):** the engine self-test job reported 244 checks, 0 failed; the macOS job compiled the AU and VST3, `auval -v aufx Mph1 Pdot` reported **AU VALIDATION SUCCEEDED**, `pluginval` 1.0.4 **passed at strictness level 5** for both the AU and the VST3, and the package contains both architectures (`x86_64 arm64`). The package is the **MimicPhon-macOS** download under Artifacts of that run and a private draft release (`test-build-8`).
- **0.3.1 tested in Logic (6 November 2026), on both the M4 Mac mini (macOS Tahoe, Logic Pro 11.2.2) and the Intel Mac (macOS 12.7.8, Logic Pro 10.7.8):** the two things found in 0.3.0 (Clear showed no sign of being clicked; the small grey text was too small) are fixed; "all good, much better". **0.4.0 has not been tested in Logic yet.**
- **GitHub build on a Mac for 0.3.1 (run #6, commit 8441fb1, both jobs green):** engine self-test 159 checks, 0 failed; `auval` AU VALIDATION SUCCEEDED; `pluginval` 1.0.4 passed at strictness level 5 for AU and VST3; package archs `x86_64 arm64`.
- **Earlier builds:** 0.3.0 (run #4, commit a792619) and 0.2.0 (run #1, commit 0198f28) were both green with `auval` succeeded and `pluginval` passed at strictness level 5 for AU and VST3; 0.2.0 was tested in Logic on both Macs on 6 November 2026 and worked as described.

## What changed in 0.4.0

- **The core repeater** (milestone 4): Input Level and the Input Gain step, Zone, Rate, Repeats, Mix, Output Level and Clear now make sound. One shared stereo memory (41.8 s) serves all eight Zones; the delay time comes from the same `core/ZoneMap.h` function the window uses. Rate glides (doppler); a Zone change crossfades (no pitch effect); Repeats goes from one echo through infinite to beyond, through a DC blocker and the soft limiter; Mix is equal power; Clear empties every Zone at once.
- **The real tail length** is told to Logic (about 60 dB of fall, capped at 120 seconds).
- **Read-outs match the sound:** Repeats now reads "x 0.35" / "infinite" / "inf +12 %"; the delay time and the Rate knob read the time the engine uses; with Tempo Sync on (not working until milestone 8) the window shows free-running times.
- **New engine files:** `core/DelayBuffer.h` (the shared memory with cubic reading and an instant Clear), `core/Repeats.h` (the Repeats law and the tail). `Engine.h` / `Engine.cpp` hold the repeater.
- **Plain window** note rewritten (what works now, what is still silent).
- **No control id changed** (the same 21).
- Not in this version: Color, Halo, Skew, Ping Pong / Swap, uRate and its LFO, Tempo Sync, Flip, Hold (milestones 5 to 11).

## What changed in 0.3.0

- **The styled panel** (milestone 3), laid out like the approved picture, is now the window that opens; **PLAIN SLIDERS** switches to the plain slider window and **Styled panel** switches back; the choice is saved with the project.
- **Look and fonts** copied from PhenomeEon and renamed (Space Grotesk and IBM Plex Mono, built into the plugin).
- **Engine, for the window only:** `core/ZoneMap.h` (what Zone and Rate mean as a delay time, free running and with Tempo Sync) and counters for the TICK and RATE lights. The audio is still untouched, bit for bit.
- **No control id changed** (the same 21).

## What changed in 0.2.0

First code in the repository. Nothing before it to compare with.

- **The empty build:** AU and VST3 audio effect, name **MimicPhon**, manufacturer particledots, universal (Apple Silicon and Intel), macOS 11 or newer, ad-hoc signed. The audio passes through unchanged.
- **All 21 controls declared** (10 knobs, 4 menus, 7 buttons) and frozen, with a plain slider window for them.
- **The window** can be resized (the shape stays 3:2) and says on its face that this is the empty build.
- **Engine skeleton** (`engine/`): the control list, the Zone table, Logic's tempo clock and the level helpers copied from PhenomeEon, and the self-test.
- **Build recipe, installer and window scaling** copied from PhenomeEon (the installer unchanged).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. `engine/include/mph/core/` holds the generic parts (the tempo clock from Logic, level helpers, the Zone / Rate time map, the delay memory, the Repeats law); `Engine.h` is the engine (the repeater) and `Params.h` the list of control values it understands, with the Zone table. `engine/tests/` holds the self-test. |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls (`Source/Parameters.h`, frozen), and the windows (`StyledPanel` and `PanelLookAndFeel` for the styled panel, `PluginEditor` for the plain one and the switch; fonts in `Fonts/`). `plugin/preview/` is an optional tool that draws the windows to pictures and checks the controls (not part of the plugin). |
| `tools/install/` | The generic installer used by all the "Unofficial" plugins (the same file in every plugin). |
| `.github/workflows/` | The automatic build. |

The wrapper hands the engine the control values (one plain `Params` list), Logic's tempo and the audio; the engine hands audio
back. Nothing in `engine/` knows about sliders or Logic.

## Getting the plugin

1. Open the **Actions** tab of this repo and click the latest **Build MimicPhon** run.
2. At the bottom, under **Artifacts**, download **MimicPhon-macOS** and unzip it (twice: the download is a zip containing `MimicPhon-macOS.zip`).
3. Inside the `MimicPhon-macOS` folder are `MimicPhon.component`, `MimicPhon.vst3`, `INSTALL.txt`, `auval.log` and
   `Install-Unofficial-Plugin.command`.
4. In Terminal, once, clear the download block: `xattr -dr com.apple.quarantine ~/Downloads/MimicPhon-macOS/*`
   (adjust the path if you unzipped elsewhere).
5. **Easy install (Logic, Audio Unit):** double-click `Install-Unofficial-Plugin.command`. It asks for your Mac
   password once, installs the AU for all users and restarts the AU registrar.
   Or by hand: copy `MimicPhon.component` to `~/Library/Audio/Plug-Ins/Components/` and `MimicPhon.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
6. Restart Logic. If it was already scanned, Logic Pro > Settings > Plug-In Manager > Reset & Rescan Selection.

The plugin appears under **Audio Units > particledots > MimicPhon**.

## Notes

- Built as a universal binary (Apple Silicon and Intel), macOS 11 or newer.
- Signed ad-hoc only (no Apple Developer account), hence the quarantine step above.
- Built with JUCE 8.0.17-security, fetched by the build.
- PhenomeEon's build recipe, installer and window scaling are the model for this one.
