# Mimic-Phon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack stereo multi-zone colour repeater (the
Make Noise / soundhack Mimeophon): eight Zones of delay time from a millisecond to over forty seconds, a Rate that
moves smoothly inside each Zone, repeats up to infinite, and colour and stereo smear on the repeats. All DSP is our own
code; it is not a port or copy of anything, and it is not affiliated with Make Noise or soundhack.

**Status: version 0.3.1 (milestone 3: the styled panel, with two small fixes after the first Logic test). Checked on a Linux test computer; 0.3.0 was also checked by GitHub's Mac build (run #4, all green). Version 0.3.0 was tried in Logic on both test Macs and worked as described apart from the two things fixed here (Clear showed no sign of being clicked; some small grey text was too small). Version 0.3.1 is not yet tested in Logic.**
Version 0.2.0 (the empty build) was tested in Logic on both test Macs on 6 November 2026 and worked as described; the panel picture
(milestone 1) was approved the same day. **The plugin still makes no sound of its own: the audio passes through unchanged and
none of the controls does anything audible.** The repeater is built in the next milestones. What is new is the window.

## First five minutes

1. Install it (see *Getting the plugin* below) and put **MimicPhon** on an audio track: Audio FX > Audio Units >
   particledots > MimicPhon. It works on mono and stereo tracks.
2. Press Play in Logic with something on the track. You hear the track exactly as it was: this version changes nothing.
3. Open the window: you see the **styled panel**, the picture you approved. Turn a knob, change the Zone, press a button; the Zone
   window and the read-outs follow. None of it changes the sound yet. (Each control can be automated in Logic: check that under the
   track's Automation menu.)
4. Press **PLAIN SLIDERS** (top right): the window changes to the plain slider window of 0.2.0, with a slider, menu or button for every
   control. Press **Styled panel** (top right there) to come back. Close the window and open it again, or save and reopen the
   project: it opens on the window you left it on.
5. While Logic plays, watch the **TICK** and **RATE** lights in the box at the right of the Zone band: TICK blinks with Logic's tempo
   (at the Clock Division, 1/4 to begin with), RATE blinks at the current delay time.
6. Press **CLEAR** while Logic plays: the button lights and switches itself off again a moment later.
7. Save the Logic project, close it and open it again: the controls come back where you left them.

## The window (new in 0.3.0)

Laid out like the approved picture (milestone 1); it can be resized (the 3:2 shape stays).

- **Zone window and staircase:** the big square shows the selected Zone in its colour (0 green, 1 lime, 2 yellow, 3 orange, 4 red,
  5 magenta, 6 lilac, 7 blue); the staircase beside it lists the eight Zones with their ranges and lights the selected one. The
  square dims and brightens, half a second each way, while **Ping Pong / Swap** is on.
- **Delay and tempo box:** the delay time that Zone and Rate give (and, with **Tempo Sync** on, the one that the Rate step and the
  Clock Division give), Logic's tempo, and the TICK and RATE lights. If Logic has not told the plugin a tempo, the box says
  *TEMPO 120.0 (assumed)*.
- **Ten knob cards and the Zone knob:** Input Level (with the -3 / 0 / +3 / +6 dB chips), Mix, Repeats, Halo, Output Level; Zone
  (eight steps, the arc takes the Zone's colour), Color (teal mark at about 3:00, "unity"), Rate (teal mark at 12:00, "1/1"), uRate,
  and the uRate LFO card (Rate, Depth, Sine / Triangle, Sync). Below: Hold, Flip, Skew, Ping Pong / Swap, Clear, Tempo Sync (teal when
  on) and the Clock Division menu.
- **Read-outs are first guesses.** Until a control has its sound they show what its position is likely to mean: Input / Output Level in
  dB, Mix as a percentage wet, Repeats and Halo and LFO Depth as percentages, Color as the word it will probably sound like (oil can,
  warm BBD, crunch BBD, tape, clean, thin), uRate as semitones (plus or minus 12), LFO Rate in Hertz (0.1 to 20), and the delay time from
  the Zone ranges in the manual. They are tuned together with the sound in the later milestones; the control ids never change.
- **Skew does not split Left and Right yet:** both read-outs show the same delay time until Skew works (milestone 6).

## The controls

All 21 controls exist now. **Their ids are frozen:** Logic stores these ids in projects and automation, so they never
change. Most do nothing yet; the table says which milestone makes each one work.

| Control | What it will do | Works from |
|---|---|---|
| **Input Level** (+ **Input Gain Step** -3 / 0 / +3 / +6 dB) | How loud the signal going into the repeater is; the step is the module's firmware input-gain choice. | Milestone 4 |
| **Mix** | Balance between the live signal and the repeats. | 4 |
| **Repeats** | How many repeats: one echo up to infinite (100% feedback) and beyond; it saturates and never runs away. When Hold is on, it sets the start point of the loop. | 4 |
| **Output Level** | How loud the output is. | 4 |
| **Zone** | One of eight ranges of delay time: 0 = 1.3 - 20.4 ms, 1 = 20.4 - 81.6 ms, 2 = 81.6 - 326.5 ms, 3 = 163.3 - 653.1 ms, 4 = 326.5 ms - 1.31 s, 5 = 653 ms - 2.61 s, 6 = 1.31 - 5.23 s, 7 = 2.61 - 41.8 s. Switching Zone jumps instantly, without a pitch effect. | 4 |
| **Rate** | The delay time inside the Zone; moves smoothly (with a doppler effect on the repeats). | 4 |
| **Clear** | Empties all the Zones. A momentary button: it acts, then switches itself off. | 4 |
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

- **This version is silent on purpose.** It adds the styled panel on top of the empty build, so the window can be looked at and judged before any sound code is added.
- **Window choice is saved with the project, not as a control.** It does not appear in Logic's automation list. A project saved with 0.2.0 opens with the styled panel.
- **Tempo comes from Logic.** The panel shows Logic's tempo while the plugin is processing audio (Logic does not run effects while it is stopped, so the box can lag behind a tempo change until you press Play).
- **Controls keep their ids forever.** If a later milestone ever needs one more control, it is added at the end, and older projects open with it at its starting position.
- **Starting positions can still change.** A new MimicPhon opens with the values in the window above; these may be tuned by ear in later milestones. Projects you save keep their own values.
- **The Clear button switches itself off** a moment after it goes on, and a project saved while Clear was on opens with it off.
- **Mono and stereo tracks are both supported.** Other channel layouts are refused by the plugin.
- Plugin buttons act while Logic is playing: Logic does not run effects while it is stopped.

## What has been checked

Everything below was run before this version was handed over; "tested in Logic" is listed separately because only that
is done by hand on the real Macs.

- **Engine self-tests (159 checks; 0.3.0 added 31):** everything from 0.2.0, and new in 0.3.0: the Zone / Rate delay-time map (each Zone's shortest and longest time, the geometric middle, steadily rising with the knob), the 13 tempo-sync steps and their factors from the manual, the Zone that fits a clock, the synced delay always inside the module's range, Logic's tempo kept for the window, and the counters behind the TICK and RATE lights at different block sizes. Before:  the control list and its starting positions; the eight Zone ranges against the manual; the audio coming out of the engine bit for bit equal to what went in (mono and stereo, 44.1, 48 and 88.2 kHz, block sizes from 1 to 4096 samples, random control values and buttons, special values including not-a-number); the Clear button noticed once; the tempo clock (ticks per second at different tempos, sample rates and divisions); the level helpers. All pass, also under a memory-error and undefined-behaviour checker. Run on a Linux test computer.
- **Plugin side (preview tool, 197 checks; 0.3.0 added 106):** in both windows,  the 21 control ids are exactly the frozen list; each control changes exactly one parameter and each parameter has exactly one control; Logic-style automation moves the controls; values survive saving and reloading; the real plugin passes audio through bit for bit (mono, stereo, 44.1 / 48 / 88.2 kHz, block sizes 1 to 4096, with every control at default, at maximum and at minimum); the Clear button switches itself off; a project saved with Clear on opens with it off; the window opened and closed 80 times at four sizes while audio ran; mono and stereo layouts accepted and others refused. The window was drawn to a picture and looked at. New in 0.3.0: both windows are checked control by control; PLAIN SLIDERS and Styled panel really switch, the choice survives saving and reopening, a project without it opens styled, and switching moves no control; the Zone window shows the right colour for all eight Zones; the delay, tempo and Rate read-outs say the right things (including Tempo Sync at 120 BPM); the Zone window flashes only with Ping Pong; the TICK and RATE lights blink while audio runs and go out when it stops. The panel was drawn to pictures at the smallest, normal and largest window size and compared with the approved mockup. Run on a Linux test computer, not on the M4 or the Intel Mac.
- **GitHub build on a Mac for 0.3.1 (run #6, commit 8441fb1, both jobs green):** engine self-test 159 checks, 0 failed; `auval` AU VALIDATION SUCCEEDED; `pluginval` 1.0.4 passed at strictness level 5 for AU and VST3; package archs `x86_64 arm64`; private draft release `test-build-6`.
- **GitHub build on a Mac for 0.3.0 (run #4, commit a792619, both jobs green):** the engine self-test job reported 159 checks, 0 failed; the macOS job compiled the AU and VST3, `auval -v aufx Mph1 Pdot` reported **AU VALIDATION SUCCEEDED**, `pluginval` 1.0.4 **passed at strictness level 5** for both the AU and the VST3, and the package contains both architectures (`x86_64 arm64`). The package is the **MimicPhon-macOS** download under Artifacts of that run and a private draft release (`test-build-4`).
- **GitHub build on a Mac for 0.2.0 (run #1, commit 0198f28, both jobs green):** the engine self-test job reported 128 checks, 0 failed; the macOS job compiled the AU and VST3, `auval -v aufx Mph1 Pdot` reported **AU VALIDATION SUCCEEDED**, `pluginval` 1.0.4 **passed at strictness level 5** for both the AU and the VST3, and the package contains both architectures (`x86_64 arm64`). The same package is the **MimicPhon-macOS** download under Artifacts and a private draft release.
- **0.2.0 tested in Logic (6 November 2026), on both the M4 Mac mini (macOS Tahoe, Logic Pro 11.2.2) and the Intel Mac (macOS 12.7.8, Logic Pro 10.7.8):** the plugin appears under Audio Units > particledots > MimicPhon and loads; the sound is exactly the same as without it; the window opens, resizes and shows all 21 controls; Clear lights and switches itself off; saved projects reopen with the same settings. All worked as described on both Macs. **0.3.0 has not been tested in Logic yet.**

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
| `engine/` | The audio engine. Plain C++, no plugin code. `engine/include/mph/core/` holds the generic parts (the tempo clock from Logic, level helpers, the Zone / Rate time map); `Engine.h` is the engine and `Params.h` the list of control values it understands, with the Zone table. `engine/tests/` holds the self-test. |
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
