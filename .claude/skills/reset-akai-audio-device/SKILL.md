---
name: reset-akai-audio-device
description: Reset an MPC OS device's saved startup audio device back to Internal, to break the crash loop MPC gets into when a USB audio interface (e.g. via an audio-device addin such as rate-bridge) is saved in MPC.settings. Stops and restarts MPC.
disable-model-invocation: true
---

# Reset the MPC's saved audio device

## Why

MPC OS (seen on 3.10.1, MPC Key 61, 2026-11-23 and 2026-11-24, device) crashes at every start when `MPC.settings`
names a USB audio interface as the startup device. At launch it opens Internal, then opens the saved device without
closing Internal first, and aborts. Signs:

- The pads light cyan during launch, go black, and the cycle repeats.
- `systemctl show acvs -p NRestarts` keeps going up.
- `journalctl -u acvs` shows `**** Audio ... 8-in; 8-out` and then `terminate called without an active exception`,
  followed by `code=dumped, status=6/ABRT`.

This is a stock MPC bug, not an addin bug. **rate-bridge 0.2.0 and later prevent it**: the addin's startup guard resets a
saved bridged interface before MPC reads its settings (`README.md`, "Things to know"). So this skill is for a device on
0.1.0, one with `guard=0`, or an interface the guard doesn't cover (one rate-bridge doesn't bridge). After the fix, suggest
updating to the latest release.

The fix is to blank `audioOutputDeviceName` / `audioInputDeviceName` in
`<DEVICESETUP>`, which means Internal. Addins and every other setting are left alone. After the fix, the user picks the
interface again in Preferences.

## Steps

1. **Find the device.** Its DHCP address changes. Try `$MPC_HOST` if set. Otherwise resolve the mDNS name
   (`ping -c1 <hostname>.local`; the Key 61 answers as `mpc-key-61.local`) or ask the user. ssh from macOS needs the
   sandbox disabled.
   - If ssh says `Host key verification failed` after an address change, run `ssh-keyscan <new-ip>` and compare the key
     with the known one (`ssh-keygen -F <old-ip>`). If it matches, connect with `-o HostKeyAlias=<old-ip>`. Never
     accept a key that doesn't match.
2. **Check first (read-only):**
   `ssh root@<host> 'sh -s' -- --check < .claude/skills/reset-akai-audio-device/reset-audio-device.sh`
   It prints the saved device and the acvs restart count. If it says `already Internal`, the crash loop has another
   cause: read `journalctl -u acvs -b` and the addin drop-in in `/etc/systemd/system/acvs.service.d/`. Also check which
   rate-bridge is installed (`grep VERSION /data/mpc-addins/rate-bridge/addin.manifest`) and whether `rate_bridge.log`
   has a `guard:` line.
3. **Confirm with the user:** the fix stops and restarts MPC.
4. **Fix:** `ssh root@<host> 'sh -s' < .claude/skills/reset-akai-audio-device/reset-audio-device.sh`.
   The script:
   - stops `acvs`;
   - backs up the settings file as `MPC.settings.bak-reset-audio-<timestamp>`;
   - writes `MPC.settings.new` and checks that exactly the two device-name lines changed;
   - moves the new file into place with `mv`, starts `acvs`, and waits 25 s to check that the restart count stays the same.
   If the diff isn't the expected one, it leaves the original in place, leaves MPC stopped, and prints the diff.
5. **Report** the before/after device and the restart counts. Remind the user to select the interface again in
   Preferences → Audio, and that MPC will crash the same way if it saves that choice again before a restart.

## Notes

- The settings live at `/data/Settings/MPC/MPC.settings`. `/media/az01-internal/Settings/MPC` is the same partition
  under a second mount path, so it's the same file.
- The device runs BusyBox: `head -n N` (not `head -N`), and `diff` prints the unified format.
- Don't edit `MPC.settings` while MPC is running. MPC rewrites it on exit.
