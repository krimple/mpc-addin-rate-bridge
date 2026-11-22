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

**Preview.** It is tested offline, and on an MPC Key 61 (Gen1, MPC OS 3.10.1) with a Quad Cortex mini, where MPC's exact
opening sequence was run against the interface with the addin loaded in a test program: 8 channels, both directions, no dropouts.
Choosing the interface in MPC's own Preferences with the addin installed is the next test. Not yet tried on other devices or interfaces.

## How it works

The addin replaces one function, `snd_pcm_open`. MPC opens its audio device as `hw:<card>`. When that card is a USB audio card
whose `/proc/asound/card<N>/stream0` lists no 44100 for the direction being opened, the addin opens alsa-lib's `type rate` PCM over
the same `hw` device at the card's own rate (48 kHz when it has it) instead. The rate plugin converts the sample rate only, so
MPC's own setup (exact 44,100 Hz, as many channels as the device has, 192-frame periods, explicit start) works as it does for any
device. Every other open is passed on with `dlsym(RTLD_NEXT)`, so it works alongside other audio addins.

**Sound:** the converter is alsa-lib's built-in linear one, which works at 16-bit resolution. Recorded side by side with the
interface's native 48 kHz, a listener couldn't tell them apart on an amp-modelled guitar. Measured, it adds faint noise above
10 kHz (about 65-75 dB below the signal). A better converter is a possible later version.

**Latency:** the converted device's buffers come out at 771 (playback) and 768 (capture) frames at 44.1 kHz, about 4 ms more in
each direction than the Key 61's own audio.

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
- `tests/device_check.c`: run MPC's opening sequence against a device on the MPC itself, with the addin preloaded
  (`LD_PRELOAD=.../librate_bridge.so RATE_BRIDGE_DIR=/tmp ./device_check hw:2`), without installing anything.

Installing an addin restarts MPC (`sh install.sh` in the package folder; `sh /data/mpc-addins/rate-bridge/uninstall.sh` removes
it). The installer and the rules addins follow are in [open-mpc/mpc-vst-plugins](https://github.com/open-mpc/mpc-vst-plugins)
(`docs/ADDINS.md`).

## Licence

MIT.
