#!/usr/bin/env bash
# What a seeded program owes a reader: the same seed replays the same round
# exactly, against either opponent, and the batch it reports keeps landing in
# the same place. A game against the dealer in double or nothing is also held
# to the loads the game's script draws.
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

# Against the dealer in double or nothing a load is drawn the way the game's
# script draws one, with the live count half the total, so live shells never
# outnumber blanks. Every load of the watched round is checked, and the first
# load of a run of seeded rounds against the dealer, which a closed input
# leaves straight after the deal.
cp "$work/w1" "$work/loads"
for seed in $(seq 1 40); do
  "$PLAY" --opponent dealer --seed "$seed" < /dev/null >> "$work/loads"
done
loads=$(grep -c "^The gun is loaded with" "$work/loads")
heavy=$(grep "^The gun is loaded with" "$work/loads" | awk '$6 > $9' || true)
if [ -n "$heavy" ]; then
  echo "a double or nothing load against the dealer has more live shells than blanks:" >&2
  echo "$heavy" >&2
  exit 1
fi
echo "ok   $loads loads against the dealer, none with more live shells than blanks"

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
