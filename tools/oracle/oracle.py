#!/usr/bin/env python3
"""Independent exact solver for Buckshot Roulette (differential-testing oracle).

This file is derived from a written rules specification only.  It is NOT a port
of the C++ engine/solver in this repository and deliberately shares no code with
it, so that a disagreement between the two is evidence that one of them is
wrong.  See README.md in this directory.

Value computed: the probability that the nominated seat is the last seat alive
in THIS round.  Under the default opponent model every other seat plays to
minimise that probability.  Under `--opponent dealer` (two seats, p1 solved)
p2 plays the game's scripted Dealer instead; see the Dealer section below.

Python 3.11, standard library only.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import itertools
import json
import re
import sys
from collections import Counter
from typing import NamedTuple

# --------------------------------------------------------------------------
# Items
# --------------------------------------------------------------------------

MG, BEER, CIG, CUFF, SAW, PHONE, ADR, INV, MED, JAM, REM = range(11)

ITEM_NAMES = (
    "Magnifying Glass", "Beer", "Cigarettes", "Handcuffs", "Hand Saw",
    "Burner Phone", "Adrenaline", "Inverter", "Expired Medicine",
    "Jammer", "Remote",
)
ITEM_TOKENS = {
    "mg": MG, "beer": BEER, "cig": CIG, "cuff": CUFF, "saw": SAW,
    "phone": PHONE, "adr": ADR, "inv": INV, "med": MED, "jam": JAM, "rem": REM,
}
TOKEN_OF = {v: k for k, v in ITEM_TOKENS.items()}

# Item pool for "double or nothing" (default), for story mode and for
# multiplayer.
POOL_DON = (MG, BEER, CIG, CUFF, SAW, PHONE, ADR, INV, MED)
POOL_STORY = (MG, BEER, CIG, CUFF, SAW)
POOL_MP = (MG, BEER, CIG, SAW, PHONE, ADR, INV, MED, JAM, REM)
POOLS = {"don": POOL_DON, "story": POOL_STORY, "mp": POOL_MP}

# Double or Nothing deals 2 to 5 items per load; a solved reload deals the
# middle of that range rounded up, 4.  Story mode and multiplayer deal 2.
# --items-per-load overrides the count for every mode.
ITEMS_PER_LOAD = {"don": 4, "story": 2, "mp": 2}
TABLE_LIMIT = 8
# A seat makes at most this many phone reads in one load.
MAX_READS = 8
# Double or Nothing loads: total ~ U{2..8}, live = max(1, total // 2), so seven
# compositions, each 1/7.  Story and multiplayer use the general distribution
# (total ~ U{2..8}, then live ~ U{1..total-1}).
LOADS_DON = ((1, 1), (1, 2), (2, 2), (2, 3), (3, 3), (3, 4), (4, 4))
# Healing does nothing to a seat holding fewer charges than this. One means it
# always works on a living seat. The third story stage gives four normal charges
# and two faded ones, and a seat past its last normal charge cannot be healed and
# dies to any hit, which is five charges with a floor of two. Mirrors
# RuleConfig::healFloor; set from --heal-floor.  With a floor above one a shot
# that hits a seat at or above the floor leaves it at least one charge.
HEAL_FLOOR = 1
# Deterministic deal: seat i (0-based, so p1 is seat 0) takes items starting
# at pool index i + DEAL_INDEX_BASE, cycling through the pool, appended to its
# hand in that order.
DEAL_INDEX_BASE = 0
# How many shells another seat has looked at the root averages over before it
# gives up and forgets them all.
KNOWLEDGE_LIMIT = 4

EPS = 1e-9

# --------------------------------------------------------------------------
# State
# --------------------------------------------------------------------------


class Seat(NamedTuple):
    hp: int
    max_hp: int
    items: tuple[int, ...]   # in the order the seat received them, oldest first
    cuffed: bool
    skipped: bool            # was just skipped; cannot be cuffed again yet


class State(NamedTuple):
    seats: tuple[Seat, ...]
    # one entry per tube position, 0 == chamber:
    #   (resolved type 'L'/'B' or None, tuple of seat indices that have seen it)
    slots: tuple[tuple[str | None, tuple[int, ...]], ...]
    live: int                # live shells still in the tube (resolved or not)
    blank: int               # blank shells still in the tube
    inverted: bool           # chamber fires as the complement of its shell
    sawed: bool
    turn: int                # 0-based seat index to move
    direction: int           # +1 == cw, -1 == ccw
    cuffs_used: bool         # a pair of cuffs was applied this turn
    budget: int              # remaining reload recursions
    listcigs: bool           # the Dealer's stale item list holds p1's Cigarettes


def _fast_replace(cls):
    """A drop-in `_replace` for a NamedTuple, about twice as fast as the
    standard one; the search calls it millions of times."""
    index = {f: i for i, f in enumerate(cls._fields)}

    def _replace(self, **changes):
        fields = list(self)
        for name, value in changes.items():
            fields[index[name]] = value
        return tuple.__new__(cls, fields)
    return _replace


Seat._replace = _fast_replace(Seat)
State._replace = _fast_replace(State)


def flip(t: str) -> str:
    return "B" if t == "L" else "L"


def canon(items: tuple[int, ...]) -> tuple[int, ...]:
    return tuple(sorted(items))


def canonical(st: State) -> State:
    """Hand order and the stale-list bit are read only by the Dealer's script,
    so every other opponent model sorts the hands and clears the bit."""
    if not st.listcigs and all(s.items == canon(s.items) for s in st.seats):
        return st
    seats = tuple(s._replace(items=canon(s.items)) for s in st.seats)
    return st._replace(seats=seats, listcigs=False)


def drop_item(items: tuple[int, ...], it: int, copy: int = 0) -> tuple[int, ...]:
    """Remove the copy-th (0-based) occurrence of item `it`."""
    where = [i for i, x in enumerate(items) if x == it]
    k = where[copy]
    return items[:k] + items[k + 1:]


def add_item(items: tuple[int, ...], it: int) -> tuple[int, ...]:
    return items + (it,)


def runs(items: tuple[int, ...]) -> list[tuple[int, int, bool]]:
    """One entry per maximal run of equal adjacent items, as (item, copy,
    named): `copy` is how many copies of the item come before the run, and
    `named` says the item has more than one run in this hand.  Removing any
    copy in a run leaves the same hand, so one move per run is exact.  Sorted
    by item index, then copy."""
    starts = [(it, items[:i].count(it)) for i, it in enumerate(items)
              if i == 0 or items[i - 1] != it]
    per_item = Counter(it for it, _ in starts)
    return sorted((it, c, per_item[it] > 1) for it, c in starts)


def item_label(it: int, copy: int, named: bool) -> str:
    return ITEM_NAMES[it] + (f" #{copy + 1}" if named else "")


def use_text(it: int, copy: int, named: bool, tgt: int | None) -> str:
    text = f"use {item_label(it, copy, named)}"
    return text if tgt is None else f"{text} on p{tgt + 1}"


def steal_text(it: int, copy: int, named: bool, victim: int) -> str:
    return f"steal {item_label(it, copy, named)} from p{victim + 1} and use it"


COPY_RE = re.compile(r" #\d+")


def group_key(text: str) -> str:
    """The move with its copy number taken out: copies of one item are one
    choice to a seat that cannot tell what separates them."""
    return COPY_RE.sub("", text)


# --------------------------------------------------------------------------
# The Dealer opponent
#
# Under `--opponent dealer` seat 1 (p2) is the scripted Dealer of the single
# player modes and seat 0 (p1) is the solved seat.  The rules follow
# DealerIntelligence.gd of the third-party decompilation of v2.2.0 hotfix 6,
# https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd
# (line numbers below are in that file unless another file is named).
# --------------------------------------------------------------------------

PLAYER = 0
DEALER = 1

# Items the Dealer's scan never picks.
DEALER_SKIPS = (ADR, JAM, REM)

# Double or Nothing plays the ENDLESS brain, story mode the STORY brain.
BRAINS = {"don": "endless", "story": "story"}


class Memory(NamedTuple):
    """The Dealer's memory within one turn (lines 65-77).  All of it is
    discarded after the shot, so a turn starts from FRESH unless a written
    position says the Dealer is in the middle of one (`dealer=`)."""
    knows: bool              # dealerKnowsShell
    known: str | None        # knownShell: 'L', 'B' or None
    target: int | None       # dealerTarget as a seat index (DEALER is itself)
    used_medicine: bool      # usingMedicine


FRESH = Memory(knows=False, known=None, target=None, used_medicine=False)


def aim(ty: str) -> int:
    """Where the Dealer shoots a shell it knows: live at p1, blank at itself."""
    return PLAYER if ty == "L" else DEALER


def hidden_from(st: State, seat: int) -> bool:
    """True when a shell is resolved but this seat has not seen it."""
    return any(t is not None and seat not in seen for t, seen in st.slots)


def blind_to(st: State, seat: int) -> State:
    """The position as this seat sees it: what it has not seen goes back into
    the unresolved pool, where it is exchangeable again."""
    slots = tuple((t, seen) if (t is not None and seat in seen) else (None, ())
                  for t, seen in st.slots)
    return st._replace(slots=slots)


def foreign_known(st: State, seat: int) -> list[int]:
    """Positions somebody else has resolved and this seat has not seen.

    These are the shells an opponent has looked at.  This seat cannot read
    them, and pretending they were never read is a different error from
    reading them."""
    return [i for i, (t, seen) in enumerate(st.slots)
            if t is not None and seat not in seen and seen]


def seats_in(mask: int) -> tuple[int, ...]:
    return tuple(i for i in range(8) if mask >> i & 1)


def knowledge_branches(st: State, seat: int, limit: int = KNOWLEDGE_LIMIT,
                       extra: dict[int, int] | None = None, keep_chamber: bool = False):
    """The position as `seat` sees it, split into the ways the shells another
    seat has looked at could have fallen.

    Each such position goes back into the unresolved pool as far as this seat
    is concerned, so the weights are draws without replacement from that pool;
    inside a branch the position is pinned again and still carries the seats
    that saw it, so they go on playing as though they know.

    `extra` maps an offset to a mask of further seats that looked at it with a
    phone whose reading this seat never saw; those offsets are drawn too, and an
    offset this seat already knows just gains the seats.  With `keep_chamber`
    the chamber is always drawn and is not counted against the limit.  Returns
    the branches, how many counted shells there were, and whether the limit
    dropped them."""
    extra = {j: m for j, m in (extra or {}).items() if m}
    blind = blind_to(st, seat)
    slots = list(blind.slots)
    for j, m in extra.items():
        t, seen = slots[j]
        if t is not None:      # this seat knows it: the readers are added
            slots[j] = (t, tuple(sorted(set(seen) | set(seats_in(m)))))
    blind = blind._replace(slots=tuple(slots))
    spots = sorted(set(foreign_known(st, seat))
                   | {j for j in extra if seat not in st.slots[j][1]})
    counted = [j for j in spots if not (keep_chamber and j == 0)]
    dropped = len(counted) > limit
    if dropped:
        spots = [0] if (keep_chamber and 0 in spots) else []
    if not spots:
        return [(1.0, blind)], len(counted), dropped
    ul, ub = unresolved_counts(blind)
    out = []
    for combo in itertools.product("LB", repeat=len(spots)):
        p, live, blank = 1.0, ul, ub
        for ty in combo:
            left = live + blank
            if left <= 0:
                p = 0.0
                break
            if ty == "L":
                p, live = p * live / left, live - 1
            else:
                p, blank = p * blank / left, blank - 1
        if p <= 0.0:
            continue
        slots = list(blind.slots)
        for idx, ty in zip(spots, combo):
            seers = {w for w in st.slots[idx][1] if w != seat}
            seers |= set(seats_in(extra.get(idx, 0)))
            slots[idx] = (ty, tuple(sorted(seers)))
        out.append((p, blind._replace(slots=tuple(slots))))
    return (out or [(1.0, blind)]), len(counted), dropped


def two_seat_dealer_phone(st: State, seat: int, mode: str) -> bool:
    """True when this seat's phone follows the Dealer's rule (uniform over the
    later shells, DealerIntelligence.gd 187-194) rather than the player's
    (BurnerPhone.gd 13-15, where a reading of offset 7 shows offset 6)."""
    return len(st.seats) == 2 and mode != "mp" and seat == DEALER


def phone_weights(n: int, uniform: bool) -> dict[int, float]:
    """Offset -> probability for a phone used on a tube of n shells."""
    if n <= 1:
        return {}
    w = {k: 1.0 / (n - 1) for k in range(1, n)}
    if not uniform and 7 in w:
        w[6] += w.pop(7)
    return w


def read_masks(st: State, reads, seat: int, mode: str = "don") -> list[tuple[int, float]]:
    """Distribution of the set of current offsets that `seat`'s unseen phone
    reads name, as [(bit mask of offsets, probability)] in ascending mask
    order.  `reads` holds (seat, size at use) pairs; those of other seats are
    ignored.  A read whose shell has already left the tube names nothing."""
    s = len(st.slots)
    uniform = two_seat_dealer_phone(st, seat, mode)
    dist = {0: 1.0}
    for who, n in reads:
        if who != seat:
            continue
        gone = n - s
        step: dict[int, float] = {}
        for k, w in phone_weights(n, uniform).items():
            j = k - gone
            bit = (1 << j) if j >= 0 else 0
            step[bit] = step.get(bit, 0.0) + w
        new: dict[int, float] = {}
        for mask, p in dist.items():
            for bit, q in step.items():
                new[mask | bit] = new.get(mask | bit, 0.0) + p * q
        dist = new
    return sorted((m, p) for m, p in dist.items() if p > 0.0)


def expand_reads(st: State, reads, mode: str = "don") -> list[tuple[float, dict[int, int]]]:
    """Every combination of the reading seats' masks: [(weight, {offset: mask
    of seats whose read names it})], seats ascending."""
    out: list[tuple[float, dict[int, int]]] = [(1.0, {})]
    for who in sorted({w for w, _ in reads}):
        nxt = []
        for p, extra in out:
            for mask, q in read_masks(st, reads, who, mode):
                e = dict(extra)
                for j in range(len(st.slots)):
                    if mask >> j & 1:
                        e[j] = e.get(j, 0) | (1 << who)
                nxt.append((p * q, e))
        out = nxt
    return out


def healed(seat: "Seat", amount: int) -> int:
    """Healing below the floor does nothing at all: the faded band."""
    if seat.hp < HEAL_FLOOR:
        return seat.hp
    return min(seat.max_hp, seat.hp + amount)


def shot_damage(seat: "Seat", dmg: int) -> int:
    """Charges left after a shot hits.  With a heal floor above one, a seat at
    or above the floor keeps at least one charge (the third story stage cuts
    its wire instead); below the floor any hit can kill.  A failed dose of
    Expired Medicine is plain subtraction and does not come through here."""
    if HEAL_FLOOR > 1 and seat.hp >= HEAL_FLOOR:
        return max(1, seat.hp - dmg)
    return max(0, seat.hp - dmg)


def unresolved_counts(st: State) -> tuple[int, int]:
    rl = sum(1 for t, _ in st.slots if t == "L")
    rb = sum(1 for t, _ in st.slots if t == "B")
    return st.live - rl, st.blank - rb


def resolve(st: State, idx: int, viewer: int | None = None):
    """Branch on the type of position idx, marking it seen by `viewer`.

    Returns [(probability, state, drawn type)].  Unresolved positions are
    exchangeable, so an unresolved position is live with probability
    (unresolved live)/(unresolved total).
    """
    t, seen = st.slots[idx]
    if t is not None:
        cand = [(1.0, t)]
    else:
        ul, ub = unresolved_counts(st)
        ut = ul + ub
        cand = []
        if ul:
            cand.append((ul / ut, "L"))
        if ub:
            cand.append((ub / ut, "B"))
    out = []
    for p, ty in cand:
        s2 = seen if (viewer is None or viewer in seen) else tuple(sorted(seen + (viewer,)))
        slots = st.slots[:idx] + ((ty, s2),) + st.slots[idx + 1:]
        out.append((p, st._replace(slots=slots), ty))
    return out


def pop_chamber(st: State, drawn: str) -> State:
    """The chamber's shell leaves the tube.  The DRAWN type is what leaves."""
    return st._replace(
        slots=st.slots[1:],
        live=st.live - (1 if drawn == "L" else 0),
        blank=st.blank - (1 if drawn == "B" else 0),
        inverted=False,
    )


def chamber_live_prob(st: State) -> float:
    """Probability the chamber FIRES live (inversion included)."""
    tot = 0.0
    for p, s1, ty in resolve(st, 0):
        if (flip(ty) if s1.inverted else ty) == "L":
            tot += p
    return tot


def advance(st: State) -> State:
    """Pass the turn: skip the dead, and skip (and uncuff) the cuffed."""
    seats = st.seats
    changed = None
    i = st.turn
    n = len(seats)
    for _ in range(2 * n + 2):
        i = (i + st.direction) % n
        s = seats[i] if changed is None else changed[i]
        if s.hp <= 0:
            continue
        if s.cuffed:
            if changed is None:
                changed = list(seats)
            changed[i] = s._replace(cuffed=False, skipped=True)
            continue
        if s.skipped:
            if changed is None:
                changed = list(seats)
            changed[i] = s._replace(skipped=False)
        return st._replace(seats=seats if changed is None else tuple(changed),
                           turn=i, cuffs_used=False)
    return st._replace(seats=seats if changed is None else tuple(changed),
                       cuffs_used=False)


def skip_cuffed_mover(st: State) -> State:
    """Root skips: while the seat to move is cuffed, it loses the cuffs, is
    marked skipped and the turn passes on, exactly as a skip during play."""
    while sum(1 for s in st.seats if s.hp > 0) >= 2 and st.seats[st.turn].cuffed:
        st = with_seat(st, st.turn, cuffed=False, skipped=True)
        st = advance(st)
    return st


def finish(st: State, pass_turn: bool) -> State:
    if pass_turn or st.seats[st.turn].hp <= 0:
        return advance(st)
    return st


def with_seat(st: State, i: int, **kw) -> State:
    seats = list(st.seats)
    seats[i] = seats[i]._replace(**kw)
    return st._replace(seats=tuple(seats))


def cuffable(st: State, actor: int) -> list[int]:
    return [i for i, s in enumerate(st.seats)
            if i != actor and s.hp > 0 and not s.cuffed and not s.skipped]


def round_over(st: State) -> bool:
    return sum(1 for s in st.seats if s.hp > 0) <= 1


# --------------------------------------------------------------------------
# Solver
# --------------------------------------------------------------------------


class Solver:
    def __init__(self, seat: int, mode: str = "don", opponent: str = "optimal",
                 items_per_load: int | None = None,
                 saw_survives: bool | None = None) -> None:
        self.seat = seat                 # 0-based nominated seat
        self.mode = mode
        self.mp = mode == "mp"
        self.pool = POOLS[mode]
        self.per_load = ITEMS_PER_LOAD[mode] if items_per_load is None else items_per_load
        # A sawed barrel survives a reload in the single-player modes.
        self.saw_survives = (not self.mp) if saw_survives is None else saw_survives
        # The Dealer's seat firing a blank into itself keeps the saw.
        self.blank_keeps_saw = not self.mp
        if mode == "don":
            self.loads = [((live, blank), 1.0 / len(LOADS_DON)) for live, blank in LOADS_DON]
        else:
            self.loads = [((live, total - live), (1.0 / 7.0) * (1.0 / (total - 1)))
                          for total in range(2, 9) for live in range(1, total)]
        self.dealer = opponent == "dealer"
        if self.dealer and (seat != PLAYER or mode not in BRAINS):
            raise ValueError("the Dealer opponent needs seat p1 and mode don or story")
        self.brain = BRAINS.get(mode, "endless")
        self.memo: dict[State, float] = {}
        self.dealer_memo: dict[tuple[State, Memory], float] = {}
        self.nodes = 0

    # ---- core -----------------------------------------------------------

    def value(self, st: State) -> float:
        # Only the Dealer's script reads hand order and the stale-list bit, so
        # every other model works on the sorted form.  Reload states are
        # memoised like any other.
        if not self.dealer:
            st = canonical(st)
        v = self.memo.get(st)
        if v is not None:
            return v
        self.nodes += 1
        v = self.compute(st)
        self.memo[st] = v
        return v

    def compute(self, st: State) -> float:
        if st.seats[self.seat].hp <= 0:
            return 0.0
        alive = [i for i, s in enumerate(st.seats) if s.hp > 0]
        if len(alive) == 1:
            return 1.0                    # the survivor is the solved seat
        if not st.slots:
            if st.budget <= 0:
                total = sum(s.hp for s in st.seats)
                return st.seats[self.seat].hp / total if total else 0.0
            return sum(p * self.value(s) for p, s in self.reload_branches(st))
        if self.dealer and st.turn == DEALER:
            return self.dealer_value(st, FRESH)
        vals = self.node_values(st)
        mine = st.turn == self.seat
        pick = max if mine else min
        if not hidden_from(st, st.turn):
            return pick(vals.values())
        # The seat to move cannot see a shell somebody else has resolved, so it
        # has to choose from its own information state and its choice is then
        # played out in the position as it really is. Moves it cannot tell apart
        # are assumed equally likely, since nothing it knows separates them.
        # This holds for the solved seat too: once the answer averages over a
        # shell somebody else looked at, reading it would be advice that the
        # seat being advised cannot follow.
        #
        # Copies of one item are one choice: each group is valued at its best
        # copy (as the mover sees it) and, when tied, plays that copy, the
        # first in move order among equals.
        seen = blind_to(st, st.turn)
        theirs = self.node_values(seen)
        if not theirs:
            return pick(vals.values())
        groups: dict[str, tuple[float, str]] = {}
        for text, v in theirs.items():
            key = group_key(text)
            if key not in groups:
                groups[key] = (v, text)
            else:
                best = groups[key][0]
                if (v > best + 1e-12) if mine else (v < best - 1e-12):
                    groups[key] = (v, text)
        edge = pick(v for v, _ in groups.values())
        tied = [text for v, text in groups.values() if abs(v - edge) <= 1e-12]
        real = [vals[text] for text in tied if text in vals]
        return sum(real) / len(real) if real else pick(vals.values())

    def node_values(self, st: State) -> dict[str, float]:
        """Value of every legal action text (targets of a steal are folded in),
        in move order."""
        mine = st.turn == self.seat
        best: dict[str, float] = {}
        for text, dist in self.gen_moves(st):
            v = sum(p * self.value(s) for p, s in dist)
            if text in best:
                best[text] = max(best[text], v) if mine else min(best[text], v)
            else:
                best[text] = v
        return best

    # ---- reload ---------------------------------------------------------

    def reload_branches(self, st: State):
        """A fresh load: shells by the mode's distribution; items dealt in pool
        order and appended; cuffs and skips off; p1 to move; a sawed barrel
        kept in the single-player modes; the stale-list bit untouched."""
        out = []
        for (live, blank), p in self.loads:
            total = live + blank
            seats = []
            for i, s in enumerate(st.seats):
                items = s.items
                if s.hp > 0:
                    start = (i + DEAL_INDEX_BASE) % len(self.pool)
                    for k in range(self.per_load):
                        if len(items) >= TABLE_LIMIT:
                            break
                        items = add_item(items, self.pool[(start + k) % len(self.pool)])
                seats.append(s._replace(items=items, cuffed=False, skipped=False))
            st2 = State(
                seats=tuple(seats),
                slots=((None, ()),) * total,
                live=live,
                blank=blank,
                inverted=False,
                sawed=st.sawed and self.saw_survives,
                turn=0,
                direction=st.direction,
                cuffs_used=False,
                budget=st.budget - 1,
                listcigs=st.listcigs,
            )
            if st2.seats[0].hp <= 0:            # seat 1 acts first by default
                st2 = advance(st2._replace(turn=0))
            out.append((p, st2))
        return out

    # ---- moves ----------------------------------------------------------

    def gen_moves(self, st: State):
        """[(text, [(probability, state)])] in move order: shots (self, then
        the others ascending), own uses (item, copy, victim), the bare
        Adrenaline, then steals (victim, item, copy, target)."""
        a = st.turn
        me = st.seats[a]
        moves = []
        moves.append(("shoot self", self.shoot(st, a)))
        for tgt in range(len(st.seats)):
            if tgt == a or st.seats[tgt].hp <= 0:
                continue
            moves.append((f"shoot p{tgt + 1}", self.shoot(st, tgt)))
        for it, copy, named in runs(me.items):
            if it == ADR or not self.allowed(st, a, it):
                continue
            base = with_seat(st, a, items=drop_item(me.items, it, copy))
            for tgt in self.targets(base, a, it):
                moves.append((use_text(it, copy, named, tgt), self.use(base, a, it, tgt)))
        if ADR in me.items:
            # Every Adrenaline the mover spends is its first one.
            spent = with_seat(st, a, items=drop_item(me.items, ADR, 0))
            moves.append(("use Adrenaline", [(1.0, spent)]))
            for v, vs in enumerate(st.seats):
                if v == a or vs.hp <= 0:
                    continue
                for it, copy, named in runs(vs.items):
                    if it == ADR:
                        continue                  # Adrenaline cannot be stolen
                    base = with_seat(spent, v, items=drop_item(vs.items, it, copy))
                    if not self.allowed(base, a, it):
                        continue
                    text = steal_text(it, copy, named, v)
                    for tgt in self.targets(base, a, it):
                        moves.append((text, self.use(base, a, it, tgt)))
        return moves

    def allowed(self, st: State, a: int, it: int) -> bool:
        """Every use the game allows.  The only refusals: a saw on a sawed
        barrel, a restraint when one was used this turn or nobody can be
        restrained, items outside their mode, and (a modelling choice) no
        information item for a seat other than the solved one."""
        if a != self.seat and it in (MG, PHONE):
            return False                  # opponents never read shells
        if it in (MG, BEER, CIG, PHONE, INV, MED):
            return True
        if it == SAW:
            return not st.sawed
        if it == CUFF:
            return (not self.mp and len(st.seats) == 2 and not st.cuffs_used
                    and bool(cuffable(st, a)))
        if it == JAM:
            return self.mp and not st.cuffs_used and bool(cuffable(st, a))
        if it == REM:
            return self.mp and sum(1 for s in st.seats if s.hp > 0) >= 3
        return False

    def targets(self, st: State, a: int, it: int) -> list[int | None]:
        if it in (CUFF, JAM):
            return list(cuffable(st, a))
        return [None]

    # ---- effects --------------------------------------------------------

    def shoot(self, st: State, tgt: int):
        # The chamber's shell leaves the tube, so its type is drawn here and
        # the shell is not marked; the drawn type is what leaves the tube.
        t = st.slots[0][0]
        if t is not None:
            cand = [(1.0, t)]
        else:
            ul, ub = unresolved_counts(st)
            cand = [(n / (ul + ub), ty) for n, ty in ((ul, "L"), (ub, "B")) if n]
        rest = st.slots[1:]
        out = []
        for p, ty in cand:
            eff = flip(ty) if st.inverted else ty
            seats = st.seats
            if eff == "L":
                seats = list(seats)
                seats[tgt] = seats[tgt]._replace(
                    hp=shot_damage(seats[tgt], 2 if st.sawed else 1))
                seats = tuple(seats)
            # The saw is spent by the shot, except when the Dealer's seat fires
            # a blank into itself and shells remain: it goes again with the
            # barrel still sawed.
            keep_saw = (self.blank_keeps_saw and len(seats) == 2 and st.turn == DEALER
                        and tgt == DEALER and eff == "B" and bool(rest))
            s2 = tuple.__new__(State, (
                seats, rest,
                st.live - (1 if ty == "L" else 0), st.blank - (1 if ty == "B" else 0),
                False, st.sawed and keep_saw, st.turn, st.direction, st.cuffs_used,
                st.budget, st.listcigs))
            keep = (tgt == st.turn and eff == "B")
            out.append((p, finish(s2, not keep)))
        return out

    def use(self, st: State, a: int, it: int, tgt: int | None):
        """Apply an item whose copy has already been removed from play."""
        if it == MG:
            return [(p, finish(s1, False)) for p, s1, _ in resolve(st, 0, viewer=a)]
        if it == BEER:
            return [(p, finish(pop_chamber(s1, ty), False))
                    for p, s1, ty in resolve(st, 0)]
        if it == CIG:
            me = st.seats[a]
            return [(1.0, finish(with_seat(st, a, hp=healed(me, 1)), False))]
        if it in (CUFF, JAM):
            s1 = with_seat(st, tgt, cuffed=True)._replace(cuffs_used=True)
            return [(1.0, finish(s1, False))]
        if it == SAW:
            return [(1.0, finish(st._replace(sawed=True), False))]
        if it == PHONE:
            weights = phone_weights(len(st.slots),
                                    two_seat_dealer_phone(st, a, self.mode))
            if not weights:
                return [(1.0, finish(st, False))]   # one shell: nothing to read
            out = []
            for k, w in weights.items():
                for p, s1, _ in resolve(st, k, viewer=a):
                    out.append((p * w, finish(s1, False)))
            return out
        if it == INV:
            t, seen = st.slots[0]
            if t is None:
                s1 = st._replace(inverted=not st.inverted)
            else:
                nt = flip(t)
                s1 = st._replace(
                    slots=((nt, seen),) + st.slots[1:],
                    live=st.live + (1 if nt == "L" else -1),
                    blank=st.blank + (1 if nt == "B" else -1),
                )
            return [(1.0, finish(s1, False))]
        if it == MED:
            me = st.seats[a]
            good = with_seat(st, a, hp=healed(me, 2))
            bad = with_seat(st, a, hp=max(0, me.hp - 1))
            return [(0.5, finish(good, False)), (0.5, finish(bad, False))]
        if it == REM:
            return [(1.0, finish(st._replace(direction=-st.direction), False))]
        raise ValueError(f"unusable item {it}")

    # ---- the Dealer -----------------------------------------------------

    def dealer_value(self, st: State, mem: Memory) -> float:
        """Value of the rest of a Dealer turn from the start of a pass."""
        key = (st, mem)
        v = self.dealer_memo.get(key)
        if v is not None:
            return v
        self.nodes += 1
        v = 0.0
        for p, _, s, m in self.dealer_pass(st, mem):
            v += p * (self.value(s) if m is None else self.dealer_value(s, m))
        self.dealer_memo[key] = v
        return v

    def dealer_pass(self, st: State, mem: Memory):
        """One pass of a Dealer turn: it uses one item or fires one shot.

        Returns [(probability, label, state, memory)].  After an item the
        memory is the one the next pass starts with; after the shot it is
        None and the state is the position the shot left.  Every branch ends
        with the stale-list bit rewritten: the list this pass built holds p1's
        Cigarettes when the Dealer held an Adrenaline and p1 still holds
        Cigarettes."""
        held_adr = ADR in st.seats[DEALER].items
        out = []
        for p0, s0 in self.dealer_uninvert(st):
            for p1, s1, m1 in self.dealer_read(s0, mem):
                for p2, label, s2, m2 in self.dealer_act(s1, m1):
                    bit = held_adr and CIG in s2.seats[PLAYER].items
                    out.append((p0 * p1 * p2, label, s2._replace(listcigs=bit), m2))
        return out

    def dealer_uninvert(self, st: State):
        """Step 0.  In the game an Inverter writes the flipped type into the
        shell itself, so a pending inversion (only a written position has one
        here) becomes an ordinary shell of the type it fires as."""
        if not st.inverted:
            return [(1.0, st)]
        out = []
        for p, s1, ty in resolve(st, 0):
            nt = flip(ty)
            seen = s1.slots[0][1]
            out.append((p, s1._replace(
                slots=((nt, seen),) + s1.slots[1:],
                live=s1.live + (1 if nt == "L" else -1),
                blank=s1.blank + (1 if nt == "B" else -1),
                inverted=False,
            )))
        return out

    def dealer_deduces(self, st: State) -> bool:
        """FigureOutShell, lines 282-303: the chamber is certain from what the
        Dealer has seen and from the tube's counts."""
        t, seen = st.slots[0]
        if t is not None and DEALER in seen:
            return True
        if st.live == 0 or st.blank == 0:
            return True
        seen_live = sum(1 for t, w in st.slots if t == "L" and DEALER in w)
        seen_blank = sum(1 for t, w in st.slots if t == "B" and DEALER in w)
        return st.live - seen_live == 0 or st.blank - seen_blank == 0

    def dealer_read(self, st: State, mem: Memory):
        """Steps 1 and 2: the deduction (ENDLESS brain only, lines 96-104) and
        the last shell (both brains, lines 106-112)."""
        out = [(1.0, st, mem)]
        if self.brain == "endless" and not mem.knows and self.dealer_deduces(st):
            out = [(p, s1, mem._replace(knows=True, known=ty, target=aim(ty)))
                   for p, s1, ty in resolve(st, 0, viewer=DEALER)]
        if len(st.slots) == 1:
            out = [(p * q, s2, m._replace(knows=True, known=ty, target=aim(ty)))
                   for p, s1, m in out
                   for q, s2, ty in resolve(s1, 0, viewer=DEALER)]
        return out

    def dealer_wants(self, st: State, mem: Memory, it: int, has_cigs: bool) -> bool:
        """The item scan's condition for one type, lines 152-201."""
        me = st.seats[DEALER]
        size = len(st.slots)
        if it == MG:
            return not mem.knows and size != 1
        if it == CIG:
            return me.hp < me.max_hp
        if it == MED:
            return (me.hp < me.max_hp and not has_cigs and not mem.used_medicine
                    and me.hp != 1)
        if it == BEER:
            return mem.known != "L" and size != 1
        if it == CUFF:
            you = st.seats[PLAYER]
            return you.hp > 0 and not you.cuffed and not you.skipped and size != 1
        if it == SAW:
            return not st.sawed and mem.known == "L"
        if it == PHONE:
            return size > 2
        if it == INV:
            return mem.knows and mem.known == "B"
        return False

    def dealer_pay(self, st: State, it: int, stolen: bool) -> State:
        """An own item costs the Dealer's first copy; a stolen one costs the
        Dealer's first Adrenaline and p1's first copy (lines 243-261)."""
        if not stolen:
            return with_seat(st, DEALER, items=drop_item(st.seats[DEALER].items, it, 0))
        st = with_seat(st, DEALER, items=drop_item(st.seats[DEALER].items, ADR, 0))
        return with_seat(st, PLAYER, items=drop_item(st.seats[PLAYER].items, it, 0))

    def dealer_coin(self, st: State):
        """CoinFlip, lines 421-431: [(probability, 0 or 1)].  The ENDLESS brain
        reads the whole tube's counts; the STORY brain flips a fair coin."""
        if self.brain == "endless" and st.live != st.blank:
            return [(1.0, 1 if st.live > st.blank else 0)]
        return [(0.5, 0), (0.5, 1)]

    def dealer_act(self, st: State, mem: Memory):
        """Steps 3 to 5: the item scan, the saw fallback and the shot."""
        me, you = st.seats[DEALER], st.seats[PLAYER]
        has_adr = ADR in me.items
        # The scan reads Cigarettes from the item list the previous pass built
        # (lines 113-116): the Dealer's own, or p1's when that list held them.
        has_cigs = CIG in me.items or st.listcigs
        # The list is rebuilt in table order: the Dealer's items, then p1's
        # when the Dealer holds an Adrenaline (lines 118-149).  The first entry
        # whose condition holds is used.
        scan = [(it, False) for it in me.items]
        if has_adr:
            scan += [(it, True) for it in you.items]
        for it, stolen in scan:
            if it in DEALER_SKIPS:
                continue
            if self.dealer_wants(st, mem, it, has_cigs):
                label = (f"steal {ITEM_NAMES[it]} from p1 and use it" if stolen
                         else f"use {ITEM_NAMES[it]}")
                base = self.dealer_pay(st, it, stolen)
                return [(p, label, s, m)
                        for p, s, m in self.dealer_use(base, mem, it)]

        # Step 4, lines 203-215: with a saw in reach and no blank known, the
        # coin decides between sawing (then shooting p1) and shooting itself.
        has_saw = SAW in me.items or (has_adr and SAW in you.items)
        if has_saw and not st.sawed and mem.known != "B":
            out = []
            stolen = SAW not in me.items
            for q, coin in self.dealer_coin(st):
                if coin == 0:
                    out += [(q * p, label, s, m) for p, label, s, m
                            in self.dealer_shoot(st, mem._replace(target=DEALER))]
                else:
                    label = ("steal Hand Saw from p1 and use it" if stolen
                             else "use Hand Saw")
                    s = self.dealer_pay(st, SAW, stolen)._replace(sawed=True)
                    out.append((q, label, s, mem._replace(target=PLAYER)))
            return out
        return self.dealer_shoot(st, mem)

    def dealer_use(self, st: State, mem: Memory, it: int):
        """The effect of an item the Dealer has paid for: [(p, state, memory)].

        PHONE and INV follow the Dealer's script, not `use`: its phone may
        pick any later offset, seen or not, and its inverter writes the live
        type into a chamber it has resolved."""
        if it == MG:
            return [(p, s1, mem._replace(knows=True, known=ty, target=aim(ty)))
                    for p, s1, ty in resolve(st, 0, viewer=DEALER)]
        if it == CIG:
            return [(1.0, with_seat(st, DEALER, hp=healed(st.seats[DEALER], 1)), mem)]
        if it == MED:
            me = st.seats[DEALER]
            mem = mem._replace(used_medicine=True)
            good = with_seat(st, DEALER, hp=healed(me, 2))
            bad = with_seat(st, DEALER, hp=max(0, me.hp - 1))
            return [(0.5, good, mem), (0.5, bad, mem)]
        if it == BEER:
            if self.brain == "endless":
                mem = mem._replace(knows=False, known=None)
            return [(p, pop_chamber(s1, ty), mem) for p, s1, ty in resolve(st, 0)]
        if it == CUFF:
            return [(1.0, with_seat(st, PLAYER, cuffed=True)._replace(cuffs_used=True), mem)]
        if it == SAW:
            return [(1.0, st._replace(sawed=True), mem)]
        if it == PHONE:
            size = len(st.slots)
            return [(p / (size - 1), s1, mem)
                    for k in range(1, size)
                    for p, s1, _ in resolve(st, k, viewer=DEALER)]
        if it == INV:
            out = []
            for p, s1, ty in resolve(st, 0, viewer=DEALER):
                if ty == "B":
                    s1 = s1._replace(slots=(("L", s1.slots[0][1]),) + s1.slots[1:],
                                     live=s1.live + 1, blank=s1.blank - 1)
                out.append((p, s1, mem._replace(known="L", target=PLAYER)))
            return out
        raise ValueError(f"the Dealer does not use item {it}")

    def dealer_shoot(self, st: State, mem: Memory):
        """Step 5, lines 268-280: shoot the target, or the coin's choice when
        there is none, with the ordinary shot.  The turn is then over."""
        if mem.target is not None:
            aims = [(1.0, mem.target)]
        else:
            aims = [(q, DEALER if coin == 0 else PLAYER) for q, coin in self.dealer_coin(st)]
        out = []
        for q, tgt in aims:
            label = "shoot self" if tgt == DEALER else "shoot p1"
            out += [(q * p, label, s, None) for p, s in self.shoot(st, tgt)]
        return out

    # ---- entry point ----------------------------------------------------

    def analyze(self, st: State, mem: Memory = FRESH):
        """Value of a root start and, when a choosing seat is to move, the value
        of each of its moves.  `mem` is the Dealer's memory when the Dealer is
        to move in the middle of its turn."""
        acts: list[tuple[str, float]] = []
        if self.dealer and st.turn == DEALER and st.slots and not round_over(st) \
                and mem != FRESH:
            return self.dealer_value(st, mem), acts, self.nodes
        v = self.value(st)
        if self.dealer and st.turn == DEALER:
            return v, acts, self.nodes    # the Dealer plays by its script
        if st.seats[self.seat].hp > 0 and st.slots and not round_over(st):
            acts = sorted(self.node_values(st).items(), key=lambda kv: (-kv[1], kv[0]))
        return v, acts, self.nodes


# --------------------------------------------------------------------------
# Position notation
# --------------------------------------------------------------------------

SEAT_RE = re.compile(r"^p(\d+)=(\d+)(?:/(\d+))?(?:\[([^\]]*)\])?$")
TURN_RE = re.compile(r"^turn=p(\d+)$")
TUBE_RE = re.compile(r"^tube=(\d+)l(\d+)b$")
KNOWN_RE = re.compile(r"^known=p(\d+):(.+)$")
SHELL_RE = re.compile(r"^(\d+)([lb])$")
PHONED_RE = re.compile(r"^phoned=p(\d+)@(\d+(?:,\d+)*)$")

DEALER_CORES = {"seen": "seen", "believes:b": "believes:B",
                "aim:self": "aim:self", "aim:p1": "aim:p1"}

MSG_TOO_MANY_ITEMS = "a seat holds at most 8 items"
MSG_BAD_PHONED = "phoned must look like phoned=p2@5 or phoned=p2@5,4"
MSG_PHONED_TWICE = "phoned is given twice for p{}"
MSG_READ_SIZE = "a phone read names a tube of 2 to 8 shells"
MSG_READ_TOO_SMALL = "a phone read names a tube at least as large as the one in the position"
MSG_TOO_MANY_READS = "a seat makes at most 8 phone reads in one load"
MSG_TWO_SEATS_ONLY = "listcigs and dealer need exactly two seats"
MSG_BAD_DEALER = ("dealer must be seen, believes:B, aim:self or aim:p1, optionally followed "
      "by med, as in dealer=seen,med")
MSG_BELIEVES_LIVE = ("dealer=believes:L cannot happen: the dealer never drinks a Beer on a shell "
      "it saw was live")
MSG_DEALER_CLASH = "dealer names at most one of seen, believes:B, aim:self and aim:p1"
MSG_DEALER_TWICE = "dealer is given twice"
MSG_DEALER_NOT_TO_MOVE = "dealer needs p2 to move"
MSG_CUFFED_MID_TURN = "a cuffed seat cannot be in the middle of its turn"
MSG_SEEN_NOT_KNOWN = "dealer=seen needs known=p2:0L or known=p2:0B"
MSG_AIM_P1_UNSAWED = "dealer=aim:p1 needs the barrel sawed"
MSG_WHOLE_POSITION_ONLY = "phoned and dealer are read only where a whole position is expected"
MSG_SAW_MEMORY_CLASH = ("the dealer saws only when it aims at p1, so a sawed barrel cannot go with "
       "this dealer memory")
MSG_EMPTY_TUBE = "dealer needs shells left in the tube"
MSG_DEALER_NEEDS_DEALER = "dealer describes the scripted dealer; use --opponent dealer"
MSG_BELIEVES_STORY_ONLY = "dealer=believes:B happens only in story mode"
MSG_AIM_SELF_DON_ONLY = "dealer=aim:self happens only in double or nothing"
MSG_PHONED_ADVISED_SEAT = ("phoned names p{0}, the seat being advised, which saw where its own phone "
       "looked; give known=p{0} instead")


class Refused(Exception):
    """A position that parses but cannot be solved as asked."""


class Position(NamedTuple):
    state: State
    reads: tuple[tuple[int, int], ...]          # (0-based seat, size at use)
    dealer: tuple[str | None, bool] | None      # (core, med) from dealer=


def parse_items(blob: str | None) -> list[int]:
    if not blob:
        return []
    out = []
    for raw in blob.split(","):
        tok = raw.strip().lower()
        if not tok:
            continue
        if tok not in ITEM_TOKENS:
            raise ValueError(f"unknown item token {raw!r}")
        out.append(ITEM_TOKENS[tok])
    return out


def parse_dealer(value: str) -> tuple[str | None, bool]:
    parts = value.split(",")
    if "believes:l" in parts:
        raise ValueError(MSG_BELIEVES_LIVE)
    if not parts or any(p not in DEALER_CORES and p != "med" for p in parts):
        raise ValueError(MSG_BAD_DEALER)
    cores = [p for p in parts if p != "med"]
    meds = len(parts) - len(cores)
    if len(cores) > 1:
        raise ValueError(MSG_DEALER_CLASH)
    if meds > 1 or (meds == 1 and parts[-1] != "med"):
        raise ValueError(MSG_BAD_DEALER)
    return (DEALER_CORES[cores[0]] if cores else None), meds == 1


def parse_seat_list(text: str, what: str) -> set[int]:
    out = set()
    for part in text.split(","):
        part = part.strip()
        if part:
            if not re.fullmatch(r"p\d+", part):
                raise ValueError(f"bad {what} seat {part!r}")
            out.add(int(part[1:]))
    return out


def parse_position(text: str, budget: int) -> Position:
    """Read a whole position: the state, the unseen phone reads and the
    Dealer's memory."""
    hp: dict[int, int] = {}
    mx: dict[int, int] = {}
    items: dict[int, list[int]] = {}
    tube = None
    turn = 1
    cuffs: set[int] = set()
    skips: set[int] = set()
    sawed = inverted = restraint = listcigs = False
    direction = 1
    known: list[tuple[int, int, str]] = []
    reads: dict[int, list[int]] = {}
    dealer: tuple[str | None, bool] | None = None

    for tok in text.split():
        low = tok.lower()
        if low == "sawed":
            sawed = True
            continue
        if low == "inverted":
            inverted = True
            continue
        if low == "restraintused":
            restraint = True
            continue
        if low == "listcigs":
            listcigs = True
            continue
        if low.startswith("dir="):
            d = low[4:]
            if d not in ("cw", "ccw"):
                raise ValueError(f"bad direction {tok!r}")
            direction = 1 if d == "cw" else -1
            continue
        if low.startswith("turn="):
            m = TURN_RE.match(low)
            if not m:
                if "[" in low:
                    raise ValueError("items go on the seat, as in p1=2/2[beer], not on turn=")
                raise ValueError(f"bad turn {tok!r}")
            turn = int(m.group(1))
            continue
        if low.startswith("cuffed="):
            cuffs |= parse_seat_list(low[7:], "cuffed")
            continue
        if low.startswith("skipped="):
            skips |= parse_seat_list(low[8:], "skipped")
            continue
        if low.startswith("phoned="):
            m = PHONED_RE.match(low)
            if not m:
                raise ValueError(MSG_BAD_PHONED)
            who = int(m.group(1))
            if who in reads:
                raise ValueError(MSG_PHONED_TWICE.format(who))
            sizes = [int(x) for x in m.group(2).split(",")]
            if any(not 2 <= n <= 8 for n in sizes):
                raise ValueError(MSG_READ_SIZE)
            if len(sizes) > MAX_READS:
                raise ValueError(MSG_TOO_MANY_READS)
            reads[who] = sizes
            continue
        if low.startswith("dealer="):
            if dealer is not None:
                raise ValueError(MSG_DEALER_TWICE)
            dealer = parse_dealer(low[7:])
            continue
        m = TUBE_RE.match(low)
        if m:
            tube = (int(m.group(1)), int(m.group(2)))
            continue
        m = KNOWN_RE.match(low)
        if m:
            who = int(m.group(1))
            for part in m.group(2).split(","):
                sm = SHELL_RE.match(part.strip())
                if not sm:
                    raise ValueError(f"bad known entry {part!r}")
                known.append((who, int(sm.group(1)), sm.group(2).upper()))
            continue
        m = SEAT_RE.match(low)
        if m:
            i = int(m.group(1))
            hp[i] = int(m.group(2))
            mx[i] = int(m.group(3)) if m.group(3) else int(m.group(2))
            items.setdefault(i, []).extend(parse_items(m.group(4)))
            if len(items[i]) > TABLE_LIMIT:
                raise ValueError(MSG_TOO_MANY_ITEMS)
            continue
        raise ValueError(f"unparsed token {tok!r}")

    if not hp:
        raise ValueError("no seats given")
    n = max(hp)
    if sorted(hp) != list(range(1, n + 1)):
        raise ValueError("seats must be numbered 1..N with no gaps")
    if not 2 <= n <= 4:
        raise ValueError("two to four seats")
    if tube is None:
        raise ValueError("tube=<live>L<blank>B is required")
    live, blank = tube
    if live + blank > 8:
        raise ValueError("the tube holds at most 8 shells")
    if turn not in hp or hp[turn] <= 0:
        raise ValueError("turn must name a living seat")
    for who in cuffs | skips | set(reads) | {w for w, _, _ in known}:
        if who not in hp:
            raise ValueError(f"p{who} is not in the position")

    slots: list[tuple[str | None, tuple[int, ...]]] = [(None, ())] * (live + blank)
    for who, off, ty in known:
        if off >= len(slots):
            raise ValueError(f"known offset {off} past the end of the tube")
        cur, seen = slots[off]
        if cur is not None and cur != ty:
            raise ValueError(f"contradictory knowledge at offset {off}")
        slots[off] = (ty, tuple(sorted(set(seen + (who - 1,)))))
    rl = sum(1 for t, _ in slots if t == "L")
    rb = sum(1 for t, _ in slots if t == "B")
    if rl > live or rb > blank:
        raise ValueError("known shells exceed the tube composition")

    if (listcigs or dealer is not None) and n != 2:
        raise ValueError(MSG_TWO_SEATS_ONLY)
    for who, sizes in reads.items():
        if any(size < live + blank for size in sizes):
            raise ValueError(MSG_READ_TOO_SMALL)
    if dealer is not None:
        core, _ = dealer
        if turn != 2:
            raise ValueError(MSG_DEALER_NOT_TO_MOVE)
        if 2 in cuffs:
            raise ValueError(MSG_CUFFED_MID_TURN)
        if live + blank == 0:
            raise ValueError(MSG_EMPTY_TUBE)
        if core == "seen" and not (slots[0][0] is not None and 1 in slots[0][1]):
            raise ValueError(MSG_SEEN_NOT_KNOWN)
        if core == "aim:p1" and not sawed:
            raise ValueError(MSG_AIM_P1_UNSAWED)
        if sawed and (core in ("believes:B", "aim:self")
                      or (core == "seen" and slots[0][0] == "B")):
            raise ValueError(MSG_SAW_MEMORY_CLASH)

    seats = []
    for i in range(1, n + 1):
        if mx[i] < hp[i] or mx[i] < 1:
            raise ValueError(f"p{i} has a bad charge count")
        seats.append(Seat(hp=hp[i], max_hp=mx[i], items=tuple(items.get(i, [])),
                          cuffed=(i in cuffs), skipped=(i in skips)))
    st = State(
        seats=tuple(seats), slots=tuple(slots), live=live, blank=blank,
        inverted=inverted, sawed=sawed, turn=turn - 1, direction=direction,
        cuffs_used=restraint, budget=budget, listcigs=listcigs,
    )
    flat = tuple((who - 1, size) for who in sorted(reads)
                 for size in sorted(reads[who], reverse=True))
    return Position(st, flat, dealer)


def parse_state(text: str, budget: int = 0) -> State:
    """Read a bare state.  Unseen phone reads and the Dealer's memory are not
    part of one, so they are refused rather than dropped."""
    if any(tok.lower().startswith(("phoned=", "dealer=")) for tok in text.split()):
        raise ValueError(MSG_WHOLE_POSITION_ONLY)
    return parse_position(text, budget).state


def print_state(st: State) -> str:
    out = []
    for i, s in enumerate(st.seats):
        tok = f"p{i + 1}={s.hp}/{s.max_hp}"
        if s.items:
            tok += "[" + ",".join(TOKEN_OF[it] for it in s.items) + "]"
        out.append(tok)
    out.append(f"tube={st.live}L{st.blank}B")
    out.append(f"turn=p{st.turn + 1}")
    if st.sawed:
        out.append("sawed")
    if st.inverted:
        out.append("inverted")
    if st.cuffs_used:
        out.append("restraintused")
    if st.direction == -1:
        out.append("dir=ccw")
    cuffed = [f"p{i + 1}" for i, s in enumerate(st.seats) if s.cuffed]
    if cuffed:
        out.append("cuffed=" + ",".join(cuffed))
    skipped = [f"p{i + 1}" for i, s in enumerate(st.seats) if s.skipped]
    if skipped:
        out.append("skipped=" + ",".join(skipped))
    for i in range(len(st.seats)):
        facts = [f"{j}{t}" for j, (t, seen) in enumerate(st.slots)
                 if t is not None and i in seen]
        if facts:
            out.append(f"known=p{i + 1}:" + ",".join(facts))
    if st.listcigs:
        out.append("listcigs")
    return " ".join(out)


def print_position(pos: Position) -> str:
    out = [print_state(pos.state)]
    for who in sorted({w for w, _ in pos.reads}):
        sizes = sorted((n for w, n in pos.reads if w == who), reverse=True)
        out.append(f"phoned=p{who + 1}@" + ",".join(str(n) for n in sizes))
    if pos.dealer is not None:
        core, med = pos.dealer
        out.append("dealer=" + ",".join(([core] if core else []) + (["med"] if med else [])))
    return " ".join(out)


# --------------------------------------------------------------------------
# Root
# --------------------------------------------------------------------------


def root_memory(template: tuple[str | None, bool] | None, st: State) -> Memory:
    """The Dealer's memory at one root start, from the `dealer=` template.  For
    `seen` it holds the chamber as drawn in this start."""
    if template is None:
        return FRESH
    core, med = template
    if core == "seen":
        t0 = st.slots[0][0]
        return Memory(True, t0, aim(t0), med)
    if core == "believes:B":
        return Memory(True, "B", DEALER, med)
    if core == "aim:self":
        return Memory(False, None, DEALER, med)
    if core == "aim:p1":
        return Memory(False, None, PLAYER, med)
    return Memory(False, None, None, med)


def check_solvable(pos: Position, seat: int, mode: str, opponent: str) -> None:
    """The refusals that depend on how a position is solved."""
    if pos.dealer is not None:
        core, _ = pos.dealer
        if opponent != "dealer":
            raise Refused(MSG_DEALER_NEEDS_DEALER)
        if core == "believes:B" and BRAINS.get(mode) == "endless":
            raise Refused(MSG_BELIEVES_STORY_ONLY)
        if core == "aim:self" and BRAINS.get(mode) == "story":
            raise Refused(MSG_AIM_SELF_DON_ONLY)
    if any(who == seat for who, _ in pos.reads):
        raise Refused(MSG_PHONED_ADVISED_SEAT.format(seat + 1))


def root_starts(pos: Position, seat: int, mode: str = "don", opponent: str = "optimal"):
    """The positions the solved seat could be in, with weights: one per way the
    unseen phone reads could have fallen, times one per way the shells other
    seats have looked at could have fallen.  Each start carries the Dealer's
    memory, has its root skips applied and, under any model but the Dealer, is
    in sorted form.  Returns (starts, counted shells, dropped)."""
    check_solvable(pos, seat, mode, opponent)
    keep = pos.dealer is not None and pos.dealer[0] == "seen"
    starts = []
    most, dropped = 0, False
    for w, extra in expand_reads(pos.state, pos.reads, mode):
        branches, count, drop = knowledge_branches(pos.state, seat, KNOWLEDGE_LIMIT,
                                                   extra, keep)
        dropped = dropped or drop
        if w > 0.0:
            most = max(most, count)
        for p, b in branches:
            mem = root_memory(pos.dealer, b)
            b = skip_cuffed_mover(b)
            if opponent != "dealer":
                b = canonical(b)
            starts.append((w * p, b, mem))
    return starts, most, dropped


def solve_position(pos: Position, seat: int, mode: str = "don", opponent: str = "optimal",
                   items_per_load: int | None = None, saw_survives: bool | None = None):
    """Solve `pos` for the 0-based `seat`, averaging over what the seat cannot
    see: the shells only another seat has looked at, and where another seat's
    phone looked.

    Every start is the same position to this seat, so the ranking is one list
    whose rows are averaged over them.  This is the only entry point that may
    be handed a raw parsed position: the solver itself reads a state as the
    truth, so anything that reaches it must already have been cut down to one
    seat's information.

    When the solved seat is to move, its information state is exactly this
    weighted mixture of starts, so it picks the best averaged row and that
    row is the value.  Every other root value is the weighted average of the
    start values.

    A cuffed seat to move is skipped first.  The skips read no shell, so they
    are the same in every start.  Under the Dealer opponent a start where p2
    is to move is a Dealer turn (from its written memory, if any) and lists no
    actions."""
    if opponent == "dealer" and len(pos.state.seats) != 2:
        raise ValueError("the Dealer opponent needs exactly two seats")
    solver = Solver(seat, mode, opponent, items_per_load, saw_survives)
    starts, _, dropped = root_starts(pos, seat, mode, opponent)
    value = 0.0
    rows: dict[str, float] = {}
    for weight, start, mem in starts:
        v, acts, _ = solver.analyze(start, mem)
        value += weight * v
        for text, av in acts:
            rows[text] = rows.get(text, 0.0) + weight * av
    order = sorted(rows.items(), key=lambda kv: (-kv[1], kv[0]))
    turn = starts[0][1].turn if starts else pos.state.turn
    if order and turn == seat:
        # Inside one start the solver would choose from a fully blinded copy
        # of that start, which is a coarser view than the mixture itself.
        value = order[0][1]
    return value, order, solver.nodes, dropped


def run(pos: str, seat: int = 1, reloads: int = 2, mode: str = "don",
        opponent: str = "optimal", items_per_load: int | None = None,
        saw_survives: bool | None = None):
    value, order, _, _ = solve_position(parse_position(pos, reloads), seat - 1, mode,
                                        opponent, items_per_load, saw_survives)
    return value, dict(order), order


@contextlib.contextmanager
def heal_floor(floor: int):
    global HEAL_FLOOR
    old, HEAL_FLOOR = HEAL_FLOOR, floor
    try:
        yield
    finally:
        HEAL_FLOOR = old


# --------------------------------------------------------------------------
# Self-test.  Every expected value below is computed by hand; the arithmetic
# is written out in the comment above each case.
# --------------------------------------------------------------------------


def selftest() -> int:
    fails = 0
    log: list[str] = []

    def check(name: str, got: float, want: float, tol: float = 1e-9) -> None:
        nonlocal fails
        ok = abs(got - want) < tol
        fails += 0 if ok else 1
        log.append(f"{'PASS' if ok else 'FAIL'}  {name:<46} got {got:.9f} want {want:.9f}")

    def check_true(name: str, cond: bool, note: str) -> None:
        nonlocal fails
        fails += 0 if cond else 1
        log.append(f"{'PASS' if cond else 'FAIL'}  {name:<46} {note}")

    # 1. p1=1/1 p2=1/1 tube=1L1B turn=p1.
    #    shoot p2 : 1/2 live -> p2 dead -> 1 ; 1/2 blank -> tube 1L0B, p2 to move,
    #               p2 shoots p1 with a certain live shell -> 0.  = 1/2 + 0 = 0.5
    #    shoot self: 1/2 live -> p1 dead -> 0 ; 1/2 blank -> p1 keeps the turn with
    #               a certain live shell and shoots p2 -> 1.  = 0 + 1/2 = 0.5
    v, d, _ = run("p1=1/1 p2=1/1 tube=1L1B turn=p1", reloads=1)
    check("1L1B coin flip: value", v, 0.5)
    check("1L1B coin flip: shoot p2", d["shoot p2"], 0.5)
    check("1L1B coin flip: shoot self", d["shoot self"], 0.5)

    # 2. p1=1/1 p2=1/1 tube=1L0B turn=p1.  The chamber is certainly live.
    #    shoot p2 -> p2 loses its last charge -> 1.0 ; shoot self -> 0.0
    v, d, _ = run("p1=1/1 p2=1/1 tube=1L0B turn=p1", reloads=1)
    check("certain live: value", v, 1.0)
    check("certain live: shoot p2", d["shoot p2"], 1.0)
    check("certain live: shoot self", d["shoot self"], 0.0)

    # 3. p1=1/1 p2=2/2 tube=1L0B turn=p1 sawed.  Sawed live shell deals 2,
    #    so p2's 2 charges go to 0 in one shot -> 1.0
    v, d, _ = run("p1=1/1 p2=2/2 tube=1L0B turn=p1 sawed", reloads=1)
    check("sawed double damage: value", v, 1.0)
    check("sawed double damage: shoot p2", d["shoot p2"], 1.0)

    # 4. p1=1/2[inv] p2=1/2 tube=1L3B turn=p1.  Chamber live prob 1/4 before the
    #    inverter and 1 - 1/4 = 3/4 after it, so the inverter strictly helps.
    st = parse_state("p1=1/2[inv] p2=1/2 tube=1L3B turn=p1", 1)
    check("inverter: chamber live before", chamber_live_prob(st), 0.25)
    sv = Solver(0, "don")
    stripped = with_seat(st, 0, items=())
    after = sv.use(stripped, 0, INV, None)[0][1]
    check("inverter: chamber live after", chamber_live_prob(after), 0.75)
    v, d, acts = run("p1=1/2[inv] p2=1/2 tube=1L3B turn=p1", reloads=1)
    check_true("inverter: strictly beats shooting",
               d["use Inverter"] > d["shoot p2"] + 1e-12
               and d["use Inverter"] > d["shoot self"] + 1e-12,
               f"inv={d['use Inverter']:.6f} > p2={d['shoot p2']:.6f}, "
               f"self={d['shoot self']:.6f}")
    check_true("inverter: ranked first", acts[0][0] == "use Inverter", f"top={acts[0][0]!r}")

    # 5. p1=1/1 p2=1/1 tube=1L2B turn=p1.
    #    shoot p2 : 1/3 * 1 + 2/3 * V(1L1B, p2 to move) = 1/3 + 2/3 * 1/2 = 2/3
    #    shoot self: 1/3 * 0 + 2/3 * V(1L1B, p1 to move) = 2/3 * 1/2 = 1/3
    v, d, _ = run("p1=1/1 p2=1/1 tube=1L2B turn=p1", reloads=1)
    check("1L2B: value", v, 2.0 / 3.0)
    check("1L2B: shoot p2", d["shoot p2"], 2.0 / 3.0)
    check("1L2B: shoot self", d["shoot self"], 1.0 / 3.0)

    # 6. p1=1/1[cuff] p2=1/1 tube=1L1B turn=p1.  Cuff p2, then shoot p2:
    #    1/2 live -> p2 dead -> 1 ; 1/2 blank -> p2 is skipped (cuffs come off)
    #    and p1 fires the remaining certain live shell at p2 -> 1.  = 1.0
    v, d, _ = run("p1=1/1[cuff] p2=1/1 tube=1L1B turn=p1", reloads=1)
    check("handcuffs: value", v, 1.0)
    check("handcuffs: cuff p2", d["use Handcuffs on p2"], 1.0)

    # 7. p1=1/2[med] p2=1/1 tube=1L0B turn=p1.  Expired Medicine:
    #    1/2 -> hp min(1+2, 2) = 2, then the certain live shell kills p2 -> 1
    #    1/2 -> hp 1-1 = 0, p1 is dead -> 0.  = 0.5 ; shooting p2 is 1.0
    v, d, _ = run("p1=1/2[med] p2=1/1 tube=1L0B turn=p1", reloads=1)
    check("medicine: value", v, 1.0)
    check("medicine: use it", d["use Expired Medicine"], 0.5)

    # 8. p1=1/1[saw] p2=2/2 tube=1L0B turn=p1.  Saw first, then the certain live
    #    shell deals 2 and ends p2 -> 1.0
    v, d, _ = run("p1=1/1[saw] p2=2/2 tube=1L0B turn=p1", reloads=1)
    check("hand saw: value", v, 1.0)
    check("hand saw: use it", d["use Hand Saw"], 1.0)

    # 9. p1=2/2 p2=1/1 p3=1/1 tube=2L0B turn=p1 --reloads 0.  Both shells are
    #    certainly live.  p1 shoots p2 (dead); p3 is next and minimises: shooting
    #    itself would hand p1 the round (1.0), so it shoots p1 -> p1 1 charge,
    #    the tube is empty and the budget is spent, so the boundary values the
    #    position at my_hp / total hp = 1 / (1 + 0 + 1) = 0.5.
    #    Shooting self first instead leaves p2 to kill p1 -> 0.
    v, d, _ = run("p1=2/2 p2=1/1 p3=1/1 tube=2L0B turn=p1", reloads=0)
    check("3 seats + boundary: value", v, 0.5)
    check("3 seats + boundary: shoot p2", d["shoot p2"], 0.5)
    check("3 seats + boundary: shoot self", d["shoot self"], 0.0)

    rules_selftest(check, check_true)
    notation_selftest(check_true)
    dealer_selftest(check, check_true)

    print("\n".join(log))
    print(f"{'ALL PASS' if fails == 0 else str(fails) + ' FAILURE(S)'}"
          f" ({len(log)} checks)")
    return 1 if fails else 0


def outcomes(pos: str, text: str, mode: str = "don") -> dict[str, float]:
    """Apply one move of the seat to move: {printed state: probability}, with
    equal states summed."""
    st = parse_state(pos, 0)
    sv = Solver(st.turn, mode)
    dists = [dist for t, dist in sv.gen_moves(st) if t == text]
    if len(dists) != 1:
        raise ValueError(f"{text!r} is not one legal move in {pos!r}")
    out: dict[str, float] = {}
    for p, s in dists[0]:
        key = print_state(s)
        out[key] = out.get(key, 0.0) + p
    return out


def same_outcomes(got: dict[str, float], want: dict[str, float]) -> bool:
    return set(got) == set(want) and all(abs(got[k] - want[k]) < 1e-12 for k in want)


def rules_selftest(check, check_true) -> None:
    """The game rules: reloads, loads, the phone, the stage-three clamp, the
    full legal set, ordered hands, unseen phone reads and the Dealer's seat
    keeping the saw."""

    # A Beer on the last shell reloads with the barrel still sawed.  Three
    # of the seven loads have p2 to fire into a sawed barrel at 2 charges.
    v, d, _ = run("p1=1/1[beer] p2=2/2 tube=0L1B turn=p1 sawed", reloads=1,
                  items_per_load=0)
    check("sawed reload: value", v, 501.0 / 980.0)
    check("sawed reload: use Beer", d["use Beer"], 501.0 / 980.0)
    check("sawed reload: shoot self", d["shoot self"], 439.0 / 1470.0)
    check("sawed reload: shoot p2", d["shoot p2"], 439.0 / 1470.0)
    v, d, _ = run("p1=1/1[beer] p2=2/2 tube=0L1B turn=p1 sawed", reloads=1,
                  items_per_load=0, saw_survives=False)
    check("saw cleared at reload: value", v, 439.0 / 1470.0)
    check("saw cleared at reload: use Beer", d["use Beer"], 123.0 / 490.0)

    # Seven Double or Nothing loads at 1/7.
    v, d, _ = run("p1=1/1 p2=1/1 tube=0L1B turn=p1", reloads=1, items_per_load=0)
    check("seven loads: value", v, 367.0 / 588.0)
    check("seven loads: shoot self", d["shoot self"], 367.0 / 588.0)
    check("seven loads: shoot p2", d["shoot p2"], 367.0 / 588.0)
    solver = Solver(0, "don")
    check_true("seven loads: compositions",
               [lb for lb, _ in solver.loads] == list(LOADS_DON)
               and all(abs(p - 1.0 / 7.0) < 1e-15 for _, p in solver.loads),
               f"{[lb for lb, _ in solver.loads]}")
    # Four items each, in pool order from the seat index, appended.
    dealt = solver.reload_branches(parse_state("p1=2/2 p2=2/2 tube=0L0B turn=p1", 1))
    check_true("four items dealt in order",
               all(print_state(s).startswith("p1=2/2[mg,beer,cig,cuff] "
                                             "p2=2/2[beer,cig,cuff,saw] ")
                   for _, s in dealt), print_state(dealt[0][1]))

    # p1's phone on three shells: offset 1 (1/2) is already known blank;
    # offset 2 (1/2) is drawn from 1L1B.
    got = outcomes("p1=2/2[phone] p2=2/2 tube=1L2B turn=p1 known=p1:1B", "use Burner Phone")
    want = {"p1=2/2 p2=2/2 tube=1L2B turn=p1 known=p1:1B": 0.5,
            "p1=2/2 p2=2/2 tube=1L2B turn=p1 known=p1:1B,2L": 0.25,
            "p1=2/2 p2=2/2 tube=1L2B turn=p1 known=p1:1B,2B": 0.25}
    check_true("player phone: offsets", same_outcomes(got, want), f"{got}")
    # p1 at eight shells: offset 7 reads as 6, so 6 carries 2/7 and 7 nothing.
    got = outcomes("p1=2/2[phone] p2=2/2 tube=4L4B turn=p1", "use Burner Phone")
    ok = all(abs(got[f"p1=2/2 p2=2/2 tube=4L4B turn=p1 known=p1:{k}{t}"]
                 - (1.0 / 7.0 if k == 6 else 1.0 / 14.0)) < 1e-12
             for k in range(1, 7) for t in "LB") and len(got) == 12
    check_true("player phone: offset 7 folds into 6", ok, f"{len(got)} outcomes")
    got = outcomes("p1=2/2 p2=2/2[phone] tube=4L4B turn=p2", "use Burner Phone")
    ok = all(abs(p - 1.0 / 14.0) < 1e-12 for p in got.values()) and len(got) == 14
    check_true("dealer-seat phone: uniform to 7", ok, f"{len(got)} outcomes")
    got = outcomes("p1=2/2[phone] p2=2/2 tube=1L0B turn=p1", "use Burner Phone")
    check_true("phone on one shell: spent, nothing read",
               same_outcomes(got, {"p1=2/2 p2=2/2 tube=1L0B turn=p1": 1.0}), f"{got}")

    # Stage three: a hit at or above the floor leaves at least one charge.
    # Either shot: live leaves the hit seat at 1, then the boundary values the
    # rest; a p2 blank into itself keeps the saw, so p1 then takes 2 and keeps 1.
    # shoot p1: 1/2 * 1/3 + 1/2 * 2/3 = 1/2; shoot self: 1/2 * 2/3 + 1/2 * 1/3.
    with heal_floor(2):
        v, d, _ = run("p1=2/5 p2=2/5 tube=1L1B turn=p2 sawed", reloads=0, mode="story")
        check("stage three clamp: value", v, 0.5)
        check("stage three clamp: shoot p1", d["shoot p1"], 0.5)
        check("stage three clamp: shoot self", d["shoot self"], 0.5)
        got = outcomes("p1=2/5[med] p2=2/5 tube=1L1B turn=p1", "use Expired Medicine", "story")
        check_true("stage three: failed dose is plain",
                   same_outcomes(got, {"p1=4/5 p2=2/5 tube=1L1B turn=p1": 0.5,
                                       "p1=1/5 p2=2/5 tube=1L1B turn=p1": 0.5}), f"{got}")

    # The full legal set, in move order, and the wasted uses.
    st = parse_state("p1=2/2[beer,adr,mg] p2=2/2[beer,mg] tube=1L1B turn=p1")
    texts = [t for t, _ in Solver(0).gen_moves(st)]
    check_true("legal set in order", texts == [
        "shoot self", "shoot p2", "use Magnifying Glass", "use Beer", "use Adrenaline",
        "steal Magnifying Glass from p2 and use it", "steal Beer from p2 and use it"],
        f"{texts}")
    got = outcomes("p1=2/2[cig] p2=2/2 tube=1L1B turn=p1", "use Cigarettes")
    check_true("cigarettes at full charge: wasted",
               same_outcomes(got, {"p1=2/2 p2=2/2 tube=1L1B turn=p1": 1.0}), f"{got}")
    got = outcomes("p1=2/2[mg] p2=2/2 tube=1L1B turn=p1 known=p1:0L", "use Magnifying Glass")
    check_true("glass on a known chamber: wasted",
               same_outcomes(got, {"p1=2/2 p2=2/2 tube=1L1B turn=p1 known=p1:0L": 1.0}),
               f"{got}")
    got = outcomes("p1=2/2[adr] p2=2/2[cig] tube=1L1B turn=p1", "use Adrenaline")
    check_true("bare Adrenaline: spent",
               same_outcomes(got, {"p1=2/2 p2=2/2[cig] tube=1L1B turn=p1": 1.0}), f"{got}")
    for name, pos, gone in (
        ("Adrenaline from Adrenaline", "p1=2/2[adr] p2=2/2[adr] tube=1L1B turn=p1", "steal"),
        ("saw on a sawed barrel", "p1=2/2[saw] p2=2/2 tube=1L1B turn=p1 sawed",
         "use Hand Saw"),
        ("cuffs on a cuffed seat", "p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 cuffed=p2",
         "use Handcuffs"),
        ("cuffs on a skipped seat", "p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 skipped=p2",
         "use Handcuffs"),
        ("second cuffs in a turn", "p1=2/2[cuff] p2=2/2 tube=1L1B turn=p1 restraintused",
         "use Handcuffs"),
    ):
        texts = [t for t, _ in Solver(0).gen_moves(parse_state(pos))]
        check_true(f"refused: {name}", not any(t.startswith(gone) for t in texts), f"{texts}")

    # Ordered hands: one move per run, named when an item has two runs.
    st = parse_state("p1=2/2[beer,mg,beer] p2=2/2 tube=1L1B turn=p1")
    texts = [t for t, _ in Solver(0).gen_moves(st)]
    check_true("named copies", texts == ["shoot self", "shoot p2", "use Magnifying Glass",
                                         "use Beer #1", "use Beer #2"], f"{texts}")
    sv = Solver(0)
    left = {print_state(s).split()[0] for t, dist in sv.gen_moves(st)
            if t == "use Beer #2" for _, s in dist}
    check_true("named copy removed", left == {"p1=2/2[beer,mg]"}, f"{left}")
    texts = [t for t, _ in Solver(0).gen_moves(parse_state(
        "p1=2/2[beer,beer,mg] p2=2/2 tube=1L1B turn=p1"))]
    check_true("one run, one move", "use Beer" in texts and "use Beer #1" not in texts,
               f"{texts}")
    # Under the minimising model the order is forgotten.
    a, _, ra = run("p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1", reloads=0)
    b, _, rb = run("p1=2/4[mg,saw] p2=4/4[cuff,beer] tube=2L3B turn=p1", reloads=0)
    check_true("hand order ignored by the optimal model", abs(a - b) < 1e-15 and ra == rb,
               f"{a} {b}")

    # Unseen phone reads.
    def masks(pos: str, seat: int):
        p = parse_position(pos, 0)
        return read_masks(p.state, p.reads, seat)

    def same_masks(got, want) -> bool:
        return [m for m, _ in got] == [m for m, _ in want] and all(
            abs(a - b) < 1e-12 for (_, a), (_, b) in zip(got, want))

    got = masks("p1=2/2 p2=2/2 tube=1L2B turn=p1 phoned=p2@5", 1)
    check_true("phone read, two shells gone", same_masks(
        got, [(0, 0.25), (1, 0.25), (2, 0.25), (4, 0.25)]), f"{got}")
    got = masks("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@5,4", 1)
    t12, t6 = 1.0 / 12.0, 1.0 / 6.0
    check_true("phone reads, two of them", same_masks(
        got, [(2, t12), (3, t12), (4, t12), (5, t12), (6, t6), (8, t12), (9, t12),
              (10, t6), (12, t6)]), f"{got}")
    got = masks("p1=2/2 p2=2/2 tube=4L4B turn=p2 phoned=p1@8", 0)
    s7 = 1.0 / 7.0
    check_true("player phone read folds 7 into 6", same_masks(
        got, [(2, s7), (4, s7), (8, s7), (16, s7), (32, s7), (64, 2 * s7)]), f"{got}")
    # The read averages the known-shell positions it could have been.
    v, d, _ = run("p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 phoned=p2@5", reloads=0)
    parts = [run(f"p1=2/2[beer] p2=2/2 tube=2L3B turn=p1 known=p2:{k}B", reloads=0)[1]
             for k in range(1, 5)]
    check_true("phone read is the mean of its offsets",
               all(abs(d[t] - sum(p[t] for p in parts) / 4) < 1e-12 for t in d)
               and set(d) == set(parts[0]), f"{d}")

    # p2 shoots itself: live (1/2) leaves p2 0, p1 wins; blank (1/2) keeps
    # the turn and the saw, and the live shell then takes p1 -> 0.  So 1/2.
    # p2 shoots p1: live -> 0; blank (1/2) -> p1 shoots the live shell at p2,
    # 2/(2+1) = 2/3 at the boundary.  So 1/3, and p2 picks it.
    v, d, _ = run("p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed", reloads=0)
    check("dealer seat keeps the saw: value", v, 1.0 / 3.0)
    check("dealer seat keeps the saw: shoot p1", d["shoot p1"], 1.0 / 3.0)
    check("dealer seat keeps the saw: shoot self", d["shoot self"], 0.5)
    # A blank p2 fires into itself keeps the saw in a two-seat game, not in
    # a multiplayer one.
    got = outcomes("p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed known=p2:0B", "shoot self")
    check_true("blank into the dealer seat keeps the saw",
               same_outcomes(got, {"p1=2/2 p2=2/2 tube=1L0B turn=p2 sawed": 1.0}), f"{got}")
    got = outcomes("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p2 sawed known=p2:0B",
                   "shoot self", "mp")
    check_true("multiplayer blank into itself clears the saw",
               same_outcomes(got, {"p1=2/2 p2=2/2 p3=2/2 tube=1L0B turn=p2": 1.0}), f"{got}")
    got = outcomes("p1=2/2 p2=2/2 tube=1L1B turn=p1 sawed known=p1:0B", "shoot self")
    check_true("p1 blank into itself clears the saw",
               same_outcomes(got, {"p1=2/2 p2=2/2 tube=1L0B turn=p1": 1.0}), f"{got}")
    got = outcomes("p1=2/2 p2=2/2 tube=0L1B turn=p2 sawed", "shoot self")
    check_true("dealer seat blank on the last shell clears the saw",
               same_outcomes(got, {"p1=2/2 p2=2/2 tube=0L0B turn=p2": 1.0}), f"{got}")

    # p1's moves and reloads leave the stale-list bit alone.
    sv = Solver(0, "don", "dealer")
    st = parse_state("p1=2/3[cig,beer,adr] p2=2/3[mg] tube=2L2B turn=p1 listcigs")
    ok = all(s.listcigs for _, dist in sv.gen_moves(st) for _, s in dist)
    ok = ok and all(s.listcigs for _, s in sv.reload_branches(
        parse_state("p1=2/3[cig] p2=2/3 tube=0L0B turn=p1 listcigs", 1)))
    check_true("p1 moves and reloads keep listcigs", ok, "every outcome")
    # The minimising model forgets the bit.
    a = run("p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p1 listcigs", reloads=0)[0]
    b = run("p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p1", reloads=0)[0]
    check("optimal ignores listcigs", a, b, 1e-15)


def notation_selftest(check_true) -> None:
    """Round trips and refusals, exact texts."""
    for given, want in (
        ("p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1",
         "p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1"),
        ("turn=p2 p2=2/2 phoned=p2@4,6 p1=2/2[beer,mg,beer] tube=1L2B",
         "p1=2/2[beer,mg,beer] p2=2/2 tube=1L2B turn=p2 phoned=p2@6,4"),
        ("dealer=aim:p1,med sawed listcigs p1=2/2 p2=2/2 tube=1L1B turn=p2",
         "p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed listcigs dealer=aim:p1,med"),
        ("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 known=p2:0L dealer=seen", None),
        ("p1=2/2[cig] p2=2/3[med] tube=2L2B turn=p2 listcigs", None),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p1@4 phoned=p2@5", None),
        ("p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1", None),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed known=p2:0L dealer=seen", None),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=aim:p1,med", None),
        ("p1=2/2[cuff] p2=2/2 p3=1/2 tube=1L1B turn=p1 restraintused dir=ccw "
         "cuffed=p2 skipped=p3 known=p1:0L known=p3:1B", None),
    ):
        got = print_position(parse_position(given, 0))
        check_true(f"round trip: {got[:40]}", got == (want or given), got)

    refusals = (
        ("p1=2/2[mg,mg,mg,mg,mg,mg,mg,mg,mg] p2=2/2 tube=1L1B turn=p1", MSG_TOO_MANY_ITEMS),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2", MSG_BAD_PHONED),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@x", MSG_BAD_PHONED),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@5 phoned=p2@4", MSG_PHONED_TWICE.format(2)),
        ("p1=2/2 p2=2/2 tube=0L1B turn=p1 phoned=p2@1", MSG_READ_SIZE),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@9", MSG_READ_SIZE),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@3", MSG_READ_TOO_SMALL),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p1 phoned=p2@8,8,8,8,8,8,8,8,8", MSG_TOO_MANY_READS),
        ("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p1 listcigs", MSG_TWO_SEATS_ONLY),
        ("p1=2/2 p2=2/2 p3=2/2 tube=1L1B turn=p2 dealer=med", MSG_TWO_SEATS_ONLY),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=foo", MSG_BAD_DEALER),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=believes:L", MSG_BELIEVES_LIVE),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 known=p2:0L dealer=seen,aim:p1", MSG_DEALER_CLASH),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=med dealer=med", MSG_DEALER_TWICE),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p1 dealer=med", MSG_DEALER_NOT_TO_MOVE),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 cuffed=p2 dealer=med", MSG_CUFFED_MID_TURN),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=seen", MSG_SEEN_NOT_KNOWN),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 known=p1:0L dealer=seen", MSG_SEEN_NOT_KNOWN),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=aim:p1", MSG_AIM_P1_UNSAWED),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=aim:self", MSG_SAW_MEMORY_CLASH),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=believes:B", MSG_SAW_MEMORY_CLASH),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed known=p2:0B dealer=seen", MSG_SAW_MEMORY_CLASH),
        ("p1=2/2 p2=2/2 tube=0L0B turn=p2 dealer=med", MSG_EMPTY_TUBE),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p1[beer]",
         "items go on the seat, as in p1=2/2[beer], not on turn="),
    )
    for text, want in refusals:
        try:
            parse_position(text, 0)
            got = "accepted"
        except ValueError as exc:
            got = str(exc)
        check_true(f"refused: {want[:40]}", got == want, got)
    try:
        parse_state("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@4", 0)
        got = "accepted"
    except ValueError as exc:
        got = str(exc)
    check_true("bare state refuses phoned", got == MSG_WHOLE_POSITION_ONLY, got)

    for text, seat, mode, opp, want in (
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=med", 0, "don", "optimal", MSG_DEALER_NEEDS_DEALER),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=believes:B", 0, "don", "dealer", MSG_BELIEVES_STORY_ONLY),
        ("p1=2/2 p2=2/2 tube=1L1B turn=p2 dealer=aim:self", 0, "story", "dealer", MSG_AIM_SELF_DON_ONLY),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p1@4", 0, "don", "optimal",
         MSG_PHONED_ADVISED_SEAT.format(1)),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p1 phoned=p2@4", 1, "don", "optimal",
         MSG_PHONED_ADVISED_SEAT.format(2)),
    ):
        try:
            root_starts(parse_position(text, 0), seat, mode, opp)
            got = "solved"
        except Refused as exc:
            got = str(exc)
        check_true(f"not solved: {want[:40]}", got == want, got)


def dealer_selftest(check, check_true) -> None:
    """Checks for the Dealer opponent: the hand-computed values first, then one
    or more checks per rule on single passes of a Dealer turn."""
    tight = 1e-12

    def dealer(pos: str, mode: str = "don"):
        return run(pos, reloads=0, mode=mode, opponent="dealer")

    # D1. p1=1/1 p2=1/1 tube=1L1B turn=p2, no items.  ENDLESS: L == B, fair
    #     coin.  Shoot self (1/2): live (1/2) p2 dies -> 1; blank (1/2) p2 keeps
    #     the gun, one live shell left, the last-shell rule shoots p1 -> 0; so
    #     1/2.  Shoot p1 (1/2): live (1/2) -> 0; blank (1/2) p1 shoots the last
    #     live shell at p2 -> 1; so 1/2.  Value 1/2.  STORY: the same, 1/2.
    for mode in ("don", "story"):
        v, _, acts = dealer("p1=1/1 p2=1/1 tube=1L1B turn=p2", mode)
        check(f"dealer {mode} 1L1B: value", v, 0.5, tight)
        check_true(f"dealer {mode} 1L1B: no actions", acts == [], f"actions={acts!r}")

    # D2. p1=1/1 p2=1/1 tube=2L1B turn=p2.  ENDLESS: L > B, coin 1, shoot p1:
    #     live 2/3 -> 0; blank 1/3 -> p1 fires a live shell at p2 -> 1.  = 1/3.
    #     STORY: fair coin.  Shoot p1 = 1/3 as above.  Shoot self: live 2/3 ->
    #     1; blank 1/3 -> a fresh turn on 2L0B, no deduction in STORY and size
    #     2, fair coin, 1/2.  = 2/3 + 1/6 = 5/6.  Value 1/2 * 1/3 + 1/2 * 5/6 = 7/12.
    v, _, _ = dealer("p1=1/1 p2=1/1 tube=2L1B turn=p2", "don")
    check("dealer don 2L1B: value", v, 1.0 / 3.0, tight)
    v, _, _ = dealer("p1=1/1 p2=1/1 tube=2L1B turn=p2", "story")
    check("dealer story 2L1B: value", v, 7.0 / 12.0, tight)

    # D3. p1=1/1 p2=1/1 tube=1L2B turn=p2 with offsets 1 and 2 known blank to
    #     both seats.  ENDLESS: B' = 2 - 2 = 0, so the chamber is live and p1
    #     is shot -> 0.  STORY: no deduction; the chamber is the only unresolved
    #     shell and it is live; fair coin: self -> 1, p1 -> 0.  = 1/2.
    pos = "p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p1:1B,2B known=p2:1B,2B"
    v, _, _ = dealer(pos, "don")
    check("dealer don both-known blanks: value", v, 0.0, tight)
    v, _, _ = dealer(pos, "story")
    check("dealer story both-known blanks: value", v, 0.5, tight)

    # D4. p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1.  The root skip hands the
    #     turn to p2 with p1 marked skipped: exactly D2 in each brain, and the
    #     Dealer is to move at the root, so no actions.
    v, _, acts = dealer("p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1", "don")
    check("dealer don root skip: value", v, 1.0 / 3.0, tight)
    check_true("dealer don root skip: no actions", acts == [], f"actions={acts!r}")
    v, _, _ = dealer("p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1", "story")
    check("dealer story root skip: value", v, 7.0 / 12.0, tight)

    # D5. p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:1B, ENDLESS.  p1 has not
    #     seen offset 1, so it is redrawn: B (2/3) or L (1/3), the Dealer seeing
    #     it.  B: L' = 1, B' = 1, no deduction; L < B, coin 0, shoot self.  Live
    #     (1/2) -> 1.  Blank (1/2): fresh turn, offset 0 is the seen blank (D1),
    #     shoot self, blank; fresh turn, one live shell, shoot p1 -> 0.  = 1/2.
    #     L: L' = 0, the chamber is blank, shoot self; fresh turn, D1 knows the
    #     live shell, shoot p1 -> 0.  Value 2/3 * 1/2 + 1/3 * 0 = 1/3.
    v, _, _ = dealer("p1=1/1 p2=1/1 tube=1L2B turn=p2 known=p2:1B", "don")
    check("dealer don redrawn memory: value", v, 1.0 / 3.0, tight)

    # D6. p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed.  A blank the Dealer fires into
    #     itself keeps the saw; any other shot clears it.
    #     A fresh turn on 1L1B, sawed, either brain: fair coin.  Self: live (1/2)
    #     2 damage -> 1; blank (1/2) the saw stays and the last live shell takes
    #     p1's 2 charges -> 0; so 1/2.  p1: live (1/2) -> 0; blank (1/2) p1 fires
    #     the last live shell at p2, 2/(2+1) = 2/3; so 1/3.  Together 5/12.
    #     ENDLESS: L < B, coin 0, shoot self.  Live (1/3): 2 damage -> 1.  Blank
    #     (2/3): 5/12.  Value 1/3 + 2/3 * 5/12 = 11/18.
    #     STORY: fair coin.  Self = 11/18 as above.  p1: live (1/3) 2 damage ->
    #     0; blank (2/3) p1 to move on 1L1B with 2 charges each, where either
    #     shot is worth 1/2; so 1/3.  Value 1/2 * 11/18 + 1/2 * 1/3 = 17/36.
    v, _, _ = dealer("p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed", "don")
    check("dealer don sawed root: value", v, 11.0 / 18.0, tight)
    v, _, _ = dealer("p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed", "story")
    check("dealer story sawed root: value", v, 17.0 / 36.0, tight)

    # D7. p1=1/1 p2=1/1 tube=1L1B turn=p2 inverted.  Step 0: the shell drawn
    #     for the chamber is replaced by its complement.  Drawn live (1/2): the
    #     tube is 0L2B, the Dealer shoots itself twice and the budget boundary
    #     gives 1/2.  Drawn blank (1/2): the tube is 2L0B, p1 is shot -> 0.
    #     ENDLESS 1/4.  STORY: 0L2B is worth 1/2 whoever is shot; on 2L0B the
    #     fair coin gives 1/2.  STORY 1/2.
    v, _, _ = dealer("p1=1/1 p2=1/1 tube=1L1B turn=p2 inverted", "don")
    check("dealer don pending inversion: value", v, 0.25, tight)
    v, _, _ = dealer("p1=1/1 p2=1/1 tube=1L1B turn=p2 inverted", "story")
    check("dealer story pending inversion: value", v, 0.5, tight)

    # D8. The root skip under the minimising opponent: p1=1/1 p2=1/1 tube=2L1B
    #     turn=p1 cuffed=p1 is p2 to move; p2 shoots p1 for 1/3 (shooting
    #     itself would give p1 at least 2/3), and the ranking lists p2's moves.
    v, d, _ = run("p1=1/1 p2=1/1 tube=2L1B turn=p1 cuffed=p1", reloads=0)
    check("optimal root skip: value", v, 1.0 / 3.0)
    check("optimal root skip: p2 shoots p1", d.get("shoot p1", -1.0), 1.0 / 3.0)

    # D9. p1=1/1[inv] p2=1/1 tube=0L2B turn=p1 known=p2:0B.  p1 has not seen
    #     offset 0, so it is redrawn from p1's pool 0L2B: one branch, blank, the
    #     Dealer seeing it.  p1 is to move and chooses from the branch mixture.
    #     Shoot self: blank for certain, p1 keeps the gun on one unseen blank,
    #     inverts it and fires it live at p2 -> 1.  Shoot p2: blank, p2 to move
    #     on one blank, the tube empties and budget 0 gives 1/(1+1) = 1/2.
    #     Value max(1, 1/2, ...) = 1, the top row, under either opponent.
    for opp in ("dealer", "optimal"):
        v, d, acts = run("p1=1/1[inv] p2=1/1 tube=0L2B turn=p1 known=p2:0B",
                         reloads=0, opponent=opp)
        check(f"{opp} root pick by p1: value", v, 1.0, tight)
        check(f"{opp} root pick by p1: shoot self", d["shoot self"], 1.0, tight)
        check(f"{opp} root pick by p1: shoot p2", d["shoot p2"], 0.5, tight)
        check_true(f"{opp} root pick by p1: value is the top row",
                   abs(v - acts[0][1]) <= tight, f"top={acts[0]!r}")

    # D10. p1=1/1[mg] p2=1/1 tube=1L1B turn=p1 known=p2:1L.  p1 has not seen
    #     offset 1, so it is redrawn from p1's pool 1L1B, the Dealer seeing it:
    #     L (1/2) leaves a blank chamber, B (1/2) a live one.  To p1 the chamber
    #     is live with probability 1/2 in both branches.
    #     Shoot p2: blank -> p2 knows the last live shell and shoots p1 -> 0;
    #     live -> 1; so 1/2.  Shoot self: blank -> p1 keeps the gun and shoots
    #     the last live shell at p2 -> 1; live -> 0; so 1/2.  Glass: p1 sees the
    #     chamber and plays it right in both branches -> 1.  Value 1.
    #     p1=1/3[mg] p2=1/3 tube=1L0B turn=p1 known=p2:0L: the counts already
    #     tell p1 the chamber is live.  The glass is still offered and changes
    #     nothing: then shoot p2 -> 1.  Value 1.
    for opp in ("dealer", "optimal"):
        v, d, _ = run("p1=1/1[mg] p2=1/1 tube=1L1B turn=p1 known=p2:1L",
                      reloads=0, opponent=opp)
        check(f"{opp} glass past Dealer memory: value", v, 1.0, tight)
        check(f"{opp} glass past Dealer memory: glass", d.get("use Magnifying Glass", -1.0),
              1.0, tight)
        check(f"{opp} glass past Dealer memory: shoot p2", d["shoot p2"], 0.5, tight)
        v, d, _ = run("p1=1/3[mg] p2=1/3 tube=1L0B turn=p1 known=p2:0L",
                      reloads=0, opponent=opp)
        check(f"{opp} glass on a deducible chamber: value", v, 1.0, tight)
        check(f"{opp} glass on a deducible chamber: offered",
              d.get("use Magnifying Glass", -1.0), 1.0, tight)

    # A sawed Dealer that knows a blank fires it into itself, keeps the
    # saw and fires the last live shell at p1 for 2 charges -> 0.
    v, _, _ = dealer("p1=2/2 p2=2/2 tube=1L1B turn=p2 sawed known=p1:0B known=p2:0B")
    check("dealer keeps the saw after its own blank", v, 0.0, tight)

    # ---- single passes ------------------------------------------------
    def first_pass(pos: str, mode: str = "don", skips: bool = False, mem: Memory = FRESH):
        st = parse_state(pos, 0)
        if skips:
            st = skip_cuffed_mover(st)
        solver = Solver(PLAYER, mode, "dealer")
        return solver, solver.dealer_pass(st, mem)

    def mass(outs) -> dict[str, float]:
        out: dict[str, float] = {}
        for p, label, _, _ in outs:
            out[label] = out.get(label, 0.0) + p
        return out

    def check_mass(name: str, outs, want: dict[str, float]) -> None:
        got = mass(outs)
        ok = set(got) == set(want) and all(abs(got[k] - want[k]) < tight for k in want)
        check_true(name, ok, f"{ {k: round(v, 6) for k, v in sorted(got.items())} }")

    def hands(outs) -> set[str]:
        return {" ".join(print_state(s).split()[:2]) for _, _, s, _ in outs}

    # The coin: ENDLESS follows the counts, STORY is fair.
    _, outs = first_pass("p1=1/1 p2=1/1 tube=2L1B turn=p2")
    check_mass("dealer coin: endless L > B shoots p1", outs, {"shoot p1": 1.0})
    _, outs = first_pass("p1=1/1 p2=1/1 tube=1L2B turn=p2")
    check_mass("dealer coin: endless L < B shoots self", outs, {"shoot self": 1.0})
    _, outs = first_pass("p1=1/1 p2=1/1 tube=1L1B turn=p2")
    check_mass("dealer coin: endless tie is fair", outs,
               {"shoot self": 0.5, "shoot p1": 0.5})
    _, outs = first_pass("p1=1/1 p2=1/1 tube=2L1B turn=p2", "story")
    check_mass("dealer coin: story is fair", outs, {"shoot self": 0.5, "shoot p1": 0.5})

    # The scan walks the hands in the order they were dealt.
    _, outs = first_pass("p1=2/2 p2=2/2[beer,mg] tube=1L2B turn=p2")
    check_mass("dealer scan order: Beer first", outs, {"use Beer": 1.0})
    _, outs = first_pass("p1=2/2 p2=2/2[mg,beer] tube=1L2B turn=p2")
    check_mass("dealer scan order: glass first", outs, {"use Magnifying Glass": 1.0})
    _, outs = first_pass("p1=2/2[beer,mg] p2=2/2[adr] tube=1L2B turn=p2")
    check_mass("dealer steal order: Beer", outs, {"steal Beer from p1 and use it": 1.0})
    check_true("dealer steal order: hands", hands(outs) == {"p1=2/2[mg] p2=2/2"},
               f"{hands(outs)}")
    _, outs = first_pass("p1=2/2 p2=2/2[cuff,beer,cuff] tube=1L2B turn=p2")
    check_mass("dealer own copy: Handcuffs", outs, {"use Handcuffs": 1.0})
    check_true("dealer own copy: first one goes",
               {print_state(s) for _, _, s, _ in outs}
               == {"p1=2/2 p2=2/2[beer,cuff] tube=1L2B turn=p2 restraintused cuffed=p1"},
               f"{ {print_state(s) for _, _, s, _ in outs} }")
    _, outs = first_pass("p1=2/2[beer,mg,beer] p2=2/2[adr] tube=1L2B turn=p2")
    check_mass("dealer steals p1's first copy", outs, {"steal Beer from p1 and use it": 1.0})
    check_true("dealer steals p1's first copy: hands",
               hands(outs) == {"p1=2/2[mg,beer] p2=2/2"}, f"{hands(outs)}")

    # A live shell seen through the glass is shot at p1, a blank at the Dealer.
    sv, outs = first_pass("p1=2/2 p2=2/2[mg] tube=1L1B turn=p2")
    check_mass("dealer glass: used first", outs, {"use Magnifying Glass": 1.0})
    ok = all(mass(sv.dealer_pass(s, m)) ==
             {("shoot p1" if s.slots[0][0] == "L" else "shoot self"): 1.0}
             for _, _, s, m in outs)
    check_true("dealer glass: shot follows the shell", ok, "live -> p1, blank -> self")

    # The saw fallback: STORY half saw, half self; ENDLESS with L > B always
    # saws and then shoots p1; ENDLESS with L < B shoots itself.
    _, outs = first_pass("p1=2/2 p2=2/2[saw] tube=1L1B turn=p2", "story")
    check_mass("dealer saw fallback: story", outs, {"use Hand Saw": 0.5, "shoot self": 0.5})
    sv, outs = first_pass("p1=2/2 p2=2/2[saw] tube=2L1B turn=p2")
    check_mass("dealer saw fallback: endless L > B", outs, {"use Hand Saw": 1.0})
    check_mass("dealer saw fallback: then p1", sv.dealer_pass(outs[0][2], outs[0][3]),
               {"shoot p1": 1.0})
    _, outs = first_pass("p1=2/2 p2=2/2[saw] tube=1L2B turn=p2")
    check_mass("dealer saw fallback: endless L < B", outs, {"shoot self": 1.0})
    _, outs = first_pass("p1=2/2[saw] p2=2/2[adr] tube=2L1B turn=p2")
    check_mass("dealer saw fallback: stolen saw", outs,
               {"steal Hand Saw from p1 and use it": 1.0})

    # A Dealer in the middle of its turn.
    believes = Memory(True, "B", DEALER, False)
    _, outs = first_pass("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2", "story", mem=believes)
    check_mass("dealer believes blank: shoots itself", outs, {"shoot self": 1.0})
    _, outs = first_pass("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2", "story")
    check_mass("dealer fresh: saw coin", outs, {"use Hand Saw": 0.5, "shoot self": 0.5})
    check_true("dealer fresh: saw aims at p1",
               all(m.target == PLAYER for _, label, _, m in outs if label == "use Hand Saw"),
               "target p1")
    aim_self = Memory(False, None, DEALER, False)
    _, outs = first_pass("p1=2/2 p2=2/2 tube=2L2B turn=p2", mem=aim_self)
    check_mass("dealer aims at itself", outs, {"shoot self": 1.0})
    _, outs = first_pass("p1=2/2 p2=2/2 tube=2L2B turn=p2")
    check_mass("dealer fresh: coin", outs, {"shoot self": 0.5, "shoot p1": 0.5})
    used_med = Memory(False, None, None, True)
    _, outs = first_pass("p1=2/2 p2=2/3[med,med] tube=2L2B turn=p2", mem=used_med)
    check_mass("dealer medicine already used", outs, {"shoot self": 0.5, "shoot p1": 0.5})
    _, outs = first_pass("p1=2/2 p2=2/3[med,med] tube=2L2B turn=p2")
    check_mass("dealer fresh: medicine", outs, {"use Expired Medicine": 1.0})
    # The same memories reached through dealer= at the root.
    for pos, mode, want in (
        ("p1=2/2 p2=2/2[saw] tube=2L2B turn=p2 dealer=believes:B", "story", believes),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 dealer=aim:self", "don", aim_self),
        ("p1=2/2 p2=2/3[med,med] tube=2L2B turn=p2 dealer=med", "don", used_med),
        ("p1=2/2 p2=2/2 tube=2L2B turn=p2 sawed dealer=aim:p1,med", "don",
         Memory(False, None, PLAYER, True)),
    ):
        starts, _, _ = root_starts(parse_position(pos, 0), 0, mode, "dealer")
        check_true(f"dealer= memory: {pos.split('dealer=')[1]}",
                   len(starts) == 1 and starts[0][2] == want, f"{starts[0][2]}")

    # dealer=seen keeps the chamber past the knowledge limit: five other
    # offsets are over the limit and dropped, the chamber is still drawn.
    pos = parse_position("p1=2/2 p2=2/2 tube=4L4B turn=p2 known=p2:0L,1L,2B,3B,4L,5B "
                         "dealer=seen", 0)
    starts, _, dropped = root_starts(pos, 0, "don", "dealer")
    ok = (dropped and len(starts) == 2
          and sorted((s.slots[0], round(w, 12)) for w, s, _ in starts)
          == [(("B", (1,)), 0.5), (("L", (1,)), 0.5)]
          and all(m == Memory(True, s.slots[0][0], aim(s.slots[0][0]), False)
                  for _, s, m in starts)
          and all(t is None for _, s, _ in starts for t, _ in s.slots[1:]))
    check_true("dealer=seen past the knowledge limit", ok,
               f"dropped={dropped} starts={len(starts)}")
    v, d, nodes, dropped = solve_position(pos, 0, "don", "dealer")
    check_true("dealer=seen past the limit solves", dropped and d == [], f"value={v:.6f}")

    # Beer after the glass showed a blank: the stale target survives.  ENDLESS
    # forgets the shell but keeps the target, STORY keeps both.
    for mode in ("don", "story"):
        sv, outs = first_pass("p1=2/2 p2=2/2[mg,beer] tube=1L2B turn=p2", mode)
        _, _, s, m = next(o for o in outs if o[2].slots[0][0] == "B")
        after = sv.dealer_pass(s, m)
        check_mass(f"dealer {mode} beer after glass: beer", after, {"use Beer": 1.0})
        m2 = after[0][3]
        want = (Memory(True, "B", DEALER, False) if mode == "story"
                else Memory(False, None, DEALER, False))
        check_true(f"dealer {mode} beer after glass: memory", m2 == want, f"{m2}")
        check_mass(f"dealer {mode} beer after glass: stale target",
                   sv.dealer_pass(after[0][2], m2), {"shoot self": 1.0})

    # The last shell in both brains.
    for mode in ("don", "story"):
        _, outs = first_pass("p1=1/1 p2=1/1 tube=1L0B turn=p2", mode)
        check_mass(f"dealer {mode} last shell live", outs, {"shoot p1": 1.0})
        _, outs = first_pass("p1=1/1 p2=1/1 tube=0L1B turn=p2", mode)
        check_mass(f"dealer {mode} last shell blank", outs, {"shoot self": 1.0})

    # Deduction D1, D2, D3 in ENDLESS, and none in STORY.
    cases = (
        ("D1", "p1=2/2 p2=2/2 tube=1L1B turn=p2 known=p2:0L", "shoot p1"),
        ("D2", "p1=2/2 p2=2/2 tube=2L0B turn=p2", "shoot p1"),
        ("D3", "p1=2/2 p2=2/2 tube=2L2B turn=p2 known=p2:1L,2L", "shoot self"),
    )
    for rule, pos, shot in cases:
        _, outs = first_pass(pos, "don")
        check_mass(f"dealer deduction {rule}: endless", outs, {shot: 1.0})
        _, outs = first_pass(pos, "story")
        check_mass(f"dealer deduction {rule}: none in story", outs,
                   {"shoot self": 0.5, "shoot p1": 0.5})

    # The Inverter writes a live shell and moves the counts.
    sv, outs = first_pass("p1=2/2 p2=2/2[mg,inv] tube=1L2B turn=p2")
    _, _, s, m = next(o for o in outs if o[2].slots[0][0] == "B")
    after = sv.dealer_pass(s, m)
    s2, m2 = after[0][2], after[0][3]
    check_true("dealer inverter: known blank made live",
               mass(after) == {"use Inverter": 1.0} and s2.slots[0] == ("L", (DEALER,))
               and (s2.live, s2.blank) == (2, 1) and m2.known == "L" and m2.target == PLAYER,
               f"chamber={s2.slots[0]} counts={s2.live}L{s2.blank}B")
    # STORY keeps "blank" through a Beer, so the Inverter meets a fresh,
    # unresolved chamber: 1L2B after the Beer, live stays (1/3), blank turns
    # live (2/3, the counts become 2L1B).  Either way the chamber is live.
    sv, outs = first_pass("p1=2/2 p2=2/2[mg,beer,inv] tube=1L3B turn=p2", "story")
    _, _, s, m = next(o for o in outs if o[2].slots[0][0] == "B")
    after = sv.dealer_pass(s, m)
    inv = sv.dealer_pass(after[0][2], after[0][3])
    check_true("dealer inverter: unseen chamber",
               mass(inv) == {"use Inverter": 1.0}
               and all(o[2].slots[0] == ("L", (DEALER,)) for o in inv),
               "every branch live and seen by the Dealer")
    check("dealer inverter: unseen chamber moved counts",
          sum(p for p, _, st2, _ in inv if st2.live == 2), 2.0 / 3.0, tight)

    # The Dealer's phone picks offsets 1 to size-1 uniformly, including one it
    # has already seen: offset 1 here, which adds no new reading, with 1/3.
    _, outs = first_pass("p1=2/2 p2=2/2[phone] tube=2L2B turn=p2 known=p1:1B known=p2:1B")
    check_mass("dealer phone: used", outs, {"use Burner Phone": 1.0})
    again = sum(p for p, _, st2, _ in outs
                if sum(1 for _, w in st2.slots if DEALER in w) == 1)
    check("dealer phone: seen offset drawn", again, 1.0 / 3.0, tight)
    _, outs = first_pass("p1=2/2 p2=2/2[phone] tube=1L1B turn=p2")
    check_true("dealer phone: not on two shells", "use Burner Phone" not in mass(outs),
               f"{sorted(mass(outs))}")

    # Expired Medicine: once per turn, never at one charge, not while
    # cigarettes are on the item list.
    sv, outs = first_pass("p1=2/2 p2=2/5[med,med] tube=2L2B turn=p2")
    check_mass("dealer medicine: used", outs, {"use Expired Medicine": 1.0})
    check_true("dealer medicine: once per turn",
               all("use Expired Medicine" not in mass(sv.dealer_pass(s, m))
                   for _, _, s, m in outs), "second copy left unused")
    _, outs = first_pass("p1=2/2 p2=1/4[med] tube=2L2B turn=p2")
    check_true("dealer medicine: not at one charge",
               "use Expired Medicine" not in mass(outs), f"{sorted(mass(outs))}")
    _, outs = first_pass("p1=2/2[cig] p2=3/4[med] tube=2L2B turn=p2")
    check_mass("dealer medicine: p1 cigarettes out of reach", outs,
               {"use Expired Medicine": 1.0})
    # The list the previous pass built is empty, so p1's Cigarettes do not
    # block the medicine, which comes first in the Dealer's hand.
    _, outs = first_pass("p1=2/2[cig] p2=3/4[adr,med] tube=2L2B turn=p2")
    check_mass("dealer medicine: empty stale list", outs, {"use Expired Medicine": 1.0})
    # With p1's Cigarettes on that list, the medicine waits and the Cigarettes
    # are stolen.
    _, outs = first_pass("p1=2/2[cig] p2=3/4[adr,med] tube=2L2B turn=p2 listcigs")
    check_mass("dealer medicine: stealable cigarettes", outs,
               {"steal Cigarettes from p1 and use it": 1.0})

    # The stale list blocks the medicine.
    _, outs = first_pass("p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p2 listcigs")
    check_mass("listcigs blocks medicine", outs, {"shoot self": 0.5, "shoot p1": 0.5})
    check_true("listcigs blocks medicine: shells split",
               sorted(round(p, 12) for p, _, _, _ in outs) == [0.25] * 4, f"{len(outs)}")
    _, outs = first_pass("p1=2/3[cig] p2=2/3[med] tube=2L2B turn=p2")
    check_mass("no listcigs: medicine", outs, {"use Expired Medicine": 1.0})
    check_true("no listcigs: medicine outcomes",
               sorted((s.seats[DEALER].hp, p) for p, _, s, _ in outs) == [(1, 0.5), (3, 0.5)],
               f"{[(s.seats[DEALER].hp, p) for p, _, s, _ in outs]}")
    # A pass with an Adrenaline in hand puts p1's Cigarettes
    # on the list; stealing the last of them takes them off.
    sv, outs = first_pass("p1=2/3[cig] p2=2/3[adr,mg] tube=2L2B turn=p2")
    check_mass("adrenaline pass: glass", outs, {"use Magnifying Glass": 1.0})
    check_true("adrenaline pass: listcigs set", all(s.listcigs for _, _, s, _ in outs),
               "every branch")
    ok = True
    for _, _, s, m in outs:
        nxt = sv.dealer_pass(s, m)
        ok = ok and mass(nxt) == {"steal Cigarettes from p1 and use it": 1.0}
        ok = ok and all(not s2.listcigs and s2.seats[DEALER].hp == 3 for _, _, s2, _ in nxt)
    check_true("stealing the last Cigarettes clears listcigs", ok, "every branch")

    # A stolen item costs an Adrenaline and p1's copy, and acts for p2.
    _, outs = first_pass("p1=2/2[cig] p2=1/2[adr] tube=2L2B turn=p2")
    s = outs[0][2]
    check_true("dealer steal: pays and heals p2",
               s.seats[DEALER].hp == 2 and s.seats[DEALER].items == ()
               and s.seats[PLAYER].items == (), f"p2={s.seats[DEALER]} p1 items={s.seats[PLAYER].items}")

    # The stale item list.  With it empty the Dealer's own medicine comes
    # first.  With p1's Cigarettes on it, the stolen glass spends the last
    # Adrenaline, but p1's Cigarettes stay on the list and keep blocking the
    # medicine.
    _, outs = first_pass("p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2")
    check_mass("dealer empty stale list: medicine", outs, {"use Expired Medicine": 1.0})
    sv, outs = first_pass("p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2 listcigs")
    check_mass("dealer stale list: stolen glass", outs,
               {"steal Magnifying Glass from p1 and use it": 1.0})
    check_true("dealer stale list: still set", all(s.listcigs for _, _, s, _ in outs),
               "every branch")
    check_true("dealer stale list: no medicine after",
               all(set(mass(sv.dealer_pass(s, m))) <= {"shoot self", "shoot p1"}
                   for _, _, s, m in outs), "shoots on pass 2")

    # Handcuffs: used on an uncuffed p1, refused when p1 is cuffed, skipped at
    # the root, or on the last shell.
    _, outs = first_pass("p1=2/2 p2=2/2[cuff] tube=1L1B turn=p2")
    check_true("dealer cuffs: used", mass(outs) == {"use Handcuffs": 1.0}
               and outs[0][2].seats[PLAYER].cuffed, "p1 cuffed")
    for name, pos, skips in (
        ("already cuffed", "p1=2/2 p2=2/2[cuff] tube=2L1B turn=p2 cuffed=p1", False),
        ("skipped at the root", "p1=2/2 p2=2/2[cuff] tube=2L1B turn=p1 cuffed=p1", True),
        ("last shell", "p1=2/2 p2=2/2[cuff] tube=1L0B turn=p2", False),
    ):
        _, outs = first_pass(pos, skips=skips)
        check_true(f"dealer cuffs: refused, {name}", "use Handcuffs" not in mass(outs),
                   f"{sorted(mass(outs))}")

    # A story reload deals 2 items: p1 from pool index 0, p2 from index 1.
    st = parse_state("p1=1 p2=1 tube=0L0B", 1)
    dealt = Solver(PLAYER, "story").reload_branches(st)[0][1]
    check_true("story deal", dealt.seats[0].items == (MG, BEER)
               and dealt.seats[1].items == (BEER, CIG),
               f"p1={dealt.seats[0].items} p2={dealt.seats[1].items}")

    # The flag checks exit 2.
    for name, argv in (
        ("three seats", ["--position", "p1=1 p2=1 p3=1 tube=1L1B"]),
        ("mode mp", ["--position", "p1=1 p2=1 tube=1L1B", "--mode", "mp"]),
        ("seat 2", ["--position", "p1=1 p2=1 tube=1L1B", "--seat", "2"]),
    ):
        code = None
        with contextlib.redirect_stderr(io.StringIO()):
            try:
                main(argv + ["--opponent", "dealer", "--reloads", "0"])
            except SystemExit as exc:
                code = exc.code
        check_true(f"dealer flags: {name} refused", code == 2, f"exit={code}")


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Independent exact Buckshot Roulette solver (oracle).")
    ap.add_argument("--position", help="position notation, e.g. "
                    "\"p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1\"")
    ap.add_argument("--seat", type=int, default=1, help="seat solved for (default 1)")
    ap.add_argument("--reloads", type=int, default=2, help="reload budget (default 2)")
    ap.add_argument("--mode", choices=("don", "story", "mp"), default="don",
                    help="item pool and deal: don (default), story or mp; with "
                         "--opponent dealer, don plays the Double or Nothing "
                         "Dealer and story the story mode Dealer")
    ap.add_argument("--opponent", choices=("optimal", "dealer"), default="optimal",
                    help="optimal (default): every other seat minimises the "
                         "value; dealer: p2 plays the scripted Dealer (two "
                         "seats, --seat 1, mode don or story)")
    ap.add_argument("--heal-floor", type=int, default=1,
                    help="charges below which healing does nothing (default 1)")
    ap.add_argument("--items-per-load", type=int, default=None,
                    help="items dealt to each seat at a reload, 0 to 8 (default: "
                         "4 for don, 2 for story and mp)")
    ap.add_argument("--saw-survives", choices=("yes", "no"), default=None,
                    help="whether a sawed barrel stays sawed through a reload "
                         "(default: yes for don and story, no for mp)")
    ap.add_argument("--json", action="store_true",
                    help="accepted for argv compatibility with the C++ advisor; "
                         "this tool always writes JSON")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.position:
        ap.error("--position is required unless --selftest is given")
    if args.reloads < 0:
        ap.error("--reloads must be >= 0")
    if not 1 <= args.heal_floor <= 8:
        ap.error("--heal-floor must be in 1..8")
    if args.items_per_load is not None and not 0 <= args.items_per_load <= 8:
        ap.error("--items-per-load must be in 0..8")
    global HEAL_FLOOR
    HEAL_FLOOR = args.heal_floor
    saw = None if args.saw_survives is None else args.saw_survives == "yes"

    pos = parse_position(args.position, args.reloads)
    st = pos.state
    if not 1 <= args.seat <= len(st.seats):
        ap.error(f"--seat must be in 1..{len(st.seats)}")
    if args.opponent == "dealer":
        if len(st.seats) != 2:
            ap.error("--opponent dealer needs a position with exactly two seats")
        if args.mode not in BRAINS:
            ap.error("--opponent dealer needs --mode don or story")
        if args.seat != 1:
            ap.error("--opponent dealer solves for p1 only: use --seat 1")
    try:
        value, acts, nodes, dropped = solve_position(
            pos, args.seat - 1, args.mode, args.opponent, args.items_per_load, saw)
    except Refused as exc:
        print(json.dumps({"refused": True, "assumptions": [f"Not solved: {exc}"],
                          "actions": [], "nodeLimitHit": False}))
        print(f"error: Not solved: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({
        "value": value,
        "actions": [{"action": a, "value": v} for a, v in acts],
        "nodes": nodes,
        "nodeLimitHit": False,
        "opponentKnowledgeDropped": dropped,
    }))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(2)
