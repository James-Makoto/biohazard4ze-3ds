#!/usr/bin/env bash
# build the cia on a windows/linux/mac box that has devkitPro installed.
#   ./build.sh            normal build
#   ./build.sh clean      wipe build output first
# looks for devkitPro in $DEVKITPRO, then the usual install spots, then the local
# tools/root pulled by tools/pull.sh. makerom and bannertool come from tools/bin.
set -e
cd "$(dirname "$0")"

find_dkp() {
    for p in "$DEVKITPRO" /opt/devkitpro /c/devkitPro /c/devkitpro \
             /c/re4z-tc/dkp2/opt/devkitpro \
             "$HOME/devkitpro" "$PWD/tools/root/opt/devkitpro"; do
        [ -n "$p" ] && [ -d "$p/devkitARM" ] && { echo "$p"; return 0; }
    done
    return 1
}

if ! DKP=$(find_dkp); then
    echo "devkitPro not found."
    echo "install it from https://devkitpro.org/wiki/Getting_Started (3ds-dev group),"
    echo "then set DEVKITPRO, or run: cd tools && sh pull.sh"
    exit 1
fi
export DEVKITPRO="$DKP"
export DEVKITARM="$DKP/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PWD/tools/bin:$PATH"
echo "devkitPro: $DEVKITPRO"

for t in makerom bannertool; do
    command -v $t >/dev/null 2>&1 || {
        echo "missing $t. drop the binary in tools/bin/ or add it to PATH."
        echo "  makerom:    https://github.com/3DSGuy/Project_CTR/releases"
        echo "  bannertool: https://github.com/carstene1ns/3ds-bannertool/releases"
        exit 1
    }
done

[ "$1" = clean ] && make clean

make -j"$(nproc 2>/dev/null || echo 4)"
echo
ls -l release/re4z.cia 2>/dev/null && echo "done: release/re4z.cia"
