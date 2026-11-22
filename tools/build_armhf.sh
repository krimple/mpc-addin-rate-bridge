#!/bin/sh
# Build the addin for Gen1 MPCs (armhf, glibc 2.31 toolchain) into build/package: librate_bridge.so, addin.manifest,
# rate_bridge.conf, and (when MPC_VST points at a checkout of open-mpc/mpc-vst-plugins) the shared addin installer, so the folder
# can be copied to a device and installed with `sh install.sh`. Needs Docker.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/package
docker run --rm --platform linux/arm/v7 -v "$PWD":/w -w /w arm32v7/gcc:11-bullseye \
    gcc -O2 -Wall -Wextra -fPIC -shared -fvisibility=hidden -Wl,--no-undefined -o build/package/librate_bridge.so src/rate_bridge.c -ldl -lpthread
cp addin.manifest rate_bridge.conf build/package/
if [ -n "${MPC_VST:-}" ]; then
    cp "$MPC_VST/tools/release/addin/install.sh" "$MPC_VST/tools/release/addin/uninstall.sh" "$MPC_VST/tools/release/addin/addin-lib.sh" build/package/
    echo "built build/package/ with the installer from $MPC_VST"
else
    echo "built build/package/ (set MPC_VST to a mpc-vst-plugins checkout to add the installer)"
fi
