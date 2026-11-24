#!/bin/sh
# Runs ON the MPC (BusyBox sh), piped over ssh: ssh root@<mpc> 'sh -s' < reset-audio-device.sh
# Resets MPC's saved startup audio device to Internal (empty DEVICESETUP names). Touches nothing else.
# Usage: sh -s [--check]   (--check: report only, change nothing)
set -eu
DIR=/data/Settings/MPC
F=$DIR/MPC.settings
MODE=${1:-fix}

[ -f "$F" ] || { echo "ERROR: $F not found"; exit 1; }
echo "== saved audio device:"
grep -E 'audio(Output|Input)DeviceName=' "$F" || echo "(no DEVICESETUP line)"
echo "== acvs: $(systemctl show acvs -p ActiveState --value) restarts=$(systemctl show acvs -p NRestarts --value)"

if ! grep -E 'audio(Output|Input)DeviceName="[^"]+"' "$F" >/dev/null; then
  echo "OK: already Internal; nothing to do."; exit 0
fi
[ "$MODE" = "--check" ] && { echo "CHECK: a non-Internal device is saved (fix would reset it)."; exit 0; }

echo "== stopping MPC (acvs)"
systemctl stop acvs
TS=$(date +%Y%m%d-%H%M%S)
cp -p "$F" "$F.bak-reset-audio-$TS"
echo "backup: $F.bak-reset-audio-$TS"

sed -E 's/audio(Output|Input)DeviceName="[^"]*"/audio\1DeviceName=""/g' "$F" > "$F.new"
# count changed lines in either diff style (BusyBox prints unified: -/+; GNU default: </>)
CHANGED=$(diff "$F" "$F.new" | grep -v -E '^(---|\+\+\+) ' | grep -c -E '^[-+<>]' || true)
if [ "$CHANGED" -ne 4 ] || grep -E 'audio(Output|Input)DeviceName="[^"]+"' "$F.new" >/dev/null; then
  echo "ERROR: unexpected diff ($CHANGED changed lines); leaving original in place, MPC stopped."
  diff "$F" "$F.new" || true
  rm -f "$F.new"; exit 1
fi
diff "$F" "$F.new" || true
mv "$F.new" "$F"
sync

echo "== starting MPC (acvs)"
systemctl start acvs
R0=$(systemctl show acvs -p NRestarts --value)
sleep 25
R1=$(systemctl show acvs -p NRestarts --value)
echo "== acvs: $(systemctl show acvs -p ActiveState --value) restarts $R0 -> $R1"
[ "$R0" = "$R1" ] && echo "OK: MPC stayed up on Internal." || { echo "WARN: MPC still restarting; check journalctl -u acvs"; exit 2; }
