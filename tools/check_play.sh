#!/usr/bin/env bash
# What a seeded program owes a reader: the same seed replays the same round
# exactly, against either opponent, and the batch it reports keeps landing in
# the same place. A game against the dealer in double or nothing is also held
# to the loads the game's script draws, a round played against the solver
# tells the player nothing the game keeps from them, and every search that
# stops at the node limit says so.
#
#     tools/check_play.sh [path-to-build-directory]
#
# Both are checked against a fixed seed. The band, rather than an exact count,
# is deliberate: two moves that tie can be ordered differently by a different
# compiler, and that is not a regression. A solver that stopped working would
# fall well below the floor.

set -euo pipefail

BUILD=${1:-build}
PLAY="$BUILD/play"
ADVISOR="$BUILD/advisor"
BATCH=(--selfplay 10 --seed 3 --charges 2 --reloads 0)
FLOOR=7
CEILING=10
POSITION="p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1"
DEALER_BATCH=(--dealer 5 --seed 3 --reloads 0)
WATCHED=(--watch --seed 3 --reloads 0)

if [ ! -x "$PLAY" ] || [ ! -x "$ADVISOR" ]; then
  echo "no binaries in $BUILD; build first" >&2
  exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$PLAY" "${BATCH[@]}" > "$work/first"
"$PLAY" "${BATCH[@]}" > "$work/second"
if ! diff -q "$work/first" "$work/second" > /dev/null; then
  echo "the same seed produced two different rounds:" >&2
  diff "$work/first" "$work/second" >&2
  exit 1
fi
echo "ok   the same seed replays the same batch, byte for byte"

# Against the scripted dealer the generator also draws every coin the dealer
# flips and every shell it reads, so the replay is checked there as well: a
# batch, and a round played out move by move with its narration.
"$PLAY" "${DEALER_BATCH[@]}" > "$work/d1"
"$PLAY" "${DEALER_BATCH[@]}" > "$work/d2"
if ! diff -q "$work/d1" "$work/d2" > /dev/null; then
  echo "the same seed produced two different batches against the dealer:" >&2
  diff "$work/d1" "$work/d2" >&2
  exit 1
fi
"$PLAY" "${WATCHED[@]}" > "$work/w1"
"$PLAY" "${WATCHED[@]}" > "$work/w2"
if ! diff -q "$work/w1" "$work/w2" > /dev/null; then
  echo "the same seed produced two different rounds against the dealer:" >&2
  diff "$work/w1" "$work/w2" >&2
  exit 1
fi
echo "ok   the same seed replays the same rounds against the dealer, byte for byte"

# What the dealer works out about the chamber as its pass begins comes before
# the item it then uses (DealerIntelligence.gd 96-112, then 151-201). A watched
# round prints it at the start of a line, straight before that item, so that
# it reads as the start of the pass and not as the end of the one before.
for seed in $(seq 1 20); do
  "$PLAY" --watch --seed "$seed" --reloads 0 >> "$work/watched"
done
worked='(One shell is left, so the dealer|Every shell left is [a-z]+, so the dealer|Both seats can tell|The dealer heard this shell|Shells only the dealer has heard)'
if ! awk -v worked="$worked" '
  $0 ~ ("^ +" worked) { print "indented: " $0; bad = 1 }
  waiting && /^  / { next }
  waiting {
    if ($0 !~ /^The dealer uses/) { print "followed by: " $0; bad = 1 }
    waiting = 0
  }
  $0 ~ ("^" worked) { waiting = 1; seen++ }
  END {
    if (seen == 0) { print "no watched round has the dealer work out a chamber"; bad = 1 }
    exit bad
  }
' "$work/watched" >&2; then
  echo "a chamber the dealer worked out is not printed before the item it led to" >&2
  exit 1
fi
count=$(grep -cE "^$worked" "$work/watched")
echo "ok   $count chambers the dealer worked out, each printed before the item it led to"

# Against the dealer in double or nothing a load is drawn the way the game's
# script draws one: 2 to 8 shells, with the live count half the total, rounded
# down and at least 1 (RoundManager.gd 148-152). Every load of the watched
# round is checked, and the first load of a run of seeded rounds against the
# dealer, which a closed input leaves straight after the deal.
cp "$work/w1" "$work/loads"
for seed in $(seq 1 40); do
  "$PLAY" --opponent dealer --seed "$seed" < /dev/null >> "$work/loads"
done
loads=$(grep -c "^The gun is loaded with" "$work/loads")
drawn=$(grep "^The gun is loaded with" "$work/loads" | awk '{
  total = $6 + $9
  live = int(total / 2)
  if (live < 1) live = 1
  if (total < 2 || total > 8 || $6 != live) print
}' || true)
if [ -n "$drawn" ]; then
  echo "a double or nothing load against the dealer is not one the game's script draws:" >&2
  echo "$drawn" >&2
  exit 1
fi
echo "ok   $loads loads against the dealer, each one the game's script can draw"

# The game shows what a Burner Phone names and what a Magnifying Glass shows
# only to the seat that used it (BurnerPhone.gd 6-35). The chance a solver
# seat gives itself is worked out from what that seat knows, so it is kept
# back for the rest of a load once p2 has heard a shell on a phone, and until
# the chamber leaves once p2 has looked at it. p1 only ever shoots p2 here, so
# it never sees a shell for itself. The seeds are ones where p2 moves again
# after each.
for seed in 5 6 9 11 12; do
  printf '2\n%.0s' $(seq 1 60) |
    "$PLAY" --seed "$seed" --charges 2 --reloads 0 >> "$work/against"
done
if ! awk '
  /^The gun is loaded with/ { phone = 0; glass = 0 }
  /^(> )?p[0-9] (shoots|uses)/ { actor = ($1 == ">") ? $2 : $1 }
  /^  The shell (was|it racked out was)/ { glass = 0 }
  /^  It listens to the type of one shell/ && actor == "p2" { phone = 1 }
  /^  It looks into the chamber/ && actor == "p2" { glass = 1 }
  /^> p2 / { if (phone) afterPhone++; if (glass) afterGlass++ }
  /^p2 rates its chances/ {
    rated++
    if (phone || glass) { print "after a shell only p2 saw: " $0; bad = 1 }
  }
  END {
    if (afterPhone == 0) { print "p2 never moves after hearing a shell on its phone"; bad = 1 }
    if (afterGlass == 0) { print "p2 never moves after looking at the chamber"; bad = 1 }
    if (rated == 0) { print "p2 never gives its chance"; bad = 1 }
    exit bad
  }
' "$work/against" >&2; then
  echo "a round against the solver gives a chance worked out from a shell only p2 saw" >&2
  exit 1
fi
rated=$(grep -c "^p2 rates its chances" "$work/against")
echo "ok   $rated chances p2 gave itself, none after a shell only it had seen"

# A search that stops at the node limit says so, in a watched weighing, in the
# chance a solver seat gives itself and in a batch.
"$PLAY" --watch --seed 3 --reloads 0 --node-limit 1000 > "$work/limited"
printf '2\n%.0s' $(seq 1 60) |
  "$PLAY" --seed 2 --charges 2 --reloads 0 --node-limit 50 >> "$work/limited"
"$PLAY" --dealer 2 --seed 3 --reloads 0 --node-limit 1000 >> "$work/limited"
for said in "Note: the search stopped at the node limit of 1000 positions" \
  "rates its chances at [0-9.]+, from a search that stopped at the node limit" \
  "^[0-9]+ of the solver's [0-9]+ searches stopped at the node limit of 1000 positions"; do
  if ! grep -qE "$said" "$work/limited"; then
    echo "a search that stopped at the node limit does not say so: no line matches $said" >&2
    exit 1
  fi
done
if grep -q "node limit" "$work/w1"; then
  echo "a watched round says the search stopped at the node limit when it did not" >&2
  exit 1
fi
echo "ok   a search that stops at the node limit says so, and one that does not is silent"

"$ADVISOR" --position "$POSITION" --reloads 1 > "$work/a1"
"$ADVISOR" --position "$POSITION" --reloads 1 > "$work/a2"
if ! diff -q "$work/a1" "$work/a2" > /dev/null; then
  echo "the same position gave two different answers:" >&2
  diff "$work/a1" "$work/a2" >&2
  exit 1
fi
echo "ok   the same position gives the same answer, byte for byte"

# A round that reaches the move cap without a winner is reported on a line of
# its own and left out of the rate, so the rounds the rate is over and the
# capped rounds add up to the batch.
line=$(grep "survived" "$work/first" || true)
if [ -z "$line" ]; then
  echo "no round of the batch finished:" >&2
  cat "$work/first" >&2
  exit 1
fi
survived=$(echo "$line" | sed -E 's/.* survived ([0-9]+) of .*/\1/')
rounds=$(echo "$line" | sed -E 's/.* of ([0-9]+) (finished )?rounds.*/\1/')
capped=$( (grep -E "^[0-9]+ rounds reached the 400 move cap" "$work/first" || true) | sed -E 's/^([0-9]+) .*/\1/')
capped=${capped:-0}
if [ $((rounds + capped)) -ne "$CEILING" ]; then
  echo "expected $CEILING rounds, read: $line, with $capped capped" >&2
  exit 1
fi
if [ "$survived" -lt "$FLOOR" ]; then
  echo "seat 1 survived $survived of $rounds, below the floor of $FLOOR: $line" >&2
  echo "a solver that plays this badly against a copy of itself is broken" >&2
  exit 1
fi
echo "ok   seat 1 survived $survived of $rounds, inside the band $FLOOR to $CEILING"
