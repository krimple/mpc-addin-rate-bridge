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

> [!CAUTION]
> **Using 0.1.0? Update to 0.2.0 or later. 0.1.0 can leave your MPC in a crash loop at every start.**
>
> MPC sometimes saves the interface as its startup audio device, on its own, while you're using it. At the next start, MPC opens
> the interface while its own audio is still open and crashes (`terminate called without an active exception`). Then it restarts,
> crashes again, and keeps doing that until the setting is changed back. **What you'll see:** the pads light up cyan while MPC
> starts, go black, and it starts again, over and over. Unplugging the interface doesn't reliably help. Seen on an MPC Key 61,
> MPC OS 3.10.1, on 2026-11-24.
>
> **0.2.0 prevents this.** Each time MPC starts, before MPC reads its settings, the addin checks the saved device. If it is an
> interface the addin bridges, the addin resets it to the MPC's own audio. You then pick the interface again in
> **Preferences > Audio**, as you already do after every restart (see below).
>
> **If you're in the loop now**, over SSH:
> ```sh
> systemctl stop acvs
> cd /data/Settings/MPC && cp -p MPC.settings MPC.settings.bak
> sed -E 's/audio(Output|Input)DeviceName="[^"]*"/audio\1DeviceName=""/' MPC.settings > MPC.settings.new && mv MPC.settings.new MPC.settings
> systemctl start acvs
> ```
> Then install 0.2.0. (`systemctl stop acvs` stops the loop. The `sed` changes only the two device-name lines; check them with
> `diff MPC.settings.bak MPC.settings` before starting MPC again.)

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
- **The MPC's main volume knob works** with the interface selected (Key 61, confirmed 2026-11-23). An earlier report that it did
  nothing was made before the interface's outputs were set up (Multi Out, MPC outputs 3/4).
- **Re-select the interface after every MPC start.** On a Key 61 (3.10.1), MPC usually saves an empty `audioSetup`
  (`audioOutputDeviceName=""`, the built-in device) even while it runs on the interface. But not always: on 2026-11-24 it had saved
  the interface, and MPC then crashed at every start (see the warning at the top). Since 0.2.0, the addin's startup guard resets
  a saved interface to the built-in device before MPC reads its settings, so MPC always starts on its own audio. After a restart or
  power cycle, pick the interface again in **Preferences > Audio**. The guard writes a line to `rate_bridge.log` when it resets the
  setting, and keeps the previous file as `MPC.settings.bak-rate-bridge`.
- **Two "AUDIO DEVICE ERROR: Failed to open Capture Stream" dialogs at every MPC start** (Key 61, 3.10.1.12). This is MPC's own
  bug and happens without the addin and without the interface connected: at startup MPC applies its saved audio setup by opening the
  device again before closing the one it already opened, and the second open fails as "device busy". Close them and carry on.
- **Don't save the interface as MPC's audio device by editing `MPC.settings`.** MPC would then open it at startup while its internal
  device is still running, and crash (`terminate called without an active exception`). The guard (0.2.0) undoes that at the next
  start, but with `guard=0` or 0.1.0, MPC crashes at every restart until the setting is changed back. Picking the interface in
  Preferences after startup is the safe way.
- **Other audio addins.** The addin only changes which device MPC's open returns, and passes every call on, so addins that hook the
  same calls keep working. Addins that watch MPC's main audio stream (for example `mpc-addin-usb-audio`, which forwards it to a
  computer over USB) will see the interface's stream once it is MPC's audio device, instead of the MPC's own.
- **To undo:** `sh /data/mpc-addins/rate-bridge/uninstall.sh -y` over SSH (MPC restarts once), or `enabled=0` in
  `rate_bridge.conf` and restart MPC. If MPC ever fails to start because of an addin, the recovery steps are in `docs/ADDINS.md` of
  open-mpc/mpc-vst-plugins ("When an addin stops MPC from starting").

## Settings

`rate_bridge.conf` in the addin's folder (`/data/mpc-addins/rate-bridge/`). The addin reads it when MPC starts.

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `1` | `0` opens every device as if the addin weren't there |
| `cards` | empty | Empty: any USB card without 44.1 kHz. Otherwise a list of card ids or numbers (from `/proc/asound/cards`) to bridge |
| `log` | `1` | One line per bridged or examined device, and per guard reset, in `rate_bridge.log` next to the settings |
| `guard` | `1` | At MPC's start, reset a saved startup audio device that the addin bridges to MPC's own audio (prevents the startup crash loop). `0` leaves `MPC.settings` alone |
| `settings` | `/data/Settings/MPC/MPC.settings` | The file the guard checks |

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
