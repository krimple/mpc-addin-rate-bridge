#!/bin/sh
# Offline test in a Linux container (Docker; the host's own architecture): unit tests for the parsers, then the addin preloaded
# into a driver that opens PCMs the way MPC does, against tests/fake_asound (card2 = the Quad Cortex mini's real stream0,
# 48 kHz only; card3 = a USB card with 44.1 kHz; card1 = not USB) with alsa-lib's null PCM standing in for the hardware.
# Prints PASSED or FAILED.
set -eu
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/w -w /w debian:bookworm sh -c '
set -eu
apt-get -qq update >/dev/null && apt-get -qq install -y gcc libc6-dev libasound2-dev >/dev/null 2>&1
B=build/host; mkdir -p $B
CF="-O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer"
gcc $CF -o $B/test_parse tests/test_parse.c src/rate_bridge.c -ldl -lpthread
RATE_BRIDGE_ASOUND=tests/fake_asound RATE_BRIDGE_DIR=$B ./$B/test_parse
gcc -O1 -g -Wall -Wextra -fPIC -shared -fvisibility=hidden -Wl,--no-undefined -o $B/librate_bridge.so src/rate_bridge.c -ldl -lpthread
gcc -O1 -g -Wall -o $B/driver tests/driver.c -lasound
fail=0
run() {   # run <expect-file-suffix> <name> <stream> [conf]
    rm -f $B/rate_bridge.log $B/rate_bridge.conf
    [ -n "${4:-}" ] && printf "%s\n" "$4" > $B/rate_bridge.conf
    RATE_BRIDGE_ASOUND=tests/fake_asound RATE_BRIDGE_DIR=$B RATE_BRIDGE_SLAVE=null LD_PRELOAD=$PWD/$B/librate_bridge.so \
        ./$B/driver "$2" "$3" > $B/out.txt 2>/dev/null || true
}
expect() { grep -q -x -- "$1" $B/out.txt || { echo "FAIL ($T): expected \"$1\" in:"; sed "s/^/    /" $B/out.txt; fail=1; }; }
logged() { grep -q -- "$1" $B/rate_bridge.log 2>/dev/null || { echo "FAIL ($T): log lacks \"$1\""; cat $B/rate_bridge.log 2>/dev/null; fail=1; }; }
nolog()  { [ ! -s $B/rate_bridge.log ] || ! grep -q "^.* bridge " $B/rate_bridge.log || { echo "FAIL ($T): bridged unexpectedly"; cat $B/rate_bridge.log; fail=1; }; }

T="QC playback bridged";  run x "hw:2" playback;  expect "open 0"; expect "rate44100 0"; expect "hw_params 0"; expect "rate 44100"; expect "io 192"; logged "bridge hw:2 playback: card 2 dev 0 runs at 48000"
T="QC capture bridged";   run x "hw:2" capture;   expect "open 0"; expect "rate44100 0"; expect "hw_params 0"; expect "io 192"; logged "bridge hw:2 capture"
T="QC by id";             run x "hw:CARD=mini,DEV=0" playback; expect "rate44100 0"; logged "bridge hw:CARD=mini,DEV=0"
T="44.1k card untouched"; run x "hw:3" playback;  nolog; logged "pass hw:3 playback: card 3 (1234:0001) has 44100"
T="non-USB untouched";    run x "hw:1" playback;  nolog
T="plughw untouched";     run x "plughw:2" playback; nolog
T="disabled";             run x "hw:2" playback "enabled=0"; nolog
T="cards= excludes";      run x "hw:2" playback "cards=other"; nolog; logged "not in cards=other"
T="cards= includes";      run x "hw:2" playback "cards=mini"; expect "rate44100 0"; logged "bridge hw:2 playback"
T="cards= by index";      run x "hw:2" playback "cards=5, 2"; expect "rate44100 0"
T="log=0";                run x "hw:2" playback "log=0"; expect "rate44100 0"; [ ! -e $B/rate_bridge.log ] || { echo "FAIL ($T): log written"; fail=1; }

# without the addin, hw:2 does not exist in the container: the open fails the same way it does with a pass-through
./$B/driver "hw:1" playback > $B/plain.txt 2>/dev/null || true
T="pass-through same as plain"; run x "hw:1" playback; cmp -s $B/plain.txt $B/out.txt || { echo "FAIL ($T)"; diff $B/plain.txt $B/out.txt; fail=1; }

# startup guard: the addin loaded into "MPC" (the driver, via RATE_BRIDGE_EXE) resets a saved bridged device before MPC reads it
QC="Neural DSP Quad Cortex mini at usb-ff500000.usb-1.1, high speed"
mkset() { printf "%s\n" "<?xml version=\"1.0\"?>" "<PROPERTIES>" "  <VALUE name=\"audioSetup\">" \
    "    <DEVICESETUP audioOutputDeviceName=\"$1\"" "                 audioInputDeviceName=\"$1\"/>" "  </VALUE>" \
    "  <VALUE name=\"midi\" val=\"$QC\"/>" "</PROPERTIES>" > $B/MPC.settings; rm -f $B/MPC.settings.bak-rate-bridge; }
grun() {   # grun <exe-name> [conf]
    rm -f $B/rate_bridge.log; printf "settings=$PWD/$B/MPC.settings\n%s\n" "${2:-}" > $B/rate_bridge.conf
    RATE_BRIDGE_EXE=$1 RATE_BRIDGE_ASOUND=tests/fake_asound RATE_BRIDGE_DIR=$B RATE_BRIDGE_SLAVE=null \
        LD_PRELOAD=$PWD/$B/librate_bridge.so ./$B/driver "hw:1" playback > /dev/null 2>&1 || true
}
T="guard resets QC"; mkset "$QC"; cp $B/MPC.settings $B/orig.settings; grun driver
grep -q "audioOutputDeviceName=\"\"" $B/MPC.settings && grep -q "audioInputDeviceName=\"\"" $B/MPC.settings || { echo "FAIL ($T): not reset"; cat $B/MPC.settings; fail=1; }
[ "$(diff $B/orig.settings $B/MPC.settings | grep -c "^[<>]")" = 4 ] || { echo "FAIL ($T): more than the 2 lines changed"; diff $B/orig.settings $B/MPC.settings; fail=1; }
cmp -s $B/orig.settings $B/MPC.settings.bak-rate-bridge || { echo "FAIL ($T): backup differs"; fail=1; }
[ ! -e $B/MPC.settings.new ] || { echo "FAIL ($T): .new left over"; fail=1; }
logged "guard: .* reset to Internal"
T="guard: other process"; mkset "$QC"; cp $B/MPC.settings $B/orig.settings; grun MPC
cmp -s $B/orig.settings $B/MPC.settings || { echo "FAIL ($T): changed in a non-MPC process"; fail=1; }
T="guard: 44.1k card kept"; mkset "Some Interface"
cp $B/MPC.settings $B/orig.settings; grun driver; cmp -s $B/orig.settings $B/MPC.settings || { echo "FAIL ($T)"; fail=1; }
T="guard: Internal kept"; mkset ""; cp $B/MPC.settings $B/orig.settings; grun driver; cmp -s $B/orig.settings $B/MPC.settings || { echo "FAIL ($T)"; fail=1; }
T="guard=0"; mkset "$QC"; cp $B/MPC.settings $B/orig.settings; grun driver "guard=0"; cmp -s $B/orig.settings $B/MPC.settings || { echo "FAIL ($T)"; fail=1; }
T="guard: enabled=0"; mkset "$QC"; cp $B/MPC.settings $B/orig.settings; grun driver "enabled=0"; cmp -s $B/orig.settings $B/MPC.settings || { echo "FAIL ($T)"; fail=1; }
T="guard: cards= excludes"; mkset "$QC"; cp $B/MPC.settings $B/orig.settings; grun driver "cards=other"; cmp -s $B/orig.settings $B/MPC.settings || { echo "FAIL ($T)"; fail=1; }
T="guard: no settings file"; rm -f $B/MPC.settings; grun driver; [ ! -e $B/MPC.settings ] || { echo "FAIL ($T): created a file"; fail=1; }

nm -D --defined-only $B/librate_bridge.so | awk "{print \$3}" | grep -v -x -E "snd_pcm_open|_init|_fini" | grep . && { echo "FAIL: exports more than snd_pcm_open"; fail=1; }
[ $fail = 0 ] && echo PASSED || { echo FAILED; exit 1; }
'
