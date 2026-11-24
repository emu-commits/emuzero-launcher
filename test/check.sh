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
# 6. themes: the ones in themes/ (via test/root/usr/share/emuzero/themes), a user theme made
#    here with the chrome the others do not use and pictures of its own, and the Theme page
#    (keep, cancel, saved file). emuZero's icons/ are linked in as /usr/share/emuzero/icons.
set -e
bin=$1; out=$2
here=$(cd "$(dirname "$0")" && pwd)
export EMUZERO_ROOT="$here/root"
export PATH="$here/bin:$PATH"
mkdir -p "$out"
export HOME="$out/home"; unset XDG_CONFIG_HOME XDG_DATA_HOME   # settings and user themes of the test only
rm -rf "$HOME"; mkdir -p "$HOME"
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

# 6. themes
"$bin" --themes > "$out/themes.txt"
printf 'minimal\tMinimal\npaper\tPaper\n' | diff -u - "$out/themes.txt"
echo "check: themes listed (minimal, paper)"
shot paper-grid      --theme paper
shot paper-carousel  --theme paper --layout carousel --keys right
u="$HOME/.local/share/emuzero/themes/testbar"; mkdir -p "$u"
cp "$here/root/usr/share/APPLaunch/share/images/camera.png" "$u/wall.png"
mkdir -p "$u/icons"; cp "$here/../icons/nz-snake.png" "$u/icons/snake.png"; cp "$here/../icons/nano.png" "$u/icons/nz-options.png"
cat > "$u/theme.conf" <<'EOF2'
name = Test bar
[colors]
background = #203040
[fonts]
regular = DejaVuSansMono.ttf
[home]
wallpaper = wall.png
card_radius = 0
[bar]
position = bottom
badges = sunken
title = TEST
[window]
show = yes
bevel = raised
title = Raised
title_height = 12
background = #c0c0c0
title_background = #000080
title_color = #ffffff
nonsense = 1
EOF2
"$bin" --theme testbar --shot "$out/testbar.png" 2>"$out/testbar.err" >/dev/null
grep -q "unknown key window.nonsense" "$out/testbar.err" || { echo "check: an unknown key was not reported"; exit 1; }
"$bin" --theme testbar --list > "$out/testbar-apps.txt"
grep -qx "snake	Snake	theme" "$out/testbar-apps.txt" && grep -qx "nz-options	Options	theme" "$out/testbar-apps.txt" \
	|| { echo "check: the theme's own icons were not used"; grep -E "^(snake|nz-options)	" "$out/testbar-apps.txt"; exit 1; }
shot testbar-carousel --theme testbar
shot testbar-grid     --theme testbar --layout grid --keys right
shot testbar-info     --theme testbar --keys right,right,i
"$bin" --theme nosuch --shot "$out/nosuch.png" 2>&1 >/dev/null | grep -q "theme nosuch not found" || { echo "check: a missing theme was not reported"; exit 1; }
echo "check: user theme (bottom bar, bevel, wallpaper, mono font, own icons) and bad keys reported"

# the Theme page: T, Right = next theme (paper), Enter keeps it; Esc puts the old one back
rm -f "$HOME/.config/emuzero/settings"
shot themepage       --keys t
shot themepage-paper --keys t,right
"$bin" --keys t,right,enter > /dev/null
grep -qx "theme=paper" "$HOME/.config/emuzero/settings" || { echo "check: Theme page did not save paper"; cat "$HOME/.config/emuzero/settings"; exit 1; }
shot saved-paper
"$bin" --keys t,down,right,right,esc > /dev/null
grep -qx "layout=" "$HOME/.config/emuzero/settings" || { echo "check: Esc on the Theme page changed the layout"; exit 1; }
"$bin" --keys t,down,right,enter > /dev/null
grep -qx "layout=carousel" "$HOME/.config/emuzero/settings" || { echo "check: Theme page did not save the layout"; exit 1; }
rm -f "$HOME/.config/emuzero/settings"
echo "check: Theme page keeps, cancels and saves"
echo "check: all passed"
