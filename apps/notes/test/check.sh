#!/bin/sh
# SPDX-License-Identifier: MIT
# emuZero Notes, end to end: the headless build plays key scripts against a notes folder of its
# own, then the files on disk are compared with what the keys should have made. Screens are
# kept as PNGs in OUT to look at.
#   apps/notes/test/check.sh out/headless/emuzero-notes out/check-notes
set -e
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); out=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
export EMUZERO_ROOT="$here/root" NOTES_GUIDE="$here/../guide.md"
pass=0; failn=0
fresh() {   # a new, empty notes folder and home for one case
	case_dir=$(mktemp -d "$out/case.XXXXXX")
	export NOTES_DIR="$case_dir/notes" HOME="$case_dir/home" XDG_STATE_HOME="$case_dir/state"
	mkdir -p "$HOME"
}
run() { "$bin" --script - "$@"; }
expect_file() {   # name expected-content
	if [ ! -f "$NOTES_DIR/$1" ]; then echo "FAIL $case: no file '$1' (have: $(ls "$NOTES_DIR" | tr '\n' ' '))"; failn=$((failn+1)); return; fi
	if [ "$(cat "$NOTES_DIR/$1")" != "$2" ]; then
		echo "FAIL $case: $1 is"; sed 's/^/  | /' "$NOTES_DIR/$1"; echo "  want"; printf '%s\n' "$2" | sed 's/^/  | /'
		failn=$((failn+1)); return
	fi
	pass=$((pass+1))
}
expect_none() { if [ -e "$NOTES_DIR/$1" ]; then echo "FAIL $case: '$1' should not exist"; failn=$((failn+1)); else pass=$((pass+1)); fi; }
expect_count() { n=$(ls "$NOTES_DIR" | grep -c '\.md$' || true); if [ "$n" != "$1" ]; then echo "FAIL $case: $n notes, want $1"; failn=$((failn+1)); else pass=$((pass+1)); fi; }

case=checklist; fresh
run <<K
key ctrl+n
type Groceries\n
type [] oat milk\ncoffee beans, the light roast from the place on Elm\neggs
key ctrl+enter
key enter
key enter
type Remember the bags.
shot $out/edit-checklist.png
wait 1600
key esc
shot $out/view-checklist.png
key up
key up
key space
shot $out/view-ticked.png
key esc
shot $out/list.png
type grocer
shot $out/list-search.png
K
expect_file Groceries.md "Groceries
- [ ] oat milk
- [x] coffee beans, the light roast from the place on Elm
- [x] eggs
Remember the bags."

case=undo; fresh
run <<K
key ctrl+n
type Undo test\none two three
key ctrl+z
key ctrl+z
key ctrl+y
key ctrl+q
K
expect_file "Undo test.md" "Undo test
one two "

case=copy-line; fresh
run <<K
key ctrl+n
type Lines\nfirst\nsecond
key ctrl+c
key up
key ctrl+v
key ctrl+end
key ctrl+x
key ctrl+q
K
expect_file Lines.md "Lines
second
first"

case=select-more; fresh
run <<K
key ctrl+n
type Sel\nhello big world
key ctrl+left
key ctrl+left
key right
key ctrl+space
key ctrl+x
key ctrl+end
key ctrl+v
key ctrl+space
key ctrl+space
shot $out/select-more.png
key esc
key esc
key ctrl+q
K
expect_file Sel.md "Sel
hello  worldbig"

case=move-line; fresh
run <<K
key ctrl+n
type Move\na\nb\nc
key alt+up
key alt+up
key ctrl+q
K
expect_file Move.md "Move
c
a
b"

case=format; fresh
run <<K
key ctrl+n
type Plan
key ctrl+h
key enter
type make it bold
key ctrl+left
key ctrl+shift+right
key ctrl+b
key ctrl+end
key enter
key ctrl+i
type slanted
key ctrl+q
K
expect_file Plan.md "# Plan
make it **bold**
*slanted*"

case=lists; fresh
run <<K
key ctrl+n
type Lists\n- one\ntwo\n\n\n1. first\nsecond\n\n\n> said\nmore\n\n\nplain
key ctrl+q
K
expect_file Lists.md "Lists
- one
- two

1. first
2. second

> said
> more

plain"

case=checklist-keys; fresh
run <<K
key ctrl+n
type Tasks\na\nb\nc
key shift+up
key shift+up
key ctrl+l
key esc
key ctrl+home
key down
key ctrl+enter
key ctrl+q
K
expect_file Tasks.md "Tasks
- [x] a
- [ ] b
- [ ] c"
run <<K
key alt
type m
shot $out/menu-edit.png
key enter
key ctrl+q
K
expect_file Tasks.md "Tasks
- [ ] b
- [ ] c
- [x] a"

case=rename-on-leave; fresh
run <<K
key ctrl+n
type Alpha\nbody
key esc
key esc
K
expect_file Alpha.md "Alpha
body"
run <<K
key enter
key enter
key ctrl+home
key end
key bs
key bs
key bs
key bs
key bs
type Beta
key esc
key esc
K
expect_file Beta.md "Beta
body"
expect_none Alpha.md

case=empty-dropped; fresh
run <<K
key ctrl+n
type x
key bs
key esc
key esc
K
expect_count 0

case=search-new; fresh
run <<K
type Shopping list
key enter
type eggs
key esc
key esc
K
expect_file "Shopping list.md" "Shopping list
eggs"

case=reopen; fresh
run <<K
key ctrl+n
type Reopen me\nline two\nline three
key up
key ctrl+q
K
grep -q '^last=Reopen me.md' "$NOTES_DIR/.notes-state" && pass=$((pass+1)) || { echo "FAIL $case: state has no last note"; failn=$((failn+1)); }
run <<K
type X
key ctrl+q
K
shot_reopen=$out/reopen.png
"$bin" --script - --shot "$shot_reopen" <<K
K
expect_file "Reopen me.md" "Reopen me
line twoX
line three"

case=trash; fresh
run <<K
type Doomed
key enter
key esc
key esc
key del
shot $out/list-deleted.png
key ctrl+z
K
expect_file Doomed.md "Doomed"
run <<K
key del
key alt
type r
shot $out/menu-list.png
key enter
shot $out/trash.png
key enter
K
expect_file Doomed.md "Doomed"

case=pin; fresh
run <<K
type Older
key enter
key esc
key esc
type Newer
key enter
key esc
key esc
key down
key ctrl+p
shot $out/list-pinned.png
K
grep -q '^pin=Older.md' "$NOTES_DIR/.notes-state" && pass=$((pass+1)) || { echo "FAIL $case: not pinned"; failn=$((failn+1)); }

case=external-change; fresh
run <<K
key ctrl+n
type Shared\nmine
wait 1600
sh printf 'Shared\ntheirs\n' > "$NOTES_DIR/Shared.md"; touch -d '+2 seconds' "$NOTES_DIR/Shared.md"
type !
wait 1600
key ctrl+q
K
expect_file Shared.md "Shared
mine!"
expect_file "Shared (other copy).md" "Shared
theirs"

case=plain-file; fresh
mkdir -p "$case_dir/etc"
printf 'key=1\r\nother=2\r\n' > "$case_dir/etc/conf.txt"
"$bin" "$case_dir/etc/conf.txt" --script - --shot "$out/plain-file.png" <<K
key ctrl+end
type new=3
key ctrl+q
K
if [ "$(od -c "$case_dir/etc/conf.txt" | head -3 | tr -s ' ')" = "$(printf 'key=1\r\nother=2\r\nnew=3' | od -c | head -3 | tr -s ' ')" ]; then pass=$((pass+1)); else echo "FAIL $case: CRLF file not kept"; od -c "$case_dir/etc/conf.txt" | head; failn=$((failn+1)); fi

case=find; fresh
run <<K
key ctrl+n
type Find\nalpha beta\ngamma beta\ndelta
key ctrl+f
type beta
key enter
shot $out/find.png
key esc
type X
key ctrl+q
K
expect_file Find.md "Find
alpha beta
gamma X
delta"

case=guide; fresh
run <<K
key ctrl+g
shot $out/guide.png
key pgdn
key pgdn
shot $out/guide-2.png
key e
key alt
shot $out/guide-menu.png
key esc
key esc
shot $out/guide-closed.png
K
expect_count 0

case=help; fresh
run <<K
key help
shot $out/help-list.png
key esc
key ctrl+n
key help
shot $out/help-edit.png
key pgdn
shot $out/help-edit-2.png
K

case=long-text; fresh
long=$(printf 'w%.0s' $(seq 1 300))
run <<K
key ctrl+n
type A very long title that goes on and on past the edge of the row for sure\n
type $long\n
type - [ ] a long checklist item that wraps onto a second line with a hanging indent\n
key ctrl+q
K
expect_file "A very long title that goes on and on past the edge of the.md" "A very long title that goes on and on past the edge of the row for sure
$long
- [ ] a long checklist item that wraps onto a second line with a hanging indent
- [ ] "
run --shot "$out/long-text.png" <<K
K
run <<K
key esc
key esc
type and on past the edge of the row for sure, and further than the box
shot $out/long-search.png
K

case=sizes; fresh
for s in 0 1 2; do
	run --shot "$out/size-$s.png" <<K
key f1
key ctrl+g
key pgdn
K
done
grep -q '^size=1' "$NOTES_DIR/.notes-state" && pass=$((pass+1)) || { echo "FAIL $case: size not kept"; failn=$((failn+1)); }

echo "notes check: $pass passed, $failn failed (screens in $out)"
[ "$failn" = 0 ]
