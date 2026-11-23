#!/bin/sh
# Build the addin for Gen2 MPCs (aarch64) into build/aarch64, the same layout as build_armhf.sh (manifest, settings and, with
# MPC_VST set, the shared installer). Needs Docker (QEMU for arm64 on an x86 host). Not yet tested on a Gen2 device.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/aarch64
docker run --rm --platform linux/arm64 -v "$PWD":/w -w /w arm64v8/gcc:12-bookworm \
    gcc -O2 -Wall -Wextra -fPIC -shared -fvisibility=hidden -Wl,--no-undefined -o build/aarch64/librate_bridge.so src/rate_bridge.c -ldl -lpthread
cp addin.manifest rate_bridge.conf build/aarch64/
if [ -n "${MPC_VST:-}" ]; then
    cp "$MPC_VST/tools/release/addin/install.sh" "$MPC_VST/tools/release/addin/uninstall.sh" "$MPC_VST/tools/release/addin/addin-lib.sh" build/aarch64/
fi
echo "built build/aarch64/"
