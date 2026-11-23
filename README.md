# MPC Rate Bridge

An addin for MPC OS standalone devices that lets MPC use a USB audio interface that can't run at 44.1 kHz, such as the 48 kHz-only
Neural DSP Quad Cortex mini, as its audio device.

MPC runs at 44,100 Hz and asks the device for exactly that. A 48 kHz-only interface refuses, and MPC shows
*"Error: 44100 may not be a supported sample rate on the external device. Code: -22"* and goes back to its own audio. With this
addin, when MPC opens such a USB interface, the interface runs at its own rate and alsa-lib's rate converter sits in between. MPC
sees a 44.1 kHz device with the interface's real channels (8 in and 8 out on the Quad Cortex mini), and you can pick it in
**Preferences > Audio** and record its inputs straight onto audio tracks.

Nothing else changes: the MPC's own audio, other USB interfaces that do 44.1 kHz, and every call after the device is opened go
to alsa-lib untouched.

## Status

**Preview.** Tested offline, and on an MPC Key 61 (Gen1, MPC OS 3.10.1) with a Quad Cortex mini (2026-11-22): installed as an
addin, MPC accepts the interface in Preferences (`Audio 44100Hz; 8-in; 8-out; 128sample buffer`), plays through it, and a guitar take
recorded on an audio track (input 1/2) played back cleanly, with no audio errors in MPC's log. Round-trip latency measured (below). Not yet tried on other devices or interfaces.

## How it works

The addin replaces one function, `snd_pcm_open`. MPC opens its audio device as `hw:<card>`. When that card is a USB audio card
whose `/proc/asound/card<N>/stream0` lists no 44100 for the direction being opened, the addin opens alsa-lib's `type rate` PCM over
the same `hw` device at the card's own rate (48 kHz when it has it) instead. The rate plugin converts the sample rate only, so
MPC's own setup (exact 44,100 Hz, as many channels as the device has, 192-frame periods, explicit start) works as it does for any
device. Every other open is passed on with `dlsym(RTLD_NEXT)`, so it works alongside other audio addins.

**Sound:** the converter is alsa-lib's built-in linear one, which works at 16-bit resolution. Recorded side by side with the
interface's native 48 kHz, a listener couldn't tell them apart on an amp-modelled guitar. Measured, it adds faint noise above
10 kHz (about 65-75 dB below the signal). A better converter is a possible later version.

**Latency** (Key 61 + Quad Cortex mini, measured 2026-11-22 with a cable loop from a QC output into QC input 2, recording the dry
input): MPC picked a 128-sample buffer, which the converter runs as 140-frame periods at 48 kHz. The full round trip is **736 samples
(16.7 ms)**. MPC already compensates 672 samples (15.2 ms) when it places a take, so recordings land **1.45 ms late**, identically on
every click (jitter about 0.01 ms, no drift over 4 bars). Set MPC's record offset to 64 samples earlier if you want it exact. The
QC's own processing (amp models) adds its latency on top when you record its processed output; that part wasn't measured.

## Setting up a Quad Cortex mini

Found on a Key 61 (MPC OS 3.10.1), 2026-11-22:

1. Install the addin, connect the Quad Cortex mini to a USB-A port, and pick it in MPC's **Preferences > Audio**. MPC then reports
   8 inputs and 8 outputs.
2. On the Quad Cortex, set its USB audio to **Multi Out**.
3. In MPC, send the audio you want to hear to **outputs 3/4** (the main output or the track outputs). With the QC set as above,
   that's what reaches its outputs and headphones.
4. The QC's processed signal arrives on MPC **inputs 1/2** (set the QC to send its output to USB 1/2), and its dry input on input 3.
   Record it on an audio track with input 1/2.

## Things to know

- **The interface becomes MPC's only audio device, outputs included.** Once you pick it in Preferences, all of MPC's sound (the main
  mix, plugins, pads) goes out through the interface, and the MPC's own outputs and headphone jack go silent. Listen on the
  interface's outputs or headphones, and make sure it sends its USB playback channels (USB 1/2 carries MPC's main mix) to them. To
  keep listening on the MPC while recording the interface, use a plugin that captures it instead (e.g. MPC USB Bridge).
- **The MPC's main volume knob does nothing while the interface is selected** (Key 61, seen 2026-11-22). The knob controls the
  MPC's own audio hardware, which MPC no longer uses, and MPC deliberately ignores an external device's volume controls (it logs
  `Ignoring non-boolean control PCM Playback Volume`). Set the listening level on the interface; MPC's master fader still works.
- **Re-select the interface after every MPC start.** MPC doesn't keep it: on a Key 61 (3.10.1) it came back on its own audio after
  each restart, and `MPC.settings` holds an empty `audioSetup` (`audioOutputDeviceName=""`), which means the built-in device. MPC
  rewrites that file while it runs (for example when you leave Preferences), and it wrote the empty value even while running on the
  interface, so a clean shutdown doesn't help: MPC doesn't save an external audio device on this firmware. So after a
  restart or power cycle, pick the interface again in **Preferences > Audio**. (It also means uninstalling the addin never leaves
  MPC stuck on a device it can't open.)
- **Two "AUDIO DEVICE ERROR: Failed to open Capture Stream" dialogs at startup** with the Quad Cortex mini connected (Key 61, 3.10.1).
  MPC opens its own audio device, then about a second later tries to open the same device again while still holding it, and gets
  "device busy" twice (`Failed to open ALSA device for capture hw:1`). Close them and carry on: the audio already running is
  unaffected. The failing opens are of the MPC's own device, which the addin passes through untouched, and they happened with and
  without other addins loaded (not yet checked with this one disabled).
- **Other audio addins.** The addin only changes which device MPC's open returns, and passes every call on, so addins that hook the
  same calls keep working. Addins that watch MPC's main audio stream (for example `mpc-addin-usb-audio`, which forwards it to a
  computer over USB) will see the interface's stream once it is MPC's audio device, instead of the MPC's own.
- **To undo:** `sh /data/mpc-addins/rate-bridge/uninstall.sh -y` over SSH (MPC restarts once), or `enabled=0` in
  `rate_bridge.conf` and restart MPC. If MPC ever fails to start because of an addin, the recovery steps are in `docs/ADDINS.md` of
  open-mpc/mpc-vst-plugins ("When an addin stops MPC from starting").

## Settings

`rate_bridge.conf` in the addin's folder (`/data/mpc-addins/rate-bridge/`). MPC reads it when it opens an audio device.

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `1` | `0` opens every device as if the addin weren't there |
| `cards` | empty | Empty: any USB card without 44.1 kHz. Otherwise a list of card ids or numbers (from `/proc/asound/cards`) to bridge |
| `log` | `1` | One line per bridged or examined device in `rate_bridge.log` next to the settings |

## Building and testing

- `tools/test_host.sh`: offline tests in a Linux container (Docker). Unit tests for the parsers, then the addin preloaded into a
  driver that opens PCMs the way MPC does, against a stand-in `/proc/asound` holding the Quad Cortex mini's real `stream0`.
- `MPC_VST=<mpc-vst-plugins checkout> tools/build_armhf.sh`: the Gen1 build in `build/package/`, with the shared addin installer.
  `tools/build_aarch64.sh` (same `MPC_VST`): the Gen2 build in `build/aarch64/`. **The Gen2 build is not yet tested on a Gen2 device.**
- `tests/device_check.c`: run MPC's opening sequence against a device on the MPC itself, with the addin preloaded
  (`LD_PRELOAD=.../librate_bridge.so RATE_BRIDGE_DIR=/tmp ./device_check hw:2`), without installing anything.

Installing an addin restarts MPC (`sh install.sh` in the package folder; `sh /data/mpc-addins/rate-bridge/uninstall.sh` removes
it). The installer and the rules addins follow are in [open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins)
(`docs/ADDINS.md`).

## Licence

MIT.
