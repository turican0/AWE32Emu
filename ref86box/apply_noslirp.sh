#!/bin/sh
# A fallback for when vcpkg cannot build glib (and therefore libslirp).
#
# Networking is mandatory in 86Box and libslirp pulls in the whole glib on
# Windows. For our purpose - recording register writes to the EMU8000 -
# networking is useless, so net_slirp.c can be replaced by an empty
# implementation. The sound is not touched, and the VM configs have no
# network device, so net_slirp_drv cannot be selected.
#
#   ref86box/apply_noslirp.sh          # replace
#   ref86box/apply_noslirp.sh --revert # restore
#
# Afterwards delete build86box/ and run the build again.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../docs/86box-src/master-full"

if [ "$1" = "--revert" ]; then
    cd "$SRC"
    git checkout src/network/net_slirp.c src/network/CMakeLists.txt
    echo "restored; check 'git diff --stat' in $SRC"
    exit 0
fi

cp "$HERE/noslirp/net_slirp.c" "$SRC/src/network/net_slirp.c"
python "$HERE/noslirp/patch_cmake.py" "$SRC/src/network/CMakeLists.txt"

cd "$SRC"
echo
echo "Changes in 86Box:"
git diff --stat
