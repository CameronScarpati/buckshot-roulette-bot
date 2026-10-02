#!/usr/bin/env python3
"""Differential test: the C++ solver against the independent Python oracle.

Two implementations written from the same rules, sharing no code, have to agree
on every value. Where they do not, one of them is wrong, and the position is
printed so it can be argued about.

    python3 tools/compare_solvers.py --advisor build/advisor
    python3 tools/compare_solvers.py --advisor build/advisor --random 200 --seed 7
    python3 tools/compare_solvers.py --advisor build/advisor --opponent dealer --mode story
    python3 tools/compare_solvers.py --advisor build/advisor --reloads 1 --random 20 --max-items 3

Exit status is non-zero when any position disagrees by more than the tolerance.
A position the advisor stops at its node limit is printed as SKIP and counted
apart; skips do not fail the run.
"""

from __future__ import annotations

import argparse
import json
import random
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ORACLE = REPO / "tools" / "oracle" / "oracle.py"

# Positions chosen to exercise one mechanism each, in the notation both sides
# accept. Keep these small: the oracle is written for clarity, not for speed.
FIXED_POSITIONS = [
    "p1=1/1 p2=1/1 tube=1L1B turn=p1",
    "p1=1/1 p2=1/1 tube=1L0B turn=p1",
    "p1=1/1 p2=2/2 tube=1L0B turn=p1 sawed",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1",
    "p1=1/2 p2=2/2 tube=1L2B turn=p1",
    "p1=2/2[saw] p2=2/2 tube=1L1B turn=p1",
    "p1=1/1[cuff] p2=1/1 tube=1L1B turn=p1",
    "p1=1/1[mg] p2=1/1 tube=1L1B turn=p1",
    "p1=1/2[cig] p2=2/2 tube=1L1B turn=p1",
    "p1=1/2[med] p2=1/1 tube=1L0B turn=p1",
    "p1=1/1[inv] p2=1/1 tube=1L3B turn=p1",
    "p1=2/2[beer] p2=2/2 tube=1L2B turn=p1",
    "p1=2/2[phone] p2=2/2 tube=2L2B turn=p1",
    "p1=2/2[adr] p2=2/2[saw] tube=1L1B turn=p1",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:0L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:1B",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2",
    "p1=1/2 p2=1/2 tube=2L1B turn=p1 sawed",
    "p1=2/3[saw,beer] p2=2/3[cuff] tube=2L2B turn=p1",
    "p1=1/1 p2=1/1 tube=1L1B turn=p1 cuffed=p2",
    # The seat to move cannot see what the other seat has: both solvers have to
    # choose from the mover's own information state rather than from the
    # position as it really is.
    "p1=1/1 p2=1/1 tube=1L1B turn=p2 known=p1:0L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p1:1L",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p1:0B,2L",
    "p1=2/2[saw] p2=2/2 tube=2L2B turn=p2 known=p1:0L",
    # The other way round: p2 has looked at a shell and the advised seat has
    # not. Neither solver may read it, and neither may forget that p2 can, so
    # both have to average over the ways it could have fallen.
    "p1=1/1 p2=1/1 tube=1L1B turn=p1 known=p2:0L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p2:1L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B",
    "p1=2/2[saw] p2=2/2[beer] tube=1L2B turn=p1 known=p2:2L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p1 known=p1:0L known=p2:2B",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:1B,3L",
    # The third story stage's faded band, which only bites under --heal-floor 2:
    # a seat on one charge there has no normal charges left, so healing cannot
    # reach it and any hit is fatal. Under the default floor these are ordinary
    # positions, so both runs are worth having.
    "p1=1/5[cig] p2=2/5 tube=1L1B turn=p1",
    "p1=1/5[med] p2=2/5 tube=1L1B turn=p1",
    "p1=2/5[cig,med] p2=1/5 tube=1L1B turn=p1",
    "p1=1/5[cig] p2=1/5[med] tube=2L1B turn=p2",
    # A shot in the faded band leaves at least one charge, so p2 on one charge
    # survives its own sawed live shell; outside the band a hit is plain.
    "p1=2/5 p2=1/5 tube=1L0B turn=p2 sawed",
    "p1=1/5 p2=1/5 tube=1L0B turn=p2",
    "p1=2/2 p2=1/2 tube=1L0B turn=p2 sawed",
    # A Beer that empties the tube: the reload keeps the barrel sawed. Run with
    # --reloads 1 these search past the reload.
    "p1=1/1[beer] p2=2/2 tube=0L1B turn=p1 sawed",
    "p1=2/2[beer] p2=2/2 tube=0L1B turn=p1 sawed",
    "p1=1/1 p2=1/1 tube=0L1B turn=p1",
    # A phone on the last shell is spent without reading anything.
    "p1=1/1[phone] p2=1/1 tube=1L0B turn=p1",
    # Hands keep the order they were dealt in; the minimising model must give
    # the same values for both orders.
    "p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1",
    "p1=2/4[mg,saw] p2=4/4[cuff,beer] tube=2L3B turn=p1",
    # The minimising model ignores the Dealer's stale item list.
    "p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p1",
    "p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p1 listcigs",
    "p1=1/1[mg,beer] p2=1/1[beer] tube=2L2B turn=p1",
    # A blank p2 fires into itself keeps the barrel sawed.
    "p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed",
    # p2 used a phone at five shells and p1 never learned what it read: the
    # value is the mean over the four offsets it could have read.
    "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 phoned=p2@5",
    "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 known=p2:1B",
    "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 known=p2:2B",
    "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 known=p2:3B",
    "p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 known=p2:4B",
    # Two copies of one item apart in a hand are two moves, and a lone
    # Adrenaline can be spent on its own.
    "p1=2/2[beer,mg,beer] p2=2/2[adr] tube=1L2B turn=p2",
    "p1=2/2[adr] p2=2/2[cig,beer] tube=1L2B turn=p1",
]

# Positions for --opponent dealer: two seats, p1 advised, p2 the scripted
# Dealer. Most have p2 to move at the root, where neither side lists moves and
# the value is the Dealer turn's value. Each is checked in the mode given by
# --mode, so each runs under both of the Dealer's brains across the two modes.
DEALER_POSITIONS = [
    # Hand-checkable values.
    "p1=1/1 p2=1/1 tube=1L1B turn=p2",
    "p1=1/1 p2=1/1 tube=2L1B turn=p2",
    "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p1:1B,2B known=p2:1B,2B",
    "p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1",
    "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:1B",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed",
    # Each scanned item held by the Dealer.
    "p1=2/2 p2=2/2[mg] tube=1L2B turn=p2",
    "p1=2/2 p2=1/2[cig] tube=2L2B turn=p2",
    "p1=2/2 p2=2/4[med] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[beer] tube=1L2B turn=p2",
    "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[saw] tube=2L1B turn=p2",
    "p1=2/2 p2=2/2[mg,saw] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[phone] tube=2L2B turn=p2",
    "p1=2/2 p2=2/2[mg,inv] tube=1L2B turn=p2",
    # Each scanned item taken from p1 with an Adrenaline.
    "p1=2/2[mg] p2=2/2[adr] tube=1L2B turn=p2",
    "p1=2/2[cig] p2=1/2[adr] tube=2L2B turn=p2",
    "p1=2/2[med] p2=2/4[adr] tube=2L2B turn=p2",
    "p1=2/2[beer] p2=2/2[adr] tube=1L2B turn=p2",
    "p1=2/2[cuff] p2=2/2[adr] tube=2L2B turn=p2",
    "p1=2/2[saw] p2=2/2[adr] tube=2L1B turn=p2",
    "p1=2/2[mg,saw] p2=2/2[adr,adr] tube=2L2B turn=p2",
    "p1=2/2[phone] p2=2/2[adr] tube=2L2B turn=p2",
    "p1=2/2[inv] p2=2/2[mg,adr] tube=1L2B turn=p2",
    # Expired Medicine. While the item list the Dealer's previous pass built is
    # empty, its own medicine comes first even with p1's Cigarettes in reach.
    # With p1's Cigarettes on that list the medicine waits: the Cigarettes are
    # stolen, or a stolen glass spends the last Adrenaline and the list still
    # blocks the medicine. It is also refused at one charge and after one use.
    "p1=2/2[cig] p2=3/4[adr,med] tube=2L2B turn=p2",
    "p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2",
    "p1=2/2[cig] p2=3/4[adr,med] tube=2L2B turn=p2 listcigs",
    "p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2 listcigs",
    "p1=2/2 p2=1/4[med] tube=2L2B turn=p2",
    "p1=2/2 p2=2/5[med,med] tube=2L2B turn=p2",
    # Handcuffs refused: p1 already cuffed, p1 skipped at the root, one shell.
    "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p2 cuffed=p1",
    "p1=2/2 p2=2/2[cuff] tube=2L2B turn=p1 cuffed=p1",
    "p1=2/2 p2=2/2[cuff] tube=1L0B turn=p2",
    # Deduction from the chamber seen by both, from the counts, and from the
    # offsets the Dealer has seen, with those facts written for both seats.
    "p1=2/2 p2=2/2 tube=1L1B turn=p2 known=p1:0L known=p2:0L",
    "p1=2/2 p2=2/2 tube=2L0B turn=p2",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p1:1L,2L known=p2:1L,2L",
    "p1=2/2 p2=2/2 tube=1L2B turn=p2 known=p1:0B known=p2:0B",
    # Dealer memory only p2 has, which the root redraws.
    "p1=2/2 p2=2/2[mg] tube=2L2B turn=p2 known=p2:1L",
    "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:0B",
    "p1=2/2 p2=2/2[beer] tube=1L3B turn=p2 known=p2:1B,2L",
    # A cuffed p2 skipped at the root, and p1 to move at the root.
    "p1=2/2 p2=2/2[cuff] tube=2L1B turn=p2 cuffed=p2",
    "p1=2/2[saw] p2=2/2[mg,beer] tube=2L2B turn=p1",
    "p1=1/2[cuff,mg] p2=2/2[cig,saw] tube=1L2B turn=p1",
    "p1=2/2[beer] p2=2/2[mg] tube=2L2B turn=p1 known=p2:0L",
    # Sawed barrel, a pending inversion, and longer item chains.
    "p1=2/2 p2=2/2[mg] tube=2L1B turn=p2 sawed",
    "p1=1/1 p2=1/1 tube=1L1B turn=p2 inverted",
    "p1=2/2 p2=2/2[mg,beer] tube=1L3B turn=p2",
    "p1=2/2 p2=2/2[mg,beer,inv] tube=1L3B turn=p2",
    "p1=2/2 p2=2/2[phone] tube=1L2B turn=p2",
    "p1=2/3[cig,saw] p2=2/3[adr,cuff,phone] tube=2L2B turn=p2",
    # The Dealer's own blank keeps the saw, so it fires the next shell sawed.
    "p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed known=p1:0B known=p2:0B",
    "p1=2/5 p2=1/5 tube=1L0B turn=p2 sawed",
    # The Dealer used a phone at five shells; the value is the mean over the
    # four offsets it could have read.
    "p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 phoned=p2@5",
    "p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 known=p2:1B",
    "p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 known=p2:2B",
    "p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 known=p2:3B",
    "p1=2/2 p2=2/2[beer] tube=2L3B turn=p2 known=p2:4B",
    # The scan walks the hands in the order they were dealt.
    "p1=2/2[beer,mg,beer] p2=2/2[adr] tube=1L2B turn=p2",
    "p1=2/2[adr] p2=2/2[cig,beer] tube=1L2B turn=p1",
    # Which copy goes decides the order a later scan meets: the Dealer spends
    # its own first copy, a steal takes the Dealer's first Adrenaline and p1's
    # first copy, and each of p1's named copy moves leaves its own hand.
    "p1=2/2 p2=2/4[beer,mg,beer] tube=2L3B turn=p2",
    "p1=2/2[beer,cig,beer] p2=2/4[adr,adr] tube=2L2B turn=p2",
    "p1=2/2[cig,beer,cig] p2=1/4[adr] tube=2L3B turn=p1",
    # The stale item list: p1's Cigarettes block the medicine, and a pass with
    # an Adrenaline in hand puts them on the list. In the third the glass
    # pass puts them there and blocks the medicine on the passes after it. In
    # the fourth p1 has smoked them, and the list outlives the turn and a
    # reload.
    "p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p2 listcigs",
    "p1=2/3[cig] p2=2/3[adr,mg] tube=2L2B turn=p2",
    "p1=2/2[cig,cig] p2=2/4[mg,adr,med] tube=2L2B turn=p2",
    "p1=2/2 p2=3/4[med] tube=0L1B turn=p1 listcigs",
    # Shells only the Dealer has seen, more of them than the minimising model
    # averages over. The Dealer acts on every one of them, so p1's chances
    # are those of a Dealer that knows the tube.
    "p1=2/2 p2=2/2 tube=3L3B turn=p2 known=p2:1L,2L,3L,4B,5B",
    "p1=1/2 p2=2/2 tube=3L3B turn=p1 known=p2:1L,2L,3B,4B,5L",
    "p1=3/4[beer,mg] p2=3/4[saw,beer] tube=4L4B turn=p1 known=p2:1L,2L,3B,4B,5L,6B,7L",
    # The Dealer in the middle of its turn.
    "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p2:0L dealer=seen",
    "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p1:0L known=p2:0L dealer=seen",
    "p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p1:0B known=p2:0B dealer=seen",
    "p1=2/2 p2=2/3[med,med] tube=2L2B turn=p2 dealer=med",
]

# Dealer memories that only one of the Dealer's brains can reach, checked only
# under that --mode.
DEALER_MODE_POSITIONS = {
    "story": ["p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 dealer=believes:B"],
    "don": ["p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=aim:self"],
}

ITEMS = ["mg", "beer", "cig", "cuff", "saw", "phone", "adr", "inv", "med"]
DEALER_ITEMS = {
    "don": ["mg", "beer", "cig", "cuff", "saw", "phone", "adr", "inv", "med"],
    "story": ["mg", "beer", "cig", "cuff", "saw"],
}
TUBES = ["1L1B", "1L2B", "2L1B", "2L2B", "1L3B", "3L1B", "1L0B", "0L2B", "2L3B"]
# The advisor's name for each --mode; the oracle takes the name as given.
ADVISOR_MODES = {"don": "don", "story": "story2"}
# Items per seat in a random hand when --max-items is not given.
DEFAULT_MAX_ITEMS = {"solver": 2, "dealer": 3}


def tubes(max_shells: int | None) -> list[str]:
    """TUBES, less any tube holding more than max_shells shells."""
    if max_shells is None:
        return list(TUBES)
    kept = [t for t in TUBES if sum(map(int, re.findall(r"\d+", t))) <= max_shells]
    if not kept:
        raise SystemExit(f"--max-shells {max_shells} leaves no tube to draw from")
    return kept


def random_phoned(rng: random.Random, seat: str, shells: int, lowest: int = 2) -> str:
    """phoned= for one seat: one or two reads, at tube sizes from the current
    size up to 8, largest first. A size under lowest is raised to lowest, which
    leaves the draws, and so the rest of the random list, as they are for a
    lowest of 2."""
    low = max(2, shells)
    sizes = sorted((max(lowest, rng.randint(low, 8)) for _ in range(rng.randint(1, 2))),
                   reverse=True)
    return f"phoned={seat}@" + ",".join(map(str, sizes))


def random_dealer_memory(rng: random.Random, mode: str, sawed: bool,
                         chamber_seen: str | None) -> str:
    """A dealer= memory the parser accepts for the position: a sawed barrel goes
    only with aim:p1, with seen on a live chamber, or with med alone."""
    cores = []
    if chamber_seen is not None and (not sawed or chamber_seen == "L"):
        cores.append("seen")
    if sawed:
        cores.append("aim:p1")
    else:
        cores.append("believes:B" if mode == "story" else "aim:self")
    core = rng.choice(cores + [None])
    if core is None:
        return "dealer=med"
    return f"dealer={core},med" if rng.random() < 0.5 else f"dealer={core}"


def random_positions(count: int, seed: int, max_items: int | None = None,
                     max_shells: int | None = None, seat: int = 1) -> list[str]:
    rng = random.Random(seed)
    top = DEFAULT_MAX_ITEMS["solver"] if max_items is None else max_items
    pool = tubes(max_shells)
    other = "p2" if seat == 1 else "p1"
    out = []
    while len(out) < count:
        tube = rng.choice(pool)
        live = int(tube.split("L")[0])
        blank = int(tube.split("L")[1][:-1])
        if live + blank == 0:
            continue
        maxhp = rng.randint(1, 3)
        hp1 = rng.randint(1, maxhp)
        hp2 = rng.randint(1, maxhp)
        held = []
        for _ in range(2):
            # A hand is a sequence: the order items are drawn in is the order
            # they were dealt.
            items = [rng.choice(ITEMS) for _ in range(rng.randint(0, top))]
            held.append("[" + ",".join(items) + "]" if items else "")
        turn = rng.choice(["p1", "p2"])
        extras = []
        if rng.random() < 0.15:
            extras.append("sawed")
        if rng.random() < 0.20 and live + blank >= 2:
            offset = rng.randrange(live + blank)
            kind = "L" if (live > 0 and rng.random() < live / (live + blank)) else "B"
            # Half the time the shell belongs to the seat that is not being
            # advised, which is the case the two solvers have to average over.
            watcher = "p1" if rng.random() < 0.5 else "p2"
            extras.append(f"known={watcher}:{offset}{kind}")
        if rng.random() < 0.10:
            extras.append("cuffed=" + ("p2" if turn == "p1" else "p1"))
        # The seat not being advised used a phone and the advised seat never
        # learned what it read. p2 sits where the Dealer does, and the Dealer
        # phones only with more than two shells in the tube
        # (DealerIntelligence.gd 187).
        if rng.random() < 0.20:
            extras.append(random_phoned(rng, other, live + blank, 3 if other == "p2" else 2))
        out.append(
            f"p1={hp1}/{maxhp}{held[0]} p2={hp2}/{maxhp}{held[1]} "
            f"tube={tube} turn={turn} {' '.join(extras)}".rstrip()
        )
    return out


def random_dealer_positions(count: int, seed: int, mode: str, max_items: int | None = None,
                            max_shells: int | None = None) -> list[str]:
    """Two-seat positions for the Dealer opponent. Shell facts are read off one
    hidden shuffle of the tube, so facts written for p1 and for p2 agree."""
    rng = random.Random(seed)
    pool = DEALER_ITEMS[mode]
    top = DEFAULT_MAX_ITEMS["dealer"] if max_items is None else max_items
    shell_pool = tubes(max_shells)
    out = []
    while len(out) < count:
        tube = rng.choice(shell_pool)
        live = int(tube.split("L")[0])
        blank = int(tube.split("L")[1][:-1])
        if live + blank == 0:
            continue
        shells = ["L"] * live + ["B"] * blank
        rng.shuffle(shells)
        maxhp = rng.randint(1, 3)
        hp1 = rng.randint(1, maxhp)
        hp2 = rng.randint(1, maxhp)
        held = []
        for _ in range(2):
            items = [rng.choice(pool) for _ in range(rng.randint(0, top))]
            held.append("[" + ",".join(items) + "]" if items else "")
        turn = rng.choice(["p1", "p2"])
        extras = []
        if rng.random() < 0.15:
            extras.append("sawed")
        # Dealer memory, which the root redraws unless p1 has seen the same
        # shell, and p1's own facts, some of them shared with the Dealer.
        if rng.random() < 0.35:
            seen = sorted(rng.sample(range(len(shells)), rng.randint(1, min(2, len(shells)))))
            extras.append("known=p2:" + ",".join(f"{i}{shells[i]}" for i in seen))
            if rng.random() < 0.5:
                both = [i for i in seen if rng.random() < 0.5] or seen[:1]
                extras.append("known=p1:" + ",".join(f"{i}{shells[i]}" for i in both))
        elif rng.random() < 0.20:
            i = rng.randrange(len(shells))
            extras.append(f"known=p1:{i}{shells[i]}")
        roll = rng.random()
        if roll < 0.10:
            extras.append("cuffed=" + ("p2" if turn == "p1" else "p1"))
        elif roll < 0.15:
            extras.append("cuffed=" + turn)
        # The stale item list holds p1's Cigarettes. Only the Double or
        # Nothing pool has the Adrenaline that puts them there, and p1 may
        # have smoked them since.
        if mode == "don" and rng.random() < (0.25 if "cig" in held[0] else 0.10):
            extras.append("listcigs")
        # The Dealer phones only with more than two shells in the tube
        # (DealerIntelligence.gd 187).
        if rng.random() < 0.20:
            extras.append(random_phoned(rng, "p2", live + blank, 3))
        # The Dealer in the middle of its turn, with a memory it can have.
        if turn == "p2" and "cuffed=p2" not in extras and rng.random() < 0.25:
            chamber = None
            for fact in extras:
                if fact.startswith("known=p2:"):
                    for entry in fact.split(":", 1)[1].split(","):
                        if entry[:-1] == "0":
                            chamber = entry[-1]
            extras.append(random_dealer_memory(rng, mode, "sawed" in extras, chamber))
        out.append(
            f"p1={hp1}/{maxhp}{held[0]} p2={hp2}/{maxhp}{held[1]} "
            f"tube={tube} turn={turn} {' '.join(extras)}".rstrip()
        )
    return out


def model_flags(mode: str, opponent: str, advisor: bool) -> list[str]:
    """--mode and --opponent for one solver. A flag at its default is left out,
    so the default run sends each solver the same argv as before either flag
    existed."""
    flags = []
    if mode != "don":
        flags += ["--mode", ADVISOR_MODES[mode] if advisor else mode]
    if opponent != "solver":
        flags += ["--opponent", opponent]
    return flags


def count_flags(items_per_load: int | None, node_limit: int | None = None) -> list[str]:
    """--items-per-load and --node-limit, each left out when not given."""
    flags = []
    if items_per_load is not None:
        flags += ["--items-per-load", str(items_per_load)]
    if node_limit is not None:
        flags += ["--node-limit", str(node_limit)]
    return flags


def run_advisor(binary: str, position: str, seat: int, reloads: int,
                heal_floor: int = 1, mode: str = "don", opponent: str = "solver",
                items_per_load: int | None = None, node_limit: int | None = None) -> dict:
    result = subprocess.run(
        [binary, "--position", position, "--seat", str(seat), "--reloads", str(reloads),
         "--heal-floor", str(heal_floor), "--json"] + model_flags(mode, opponent, True)
        + count_flags(items_per_load, node_limit),
        capture_output=True,
        text=True,
        timeout=600,
    )
    if result.returncode != 0:
        raise RuntimeError(f"advisor failed on {position}: {result.stderr.strip()}")
    return json.loads(result.stdout)


def run_oracle(position: str, seat: int, reloads: int, heal_floor: int = 1,
               mode: str = "don", opponent: str = "solver",
               items_per_load: int | None = None) -> dict:
    result = subprocess.run(
        [sys.executable, str(ORACLE), "--position", position, "--seat", str(seat),
         "--reloads", str(reloads), "--heal-floor", str(heal_floor)]
        + model_flags(mode, opponent, False) + count_flags(items_per_load),
        capture_output=True,
        text=True,
        timeout=900,
    )
    if result.returncode != 0:
        # A position refused at solve time still prints its JSON answer, with
        # refused set; anything else is a failure of the oracle itself.
        try:
            answer = json.loads(result.stdout)
        except json.JSONDecodeError:
            answer = {}
        if answer.get("refused"):
            return answer
        raise RuntimeError(f"oracle failed on {position}: {result.stderr.strip()}")
    return json.loads(result.stdout)


# The C++ advisor names the victim of a stolen Handcuffs or Jammer ("steal
# Handcuffs from p2 and use it on p3"); the oracle gives one text per steal,
# "steal Handcuffs from p2 and use it", valued at the best target for the seat
# to move. Both styles are documented, so the advisor's rows are folded into
# the oracle's form before the two lists are matched.
STEAL_TARGET = re.compile(r" on p\d+$")


def fold_steal_targets(actions: list[dict], seat: int) -> dict[str, float]:
    """Advisor rows keyed by the oracle's action text.

    The seat to move never reads "shoot p<its own number>" (it reads "shoot
    self"), so the advised seat is to move exactly when no row shoots it. Its
    folded steals keep the largest value; another mover's keep the smallest.
    """
    labels = {entry["action"] for entry in actions}
    pick = min if f"shoot p{seat}" in labels else max
    folded: dict[str, float] = {}
    for entry in actions:
        label = entry["action"]
        if label.startswith("steal "):
            label = STEAL_TARGET.sub("", label)
        if label in folded:
            folded[label] = pick(folded[label], entry["value"])
        else:
            folded[label] = entry["value"]
    return folded


def compare(position: str, seat: int, reloads: int, binary: str, tolerance: float,
            heal_floor: int = 1, mode: str = "don", opponent: str = "solver",
            items_per_load: int | None = None,
            node_limit: int | None = None) -> list[str] | None:
    """The disagreements on one position, or None when the advisor stopped at
    its node limit and there is nothing to compare."""
    problems = []
    mine = run_advisor(binary, position, seat, reloads, heal_floor, mode, opponent,
                       items_per_load, node_limit)
    if mine.get("nodeLimitHit"):
        return None
    theirs = run_oracle(position, seat, reloads, heal_floor, mode, opponent, items_per_load)

    # A position one solver refuses has to be refused by the other as well.
    if mine.get("refused") or theirs.get("refused"):
        if not (mine.get("refused") and theirs.get("refused")):
            who = "the C++ solver" if mine.get("refused") else "the oracle"
            problems.append(f"only {who} refuses the position")
        return problems

    if abs(mine["value"] - theirs["value"]) > tolerance:
        problems.append(
            f"value {mine['value']:.6f} against {theirs['value']:.6f}"
        )

    mine_actions = fold_steal_targets(mine["actions"], seat)
    their_actions = {entry["action"]: entry["value"] for entry in theirs["actions"]}
    only_mine = sorted(set(mine_actions) - set(their_actions))
    only_theirs = sorted(set(their_actions) - set(mine_actions))
    if only_mine:
        problems.append("moves only the C++ solver offers: " + ", ".join(only_mine))
    if only_theirs:
        problems.append("moves only the oracle offers: " + ", ".join(only_theirs))
    for action in sorted(set(mine_actions) & set(their_actions)):
        gap = abs(mine_actions[action] - their_actions[action])
        if gap > tolerance:
            problems.append(
                f"{action}: {mine_actions[action]:.6f} against "
                f"{their_actions[action]:.6f}, apart by {gap:.6f}"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--advisor", default=str(REPO / "build" / "advisor"))
    parser.add_argument("--reloads", type=int, default=0)
    parser.add_argument("--tolerance", type=float, default=1e-9)
    parser.add_argument("--random", type=int, default=0, help="extra random positions")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--seat", type=int, default=1)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--heal-floor", type=int, default=1,
                        help="charges below which healing does nothing, for the "
                             "third story stage's faded band (default 1)")
    parser.add_argument("--opponent", choices=("solver", "optimal", "dealer"),
                        default="solver",
                        help="opponent model passed to both solvers: solver "
                             "(default), the minimising opponent, which the older "
                             "name optimal also selects; or dealer, the scripted "
                             "Dealer as p2, which checks DEALER_POSITIONS and needs "
                             "--seat 1")
    parser.add_argument("--mode", choices=("don", "story"), default="don",
                        help="don (default) or story; story is sent to the advisor "
                             "as story2 and to the oracle as story")
    parser.add_argument("--items-per-load", type=int, default=None,
                        help="items dealt to each seat at a reload, passed to both "
                             "solvers (default: each solver's own for the mode)")
    parser.add_argument("--node-limit", type=int, default=None,
                        help="node limit passed to the advisor only; a position it "
                             "stops at is printed as SKIP and counted apart")
    parser.add_argument("--max-items", type=int, default=None,
                        help="most items in a random hand (default 2, or 3 under "
                             "--opponent dealer)")
    parser.add_argument("--max-shells", type=int, default=None,
                        help="leave out random tubes holding more shells than this")
    parser.add_argument("--jobs", type=int, default=1,
                        help="positions checked at once (default 1); the report "
                             "keeps the order of the positions either way")
    args = parser.parse_args()
    if args.opponent == "optimal":
        args.opponent = "solver"
    if args.opponent == "dealer" and args.seat != 1:
        parser.error("--opponent dealer advises p1 only: use --seat 1")

    if not Path(args.advisor).exists():
        print(f"no advisor at {args.advisor}; build it first", file=sys.stderr)
        return 2
    if not ORACLE.exists():
        print(f"no oracle at {ORACLE}", file=sys.stderr)
        return 2

    if args.opponent == "dealer":
        positions = list(DEALER_POSITIONS) + DEALER_MODE_POSITIONS[args.mode]
        if args.random:
            positions += random_dealer_positions(args.random, args.seed, args.mode,
                                                 args.max_items, args.max_shells)
    else:
        positions = list(FIXED_POSITIONS)
        if args.random:
            positions += random_positions(args.random, args.seed, args.max_items,
                                          args.max_shells, args.seat)

    def check(position: str):
        try:
            return compare(position, args.seat, args.reloads, args.advisor,
                           args.tolerance, args.heal_floor, args.mode, args.opponent,
                           args.items_per_load, args.node_limit)
        except Exception as error:  # noqa: BLE001 - report and keep going
            return error

    failures = 0
    skipped = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for position, problems in zip(positions, pool.map(check, positions)):
            if isinstance(problems, Exception):
                print(f"FAIL {position}\n     {problems}", flush=True)
                failures += 1
            elif problems is None:
                skipped += 1
                print(f"SKIP {position}", flush=True)
            elif problems:
                failures += 1
                print(f"FAIL {position}")
                for problem in problems:
                    print(f"     {problem}")
                sys.stdout.flush()
            elif not args.quiet:
                print(f"ok   {position}", flush=True)

    agree = len(positions) - failures - skipped
    print(f"\n{agree} of {len(positions)} positions agree, {skipped} skipped")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
