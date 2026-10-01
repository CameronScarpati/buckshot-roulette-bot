#!/usr/bin/env bash
# Narrated sessions piped into the advisor, each held to the exact position it
# must print afterwards: fresh loads, which copy of an item leaves a hand, steals
# in both directions, phone reads nobody else heard, the shells a phone can
# name, medicine that heals nothing, and the dealer's memory part-way through
# its turn.
#
#     tools/check_advisor_session.sh [path-to-build-directory]
#
# A line given as ~text only has to appear somewhere in the output; every other
# line has to appear as a whole line. The first session that falls short prints
# what the advisor said, and the script exits 1 once every session has run.

set -euo pipefail

BUILD=${1:-build}
ADVISOR="$BUILD/advisor"

if [ ! -x "$ADVISOR" ]; then
  echo "no advisor in $BUILD; build first" >&2
  exit 2
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

# session NAME "ARGUMENTS" "INPUT" LINE...
session() {
  local name=$1 arguments=$2 input=$3
  shift 3
  local out="$work/out"
  # The arguments are split on spaces on purpose; none of them holds one.
  # shellcheck disable=SC2086
  printf '%b' "$input" | "$ADVISOR" $arguments > "$out" 2>&1 || true
  local line
  for line in "$@"; do
    if [ "${line:0:1}" = "~" ]; then
      grep -Fq -- "${line:1}" "$out" && continue
    else
      grep -Fxq -- "$line" "$out" && continue
    fi
    echo "FAIL $name: expected the line" >&2
    echo "  $line" >&2
    echo "in:" >&2
    sed 's/^/  | /' "$out" >&2
    failures=$((failures + 1))
    return 0
  done
  echo "ok   $name"
}

# answer NAME "POSITION" "ARGUMENTS" LINE...: one written position, no session.
answer() {
  local name=$1 position=$2 arguments=$3
  shift 3
  local out="$work/out"
  # shellcheck disable=SC2086
  "$ADVISOR" --position "$position" $arguments > "$out" 2>&1 || true
  local line
  for line in "$@"; do
    grep -Fq -- "$line" "$out" && continue
    echo "FAIL $name: expected" >&2
    echo "  $line" >&2
    echo "in:" >&2
    sed 's/^/  | /' "$out" >&2
    failures=$((failures + 1))
    return 0
  done
  echo "ok   $name"
}

PLAIN="--reloads 0"
DEALER="--reloads 0 --opponent dealer"
STORY="--reloads 0 --opponent dealer --mode story2"

# A fresh load: p1 moves first, cuffs go, a sawed barrel stays, and the stale
# item list stays while phone reads go.
session "a load after a shot hands p1 the turn" "$PLAIN" \
  "set p1=2/2 p2=2/2 tube=1L0B turn=p1\nshot p2 live\nload 1L1B\nstate\n" \
  "p1=2/2 p2=1/2 tube=1L1B turn=p1" "p1 is to move."
session "a load clears cuffs and the restraint used this turn" "$PLAIN" \
  "set p1=2/2[cuff,cuff,beer] p2=2/2 tube=1L0B turn=p1\nuse cuff p2\neject live\nload 1L1B\nstate\nadvise\n" \
  "p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1" "~use Handcuffs on p2"
session "a barrel sawed before a Beer emptied the tube stays sawed" "$PLAIN" \
  "set p1=2/2[saw,beer] p2=2/2 tube=1L0B turn=p1\nuse saw\neject live\nload 1L1B\nstate\n" \
  "p1=2/2 p2=2/2 tube=1L1B turn=p1 sawed"
session "a load after the dealer's blank hands p1 the turn" "$PLAIN" \
  "set p1=2/2 p2=2/2 tube=0L1B turn=p2\nshot self blank\nload 1L1B\nstate\n" \
  "p1=2/2 p2=2/2 tube=1L1B turn=p1"
session "a load keeps the stale list and drops phone reads" "$PLAIN" \
  "set p1=2/2[cig] p2=2/2 tube=0L1B turn=p1 listcigs phoned=p2@3\nshot self blank\nload 1L1B\nstate\n" \
  "p1=2/2[cig] p2=2/2 tube=1L1B turn=p1 listcigs"

# Which copy leaves a hand.
session "give appends and take removes the copy named" "$PLAIN" \
  "set p1=2/2[beer,mg] p2=2/2 tube=1L1B turn=p1\ngive p1 beer\nstate\nboard\ntake p1 beer #2\nstate\n" \
  "p1=2/2[beer,mg,beer] p2=2/2 tube=1L1B turn=p1" \
  "~items: Beer #1, Magnifying Glass, Beer #2" \
  "p1=2/2[beer,mg] p2=2/2 tube=1L1B turn=p1"
session "take without a copy removes the first" "$PLAIN" \
  "set p1=2/2[beer,mg,beer] p2=2/2 tube=1L1B turn=p1\ntake p1 beer\nstate\n" \
  "p1=2/2[mg,beer] p2=2/2 tube=1L1B turn=p1"
session "give stops at eight items and takes no copy" "$PLAIN" \
  "set p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1\ngive p1 beer\ngive p1 mg #2\n" \
  "a seat holds at most 8 items" \
  "give adds the item at the end of the hand, so it takes no copy number."
session "a Beer names its copy" "$PLAIN" \
  "set p1=2/2[beer,mg,beer] p2=2/2 tube=1L2B turn=p1\neject blank #2\nstate\n" \
  "p1=2/2[beer,mg] p2=2/2 tube=1L1B turn=p1"
session "an Adrenaline used alone takes nothing" "$PLAIN" \
  "set p1=2/2[adr,adr] p2=2/2 tube=1L1B turn=p1\nuse adr\nstate\n" \
  "p1=2/2[adr] p2=2/2 tube=1L1B turn=p1"
session "a steal names the victim's copy" "$PLAIN" \
  "set p1=1/2[adr] p2=2/2[cig,mg,cig] tube=1L1B turn=p1\nuse adr p2 cig #2\nstate\n" \
  "p1=2/2 p2=2/2[cig,mg] tube=1L1B turn=p1"
session "the dealer steals p1's second glass" "$DEALER" \
  "set p1=2/2[mg,beer,mg] p2=2/2[adr] tube=2L2B turn=p2\nuse adr p1 mg unseen #2\nstate\n" \
  "p1=2/2[mg,beer] p2=2/2 tube=2L2B turn=p2 known=p2:0L dealer=seen"
session "the dealer steals p1's phone" "$DEALER" \
  "set p1=2/2[phone] p2=2/2[adr] tube=2L2B turn=p2\nuse adr p1 phone unseen\nstate\n" \
  "p1=2/2 p2=2/2 tube=2L2B turn=p2 phoned=p2@4"

# Another seat's phone.
session "another seat's phone is recorded unseen" "$PLAIN" \
  "set p1=2/2 p2=2/2[phone,phone] tube=2L3B turn=p2\nphone unseen\nstate\nphone 3 unseen\nphone 3 live\n" \
  "p1=2/2 p2=2/2[phone] tube=2L3B turn=p2 phoned=p2@5" \
  "the game never shows which shell another seat's phone named; type phone unseen"
session "a seat makes at most eight phone reads in one load" "$PLAIN" \
  "set p1=2/2 p2=2/2[phone] tube=2L3B turn=p2 phoned=p2@8,8,8,8,8,8,8,8\nphone unseen\n" \
  "a seat makes at most 8 phone reads in one load"

# The player's phone moves a pick of the eighth shell to the seventh, so with
# eight loaded it names shells 2 to 7; the dealer's names any of 2 to 8
# (BurnerPhone.gd 13-15, DealerIntelligence.gd 187-194).
session "the player's phone never names shell 8 of 8" "$PLAIN" \
  "set p1=2/2[phone] p2=2/2 tube=4L4B turn=p1\nphone 8 live\nstate\nphone 7 live\nstate\n" \
  "This phone never names shell 8 when 8 shells are loaded: it names shells 2 to 7." \
  "p1=2/2[phone] p2=2/2 tube=4L4B turn=p1" \
  "p1=2/2 p2=2/2 tube=4L4B turn=p1 known=p1:6L"
session "the dealer's phone can name shell 8 of 8" "$PLAIN --seat 2" \
  "set p1=2/2 p2=2/2[phone] tube=4L4B turn=p2\nphone 8 live\nstate\n" \
  "p1=2/2 p2=2/2 tube=4L4B turn=p2 known=p2:7L"

# Expired Medicine that works heals nothing at full charges or in the faded
# band, and a failure always costs a charge (MedicineManager.gd 17-38,
# HealthCounter.gd 141-153), so the two outcomes stay apart there too.
session "medicine at full charges" "$PLAIN" \
  "set p1=4/4[med] p2=4/4 tube=1L1B turn=p1\nuse med ok\nstate\nset p1=4/4[med] p2=4/4 tube=1L1B turn=p1\nuse med bad\nstate\n" \
  "p1=4/4 p2=4/4 tube=1L1B turn=p1" \
  "p1=3/4 p2=4/4 tube=1L1B turn=p1"
session "medicine in the faded band" "--reloads 0 --opponent dealer --mode story3" \
  "set p1=1/5[med] p2=4/5 tube=1L1B turn=p1\nuse med ok\nstate\nset p1=1/5[med] p2=4/5 tube=1L1B turn=p1\nuse med bad\nstate\n" \
  "p1=1/5 p2=4/5 tube=1L1B turn=p1" \
  "p1=0/5 p2=4/5 tube=1L1B turn=p1"
session "stolen medicine at full charges" "$PLAIN" \
  "set p1=4/4[adr] p2=3/4[med] tube=1L1B turn=p1\nuse adr p2 med ok\nstate\n" \
  "p1=4/4 p2=3/4 tube=1L1B turn=p1"

# The dealer's memory part-way through its turn.
session "a glass then a Beer on a blank leaves the endless dealer aiming at itself" "$DEALER" \
  "set p1=2/2 p2=2/2[mg,beer] tube=2L2B turn=p2\nmg blank\nstate\neject blank\nstate\n" \
  "p1=2/2 p2=2/2[beer] tube=2L2B turn=p2 known=p2:0B dealer=seen" \
  "p1=2/2 p2=2/2 tube=2L1B turn=p2 dealer=aim:self"
session "the story dealer keeps believing in a blank" "$STORY" \
  "set p1=2/2 p2=2/2[mg,beer] tube=2L2B turn=p2\nmg blank\neject blank\nstate\n" \
  "p1=2/2 p2=2/2 tube=2L1B turn=p2 dealer=believes:B"
session "a Beer on a chamber the counts gave away" "$DEALER" \
  "set p1=2/2 p2=2/2[beer] tube=0L2B turn=p2\neject blank\nstate\n" \
  "p1=2/2 p2=2/2 tube=0L1B turn=p2 dealer=aim:self"
session "a Beer on a chamber the counts did not give away" "$DEALER" \
  "set p1=2/2 p2=2/2[beer] tube=1L2B turn=p2\neject blank\nstate\n" \
  "p1=2/2 p2=2/2 tube=1L1B turn=p2"
session "a Beer after the dealer's own phone reads" "$DEALER" \
  "set p1=2/2 p2=2/2[beer] tube=1L3B turn=p2 phoned=p2@4\neject blank\nstate\n" \
  "The dealer may have worked out that shell from its phone reads; its memory after the Beer is taken as fresh." \
  "p1=2/2 p2=2/2 tube=1L2B turn=p2 phoned=p2@4"
session "the saw coin aims at p1" "$DEALER" \
  "set p1=2/2 p2=2/2[saw,cig] tube=2L2B turn=p2\nuse saw\nstate\nshot p1 blank\nstate\n" \
  "p1=2/2 p2=2/2[cig] tube=2L2B turn=p2 sawed dealer=aim:p1" \
  "p1=2/2 p2=2/2[cig] tube=2L1B turn=p1"
session "a saw on a live chamber the dealer saw keeps what it saw" "$DEALER" \
  "set p1=2/2 p2=2/2[mg,saw] tube=2L2B turn=p2\nmg live\nuse saw\nstate\n" \
  "p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed known=p2:0L dealer=seen"
session "medicine is remembered for the rest of the turn" "$DEALER" \
  "set p1=4/4 p2=2/4[med] tube=2L2B turn=p2\nuse med ok\nstate\n" \
  "p1=4/4 p2=4/4 tube=2L2B turn=p2 dealer=med"
session "the dealer's Inverter turns a blank it saw live" "$DEALER" \
  "set p1=2/2 p2=2/2[mg,inv] tube=2L2B turn=p2\nmg blank\nuse inv\nstate\nshot p1 live\nstate\n" \
  "p1=2/2 p2=2/2 tube=3L1B turn=p2 known=p2:0L dealer=seen" \
  "p1=1/2 p2=2/2 tube=2L1B turn=p1"
session "the dealer's pass writes the stale list" "$DEALER" \
  "set p1=2/3[cig] p2=2/3[adr,mg] tube=2L2B turn=p2\nmg unseen\nstate\nuse adr p1 cig\nstate\n" \
  "p1=2/3[cig] p2=2/3[adr] tube=2L2B turn=p2 known=p2:0L listcigs dealer=seen" \
  "p1=2/3 p2=3/3 tube=2L2B turn=p2 known=p2:0L dealer=seen"

# Written positions.
answer "a written memory is taken as the turn under way" \
  "p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=aim:self" "$DEALER" \
  "Its turn is taken to be under way, with dealer=aim:self."
answer "an inverted chamber is not a sign of the dealer's turn" \
  "p1=2/2 p2=2/2 tube=1L2B turn=p2 inverted" "$DEALER" \
  "Its turn is taken to start here."
answer "a sawed barrel asks for the dealer's memory" \
  "p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed" "$DEALER" \
  "looks like the middle of a dealer turn" "dealer=aim:p1"
answer "a node limit stops the search and says so" \
  "p1=1/1[mg,beer] p2=1/1[beer] tube=2L2B turn=p1" "$PLAIN --node-limit 1" \
  "stopped at the node limit; values may be wrong"
answer "the JSON answer reports the node limit" \
  "p1=1/1[mg,beer] p2=1/1[beer] tube=2L2B turn=p1" "$PLAIN --node-limit 1 --json" \
  '"truncated": true, "budgetReached": false, "nodeLimitHit": true}'

if [ "$failures" -gt 0 ]; then
  echo "$failures session(s) printed something else" >&2
  exit 1
fi
