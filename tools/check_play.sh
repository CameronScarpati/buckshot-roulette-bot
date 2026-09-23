#!/usr/bin/env bash
# The two properties a seeded program owes a reader: the same seed replays the
# same round exactly, and the batch it reports keeps landing in the same place.
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

"$ADVISOR" --position "$POSITION" --reloads 1 > "$work/a1"
"$ADVISOR" --position "$POSITION" --reloads 1 > "$work/a2"
if ! diff -q "$work/a1" "$work/a2" > /dev/null; then
  echo "the same position gave two different answers:" >&2
  diff "$work/a1" "$work/a2" >&2
  exit 1
fi
echo "ok   the same position gives the same answer, byte for byte"

line=$(grep "survived" "$work/first")
survived=$(echo "$line" | sed -E 's/.* survived ([0-9]+) of .*/\1/')
rounds=$(echo "$line" | sed -E 's/.* of ([0-9]+) rounds.*/\1/')
if [ "$rounds" != "$CEILING" ]; then
  echo "expected $CEILING rounds, read: $line" >&2
  exit 1
fi
if [ "$survived" -lt "$FLOOR" ]; then
  echo "seat 1 survived $survived of $rounds, below the floor of $FLOOR: $line" >&2
  echo "a solver that plays this badly against a copy of itself is broken" >&2
  exit 1
fi
echo "ok   seat 1 survived $survived of $rounds, inside the band $FLOOR to $CEILING"
