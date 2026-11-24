#!/bin/sh
# SPDX-License-Identifier: MIT
# Headless checks against the apps in test/root.
#   test/check.sh out/headless/emuzero-launcher out/check
# 1. the app list matches test/expected-apps.txt (which entries show, names, icon or tile)
# 2. each layout draws after a key script; the PNGs are kept in out/check to look at
# 3. the status bar in other states (charging, low, Wi-Fi not connected or off, no USB)
# 4. Enter and O start the right command lines (the headless build prints them)
# 5. App info: Back is selected first, Remove asks with "No" selected, Update and Remove
#    call nz-admin; dpkg-query, dpkg and sudo are the stand-ins in test/bin
set -e
bin=$1; out=$2
here=$(cd "$(dirname "$0")" && pwd)
export EMUZERO_ROOT="$here/root"
export PATH="$here/bin:$PATH"
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

# 3. status bar states, each from a small root of its own
state() {   # name capacity status wlan-flags operstate
	r="$out/state-$1"; rm -rf "$r"
	mkdir -p "$r/sys/class/power_supply/bq27220-0" "$r/sys/class/net/wlan0"
	ln -s "$here/root/usr" "$r/usr"
	echo Battery > "$r/sys/class/power_supply/bq27220-0/type"
	echo "$2" > "$r/sys/class/power_supply/bq27220-0/capacity"
	echo "$3" > "$r/sys/class/power_supply/bq27220-0/status"
	echo "$4" > "$r/sys/class/net/wlan0/flags"
	echo "$5" > "$r/sys/class/net/wlan0/operstate"
	EMUZERO_ROOT="$r" shot "status-$1"
}
state charging 45 Charging 0x1003 dormant
state low 9 Discharging 0x1002 down

# 4. what Enter and O start
runs() {   # expected-line keys...
	want=$1; shift
	got=$("$bin" --keys "$1" 2>/dev/null | sed -n 's/^launch: //p')
	[ "$got" = "$want" ] || { echo "check: keys $1 started '$got', expected '$want'"; exit 1; }
	echo "check: keys $1 start $want"
}
runs "/usr/bin/camera" right,right,enter
runs "vi (terminal)" right,right,right,right,right,enter          # Exec=vi %F: field code dropped
runs "nz-options (terminal)" o

# 5. App info
export EMUZERO_FAKE_LOG="$out/admin.log"
admin() {   # name expected-nz-admin-call-or-empty keys
	: > "$EMUZERO_FAKE_LOG"
	"$bin" --keys "$3" --shot "$out/$1.png" > "$out/$1.out" 2>/dev/null
	got=$(cat "$EMUZERO_FAKE_LOG")
	[ "$got" = "$2" ] || { echo "check: $1: nz-admin got '$got', expected '$2'"; exit 1; }
	echo "check: $out/$1.png${2:+ ($2)}"
}
admin info-camera "" right,right,i
admin info-back "" right,right,i,enter                              # Back first: closes, nothing runs
grep -q . "$out/info-back.out" && { echo "check: info-back started something"; exit 1; }
admin info-confirm "" right,right,i,right,right,enter
admin info-keep "" right,right,i,right,right,enter,enter            # "No, keep it" first
admin info-removed "-n /usr/sbin/nz-admin store remove camera" right,right,i,right,right,enter,right,enter
admin info-updated "-n /usr/sbin/nz-admin store install camera" right,right,i,right,enter
admin info-files-confirm "" right,right,right,right,right,right,i,right,enter   # names Keyboard guide too
admin info-own "" right,right,right,right,right,right,right,right,right,right,right,i                 # Options: held, no Remove
admin info-own-enter "" right,right,right,right,right,right,right,right,right,right,right,i,right,right,enter
admin info-local "" right,right,right,right,right,i                # /usr/local entry: no package
echo "check: all passed"
