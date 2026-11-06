# Mimic-Phon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack stereo multi-zone colour repeater (the
Make Noise / soundhack Mimeophon): eight Zones of delay time from a millisecond to over forty seconds, a Rate that
moves smoothly inside each Zone, repeats up to infinite, and colour and stereo smear on the repeats. All DSP is our own
code; it is not a port or copy of anything, and it is not affiliated with Make Noise or soundhack.

**Status: version 0.2.0 (milestone 2: the empty build), tested in Logic on both test Macs (6 November 2026) and working as described.**
Milestone 1 (the panel picture) was approved the same day. **The plugin makes no sound of its own yet: the audio passes
through unchanged and none of the controls does anything audible.** The repeater is built in the next milestones.

## First five minutes

1. Install it (see *Getting the plugin* below) and put **MimicPhon** on an audio track: Audio FX > Audio Units >
   particledots > MimicPhon. It works on mono and stereo tracks.
2. Press Play in Logic with something on the track. You hear the track exactly as it was: this version changes nothing.
3. Open the window: the plain slider window with a slider, menu or button for every control. Move any of them; nothing
   changes in the sound yet. (Each one can be automated in Logic: check that under the track's Automation menu.)
4. Press **CLEAR** while Logic plays: the button lights and switches itself off again a moment later (that is how it will
   behave once there is something to clear).
5. Save the Logic project, close it and open it again: the controls come back where you left them.

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

- **This version is silent on purpose.** It exists to prove the plugin installs, loads in Logic, keeps its settings, and that all the controls and their ids are in place before any sound code is added.
- **Controls keep their ids forever.** If a later milestone ever needs one more control, it is added at the end, and older projects open with it at its starting position.
- **Starting positions can still change.** A new MimicPhon opens with the values in the window above; these may be tuned by ear in later milestones. Projects you save keep their own values.
- **The Clear button switches itself off** a moment after it goes on, and a project saved while Clear was on opens with it off.
- **Mono and stereo tracks are both supported.** Other channel layouts are refused by the plugin.
- Plugin buttons act while Logic is playing: Logic does not run effects while it is stopped.

## What has been checked

Everything below was run before this version was handed over; "tested in Logic" is listed separately because only that
is done by hand on the real Macs.

- **Engine self-tests (128 checks):** the control list and its starting positions; the eight Zone ranges against the manual; the audio coming out of the engine bit for bit equal to what went in (mono and stereo, 44.1, 48 and 88.2 kHz, block sizes from 1 to 4096 samples, random control values and buttons, special values including not-a-number); the Clear button noticed once; the tempo clock (ticks per second at different tempos, sample rates and divisions); the level helpers. All pass, also under a memory-error and undefined-behaviour checker. Run on a Linux test computer.
- **Plugin side (preview tool, 91 checks):** the 21 control ids are exactly the frozen list; each control changes exactly one parameter and each parameter has exactly one control; Logic-style automation moves the controls; values survive saving and reloading; the real plugin passes audio through bit for bit (mono, stereo, 44.1 / 48 / 88.2 kHz, block sizes 1 to 4096, with every control at default, at maximum and at minimum); the Clear button switches itself off; a project saved with Clear on opens with it off; the window opened and closed 80 times at four sizes while audio ran; mono and stereo layouts accepted and others refused. The window was drawn to a picture and looked at. Run on a Linux test computer, not on the M4 or the Intel Mac.
- **GitHub build on a Mac (run #1, commit 0198f28, both jobs green):** the engine self-test job reported 128 checks, 0 failed; the macOS job compiled the AU and VST3, `auval -v aufx Mph1 Pdot` reported **AU VALIDATION SUCCEEDED**, `pluginval` 1.0.4 **passed at strictness level 5** for both the AU and the VST3, and the package contains both architectures (`x86_64 arm64`). The same package is the **MimicPhon-macOS** download under Artifacts and a private draft release.
- **Tested in Logic (6 November 2026), on both the M4 Mac mini (macOS Tahoe, Logic Pro 11.2.2) and the Intel Mac (macOS 12.7.8, Logic Pro 10.7.8):** the plugin appears under Audio Units > particledots > MimicPhon and loads; the sound is exactly the same as without it; the window opens, resizes and shows all 21 controls; Clear lights and switches itself off; saved projects reopen with the same settings. All worked as described on both Macs.

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
| `engine/` | The audio engine. Plain C++, no plugin code. `engine/include/mph/core/` holds the generic parts (the tempo clock from Logic, level helpers); `Engine.h` is the engine and `Params.h` the list of control values it understands, with the Zone table. `engine/tests/` holds the self-test. |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls (`Source/Parameters.h`, frozen), and the window. `plugin/preview/` is an optional tool that draws the window to a picture and checks the controls (not part of the plugin). |
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
