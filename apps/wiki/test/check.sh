#!/bin/sh
# SPDX-License-Identifier: MIT
# emuZero Wiki, end to end: the headless build plays key scripts against the survival pack and a
# small Wikipedia pack made from test/fixture.py; what each screen showed (WIKI_TRACE) is
# checked, and screens are kept as PNGs in OUT to look at.
#   apps/wiki/test/check.sh out/headless/emuzero-wiki out/headless/survival.wpack out/check-wiki
set -e
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); survival=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); out=$3
here=$(cd "$(dirname "$0")" && pwd)
tools=$here/../tools
mkdir -p "$out"
out=$(cd "$out" && pwd)
export EMUZERO_ROOT="$here/../../../test/root"
pass=0; failn=0

# the packs: survival as built, Wikipedia from the fixture
mkdir -p "$out/packs" "$out/empty" "$out/bad"
python3 "$here/fixture.py" "$out/fixture.db"
python3 "$tools/build-pack.py" simplewiki "$out/fixture.db" "$out/packs/simplewiki.wpack" >/dev/null 2>&1
python3 "$tools/build-pack.py" check "$out/packs/simplewiki.wpack" >/dev/null || { echo "FAIL fixture pack check"; failn=$((failn+1)); }
cp "$survival" "$out/packs/survival.wpack"
cp "$survival" "$out/bad/survival.wpack"
head -c 9000 /dev/urandom > "$out/bad/broken.wpack"
printf 'not a pack' > "$out/bad/text.wpack"

fresh() {   # a home and state of its own for one case
	case_dir=$(mktemp -d "$out/case.XXXXXX")
	export HOME="$case_dir/home" XDG_STATE_HOME="$case_dir/state" WIKI_TRACE="$case_dir/trace" WIKI_DIR="$out/packs"
	mkdir -p "$HOME"
}
run() { "$bin" --script - "$@"; }
last() { grep -aF -- "$1" "$WIKI_TRACE" | tail -1; }
ok() { pass=$((pass+1)); }
bad() { echo "FAIL $case: $1"; failn=$((failn+1)); }
expect() {   # line-prefix text...: the last trace line starting so holds every text
	line=$(last "$1"); shift
	for t in "$@"; do case "$line" in *"$t"*) ;; *) bad "'$t' not in: $line"; return ;; esac; done
	ok
}
expect_order() {   # line-prefix a b: a comes before b in that line
	line=$(last "$1")
	case "$line" in *"$2"*"$3"*) ok ;; *) bad "'$2' should come before '$3' in: $line" ;; esac
}
expect_none() { if grep -aqF -- "$1" "$WIKI_TRACE"; then bad "'$1' should not show"; else ok; fi; }

case=shelves; fresh
run <<K
shot $out/home.png
K
expect "home q=\"\"" ">What is happening? | Survival guides | Army Survival Manual | Wikipedia |"

case=search-ranking; fresh
run <<K
type hypoth
shot $out/search-hypoth.png
K
expect "home q=\"hypoth\"" "#SURVIVAL" ">Hypothermia and Frostbite" "#WIKIPEDIA"
expect_order "home q=\"hypoth\"" "Hypothesis |" "Armenian hypothesis"
expect_order "home q=\"hypoth\"" "Hypothermia |" "Armenian hypothesis"

case=exact-first; fresh
run <<K
type water
K
expect_order "home q=\"water\"" "#WIKIPEDIA | Water |" "Water cycle"

case=links-and-back; fresh
run <<K
type water cycle
key enter
shot $out/read-wiki.png
key /
type rain
key esc
key enter
shot $out/read-link.png
key esc
key esc
shot $out/back-to-search.png
K
grep -aqF 'read "Rain"' "$WIKI_TRACE" && ok || bad "the Rain link was not followed"
grep -aqF 'find "rain" 1 matches' "$WIKI_TRACE" && ok || bad "find did not find rain"
grep -aF 'read "' "$WIKI_TRACE" | tail -1 | grep -aqF 'read "Water cycle"' && ok || bad "Back did not return to Water cycle"
expect "home q=" "q=\"water cycle\""

case=pack-text
python3 - "$out/packs/simplewiki.wpack" <<'P' && ok || bad "Water's pack text"
import sqlite3, sys, zlib
db = sqlite3.connect(sys.argv[1])
t = zlib.decompress(db.execute("SELECT body FROM articles WHERE title='Water'").fetchone()[0]).decode()
assert "References" not in t and "Other websites" not in t, t
assert "](id:" in t and "- Not an article here" in t, t
w = zlib.decompress(db.execute("SELECT body FROM articles WHERE title='World War II'").fetchone()[0]).decode()
assert "\\*Stars\\*" in w and "\\[1\\]" in w, w
assert not db.execute("SELECT 1 FROM articles WHERE title='Empty article'").fetchone()
P

case=inside; fresh
run <<K
type purify
key ctrl+enter
shot $out/inside.png
key enter
K
expect "list list=5" "Purification" "Making Water Safe to Drink" "Water"
case=inside-common; fresh
run <<K
type the
key shift+enter
shot $out/inside-common.png
K
expect "list list=5" "Only very common words"

case=triage; fresh
run <<K
key ctrl+t
shot $out/triage.png
type 1
shot $out/triage-person.png
type 1
shot $out/triage-cpr.png
key esc
K
expect "triage node=root" "5  A child, baby or older adult"
expect "triage node=" "node=person"
grep -aqF 'read "CPR"' "$WIKI_TRACE" && ok || bad "1, 1 did not open CPR"

case=picture; fresh
run <<K
type fire lays
key enter
key x
key x
shot $out/read-picture.png
key enter
shot $out/picture-fit.png
key plus
shot $out/picture-actual.png
key x
key esc
K
expect "picture fire_lays.jpg" "picture fire_lays.jpg"
expect "picture fire_lays.jpg zoom" "zoom=1"
grep -aF 'read "' "$WIKI_TRACE" | tail -1 | grep -aqF 'read "Fire Lays"' && ok || bad "Esc did not return to Fire Lays"

case=contents; fresh
run <<K
type fm 21-76 water
key ctrl+enter
key esc
key esc
type water procurement
key enter
key c
shot $out/contents.png
key down
key enter
shot $out/contents-jump.png
K
expect "read \"" "Water Procurement"
expect "contents -> line" "contents -> line"

case=bookmarks; fresh
run <<K
type hypothesis
key enter
key b
key esc
key esc
shot $out/home-after.png
K
expect "home q=\"\"" "Recently read" "Bookmarks"
grep -aqF "mark	Simple English Wikipedia	Hypothesis" "$XDG_STATE_HOME/emuzero/wiki/state" && ok || bad "bookmark not saved"
run <<K
key ctrl+s
key alt
type b
key enter
shot $out/bookmarks.png
key del
K
expect "list list=4" "No bookmarks yet"

case=long-title; fresh
run <<K
type zyxwv
key enter
shot $out/long-title.png
key b
key ctrl+s
key esc
key alt
type b
key enter
key enter
K
n=$(grep -ac 'read "Zyxwv specimen .*ending here"' "$WIKI_TRACE" || true)
[ "$n" = 2 ] && ok || bad "the long title did not open whole, twice (once from Bookmarks): $n"

case=long-text; fresh
run <<K
type long article
key enter
key n
shot $out/long-end.png
key k
key m
key m
shot $out/long-paged.png
key esc
key esc
type a very long search that goes on and on past the edge of the box, with words like water and ice and rain and more and more and more and more until it is three hundred characters long or close to that, and then it keeps going so that the end is what shows in the box, as Notes does it ok
shot $out/long-query.png
K
expect "read \"" "read \"Long article\""
expect "reader focus" "focus="
expect "home q=" "keeps going"

case=tall-paragraph; fresh
run <<K
type long article
key enter
key x
key x
key x
shot $out/tall-1.png
key x
shot $out/tall-2.png
K
# the lead is one paragraph many screens tall: Down scrolls inside it before moving on
line=$(grep -aF "reader focus" "$WIKI_TRACE" | tail -1)
case "$line" in *"focus=2 top=2."[1-9]*) ok ;; *) bad "Down should scroll through the tall paragraph: $line" ;; esac

case=missing-packs; fresh
WIKI_DIR="$out/empty" run <<K
shot $out/no-packs.png
K
expect "home q=\"\"" "No packs found" "Get Wikipedia"

case=broken-pack; fresh
WIKI_DIR="$out/bad" run <<K
shot $out/broken-pack.png
type fire
K
expect "home q=\"\"" "Could not read: broken.wpack, text.wpack"
expect "home q=\"fire\"" "#SURVIVAL"

case=sizes; fresh
run <<K
type fire lays
key enter
key f1
shot $out/size-large.png
key f1
shot $out/size-small.png
K
grep -q "^size 0" "$XDG_STATE_HOME/emuzero/wiki/state" && ok || bad "text size not saved"

case=about; fresh
run <<K
key alt
type aa
key enter
shot $out/about.png
K
expect "read \"" "read \"About the Wiki\""

for theme in $(ls "$EMUZERO_ROOT/usr/share/emuzero/themes" | grep -v README); do
	case=theme-$theme; fresh
	run --theme "$theme" <<K
shot $out/theme-$theme-home.png
type fire lays
key enter
shot $out/theme-$theme-read.png
K
done

rm -rf "$out"/case.*
echo "emuZero Wiki checks: $pass passed, $failn failed (screens in $out)"
[ "$failn" = 0 ]
