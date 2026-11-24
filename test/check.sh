#!/bin/sh
# SPDX-License-Identifier: MIT
# Headless checks against the apps in test/root.
#   test/check.sh out/headless/emuzero-launcher out/check
# 1. the app list matches test/expected-apps.txt (which entries show, names, icon or tile)
# 2. each layout draws after a key script; the PNGs are kept in out/check to look at
set -e
bin=$1; out=$2
here=$(cd "$(dirname "$0")" && pwd)
export EMUZERO_ROOT="$here/root"
mkdir -p "$out"
"$bin" --list > "$out/apps.txt"
diff -u "$here/expected-apps.txt" "$out/apps.txt"
echo "check: app list ok ($(wc -l < "$out/apps.txt") apps)"
shot() { name=$1; shift; "$bin" "$@" --shot "$out/$name.png"; test -s "$out/$name.png"; echo "check: $out/$name.png"; }
shot carousel        --layout carousel
shot carousel-right3 --layout carousel --keys right,right,right
shot carousel-wrap   --layout carousel --keys left
shot grid            --layout grid
shot grid-down-right --layout grid --keys down,right
shot grid-page2      --layout grid --keys right,right,right,right,right
shot grid-page2-down --layout grid --keys right,right,right,right,right,down
mkdir -p "$out/empty"
EMUZERO_ROOT="$out/empty" shot empty-grid --layout grid --keys right
