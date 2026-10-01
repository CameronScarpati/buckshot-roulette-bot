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

# Item pool for "double or nothing" (default), for story mode and for
# multiplayer.
POOL_DON = (MG, BEER, CIG, CUFF, SAW, PHONE, ADR, INV, MED)
POOL_STORY = (MG, BEER, CIG, CUFF, SAW)
POOL_MP = (MG, BEER, CIG, SAW, PHONE, ADR, INV, MED, JAM, REM)
POOLS = {"don": POOL_DON, "story": POOL_STORY, "mp": POOL_MP}

# Double or Nothing draws 1 to 5 items per load; a solved reload takes the
# middle of that range, matching RuleConfig::itemsDealtPerLoad.  Story mode
# deals 2 per load.
ITEMS_PER_LOAD = 3
STORY_ITEMS_PER_LOAD = 2
LOAD_SIZES = {"don": ITEMS_PER_LOAD, "story": STORY_ITEMS_PER_LOAD, "mp": ITEMS_PER_LOAD}
TABLE_LIMIT = 8
# Healing does nothing to a seat holding fewer charges than this. One means it
# always works on a living seat. The third story stage gives four normal charges
# and two faded ones, and a seat past its last normal charge cannot be healed and
# dies to any hit, which is five charges with a floor of two. Mirrors
# RuleConfig::healFloor; set from --heal-floor.
HEAL_FLOOR = 1
# Deterministic deal: seat i (0-based, so p1 is seat 0) takes items starting
# at pool index i + DEAL_INDEX_BASE, cycling through the pool.
DEAL_INDEX_BASE = 0

EPS = 1e-9

# --------------------------------------------------------------------------
# State
# --------------------------------------------------------------------------


class Seat(NamedTuple):
    hp: int
    max_hp: int
    items: tuple[int, ...]   # sorted, so the state key is canonical
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


def flip(t: str) -> str:
    return "B" if t == "L" else "L"


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

# The item scan's type order.  Each side (the Dealer's own items, then p1's
# items when the Dealer holds an Adrenaline) is scanned in this order.
DEALER_SCAN = (MG, CIG, MED, BEER, CUFF, SAW, PHONE, INV)

# Double or Nothing plays the ENDLESS brain, story mode the STORY brain.
BRAINS = {"don": "endless", "story": "story"}


class Memory(NamedTuple):
    """The Dealer's memory within one turn (lines 65-77).  All of it is
    discarded after the shot, so every turn starts from FRESH."""
    knows: bool              # dealerKnowsShell
    known: str | None        # knownShell: 'L', 'B' or None
    target: int | None       # dealerTarget as a seat index (DEALER is itself)
    used_medicine: bool      # usingMedicine
    adr_list: bool | None    # the previous pass's item list included p1's
                             # items; None before the first scan of the turn


FRESH = Memory(knows=False, known=None, target=None, used_medicine=False, adr_list=None)


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


def knowledge_branches(st: State, seat: int, limit: int = 4):
    """The position as `seat` sees it, split into the ways the shells another
    seat has looked at could have fallen.

    Each such position goes back into the unresolved pool as far as this seat
    is concerned, so the weights are draws without replacement from that pool;
    inside a branch the position is pinned again and still carries the seats
    that saw it, so they go on playing as though they know.  Returns the
    branches, how many such shells there were, and whether the limit dropped
    them."""
    blind = blind_to(st, seat)
    spots = foreign_known(st, seat)
    if not spots:
        return [(1.0, blind)], 0, False
    if len(spots) > limit:
        return [(1.0, blind)], len(spots), True
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
            slots[idx] = (ty, tuple(w for w in st.slots[idx][1] if w != seat))
        out.append((p, blind._replace(slots=tuple(slots))))
    return (out or [(1.0, blind)]), len(spots), False


def healed(seat: "Seat", amount: int) -> int:
    """Healing below the floor does nothing at all: the faded band."""
    if seat.hp < HEAL_FLOOR:
        return seat.hp
    return min(seat.max_hp, seat.hp + amount)


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
    seats = list(st.seats)
    i = st.turn
    n = len(seats)
    for _ in range(2 * n + 2):
        i = (i + st.direction) % n
        if seats[i].hp <= 0:
            continue
        if seats[i].cuffed:
            seats[i] = seats[i]._replace(cuffed=False, skipped=True)
            continue
        seats[i] = seats[i]._replace(skipped=False)
        return st._replace(seats=tuple(seats), turn=i, cuffs_used=False)
    return st._replace(seats=tuple(seats), cuffs_used=False)


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


def drop_item(items: tuple[int, ...], it: int) -> tuple[int, ...]:
    lst = list(items)
    lst.remove(it)
    return tuple(lst)


def add_item(items: tuple[int, ...], it: int) -> tuple[int, ...]:
    return tuple(sorted(items + (it,)))


def cuffable(st: State, actor: int) -> list[int]:
    return [i for i, s in enumerate(st.seats)
            if i != actor and s.hp > 0 and not s.cuffed and not s.skipped]


def display(it: int, tgt: int | None) -> str:
    if tgt is None:
        return f"use {ITEM_NAMES[it]}"
    return f"use {ITEM_NAMES[it]} on p{tgt + 1}"


# --------------------------------------------------------------------------
# Solver
# --------------------------------------------------------------------------


class Solver:
    def __init__(self, seat: int, mode: str = "don", opponent: str = "optimal") -> None:
        self.seat = seat                 # 0-based nominated seat
        self.pool = POOLS[mode]
        self.per_load = LOAD_SIZES[mode]
        self.dealer = opponent == "dealer"
        if self.dealer and (seat != PLAYER or mode not in BRAINS):
            raise ValueError("the Dealer opponent needs seat p1 and mode don or story")
        self.brain = BRAINS.get(mode, "endless")
        self.memo: dict[State, float] = {}
        self.dealer_memo: dict[tuple[State, Memory], float] = {}
        self.nodes = 0

    # ---- core -----------------------------------------------------------

    def value(self, st: State) -> float:
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
        seen = blind_to(st, st.turn)
        theirs = self.node_values(seen)
        if not theirs:
            return pick(vals.values())
        edge = pick(theirs.values())
        tied = [text for text, v in theirs.items() if abs(v - edge) <= 1e-12]
        real = [vals[text] for text in tied if text in vals]
        return sum(real) / len(real) if real else pick(vals.values())

    def node_values(self, st: State) -> dict[str, float]:
        """Value of every legal action text (targets of a steal are folded in)."""
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
        """total ~ U{2..8}, then live ~ U{1..total-1}; deal items; cuffs off."""
        out = []
        for total in range(2, 9):
            for live in range(1, total):
                p = (1.0 / 7.0) * (1.0 / (total - 1))
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
                    blank=total - live,
                    inverted=False,
                    sawed=False,
                    turn=0,
                    direction=st.direction,
                    cuffs_used=False,
                    budget=st.budget - 1,
                )
                if st2.seats[0].hp <= 0:            # seat 1 acts first by default
                    st2 = advance(st2._replace(turn=0))
                out.append((p, st2))
        return out

    # ---- moves ----------------------------------------------------------

    def gen_moves(self, st: State):
        a = st.turn
        me = st.seats[a]
        moves = []
        for tgt in range(len(st.seats)):
            if tgt != a and st.seats[tgt].hp <= 0:
                continue
            text = "shoot self" if tgt == a else f"shoot p{tgt + 1}"
            moves.append((text, self.shoot(st, tgt)))
        for it in sorted(set(me.items)):
            if it == ADR:
                continue
            if not self.allowed(st, a, it):
                continue
            base = with_seat(st, a, items=drop_item(me.items, it))
            for tgt in self.targets(base, a, it):
                moves.append((display(it, tgt), self.use(base, a, it, tgt)))
        if ADR in me.items:
            for v, vs in enumerate(st.seats):
                if v == a or vs.hp <= 0:
                    continue
                for it in sorted(set(vs.items)):
                    if it == ADR:
                        continue
                    if a != self.seat and it in (MG, PHONE):
                        continue
                    base = with_seat(st, a, items=drop_item(me.items, ADR))
                    base = with_seat(base, v, items=drop_item(vs.items, it))
                    if not self.allowed(base, a, it):
                        continue
                    text = f"steal {ITEM_NAMES[it]} from p{v + 1} and use it"
                    for tgt in self.targets(base, a, it):
                        moves.append((text, self.use(base, a, it, tgt)))
        return moves

    def allowed(self, st: State, a: int, it: int) -> bool:
        """Legal-move filtering, exactly as specified."""
        me = st.seats[a]
        n = len(st.slots)
        if a != self.seat and it in (MG, PHONE):
            return False                  # opponents never read shells
        if it == MG:
            # Decided on what this seat knows: a shell only another seat has
            # seen goes back into its pool, so it neither settles the chamber
            # nor tips the counts.
            mine = blind_to(st, a)
            if mine.slots[0][0] is not None:
                return False
            ul, ub = unresolved_counts(mine)
            return ul > 0 and ub > 0
        if it == CIG:
            return HEAL_FLOOR <= me.hp < me.max_hp
        if it == SAW:
            return (not st.sawed) and n > 0
        if it in (CUFF, JAM):
            return (not st.cuffs_used) and bool(cuffable(st, a))
        if it == PHONE:
            return n >= 2 and any(a not in st.slots[i][1] for i in range(1, n))
        if it in (BEER, INV):
            return n > 0
        if it == MED:
            return True
        if it == REM:
            return sum(1 for s in st.seats if s.hp > 0) >= 3
        return False

    def targets(self, st: State, a: int, it: int) -> list[int | None]:
        if it in (CUFF, JAM):
            return list(cuffable(st, a))
        return [None]

    # ---- effects --------------------------------------------------------

    def shoot(self, st: State, tgt: int):
        out = []
        for p, s1, ty in resolve(st, 0):
            eff = flip(ty) if s1.inverted else ty
            s2 = pop_chamber(s1, ty)
            if eff == "L":
                dmg = 2 if s2.sawed else 1
                s2 = with_seat(s2, tgt, hp=max(0, s2.seats[tgt].hp - dmg))
            s2 = s2._replace(sawed=False)       # the saw is consumed either way
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
            cand = [i for i in range(1, len(st.slots)) if a not in st.slots[i][1]]
            out = []
            for i in cand:
                for p, s1, _ in resolve(st, i, viewer=a):
                    out.append((p / len(cand), finish(s1, False)))
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
        None and the state is the position the shot left."""
        out = []
        for p0, s0 in self.dealer_uninvert(st):
            for p1, s1, m1 in self.dealer_read(s0, mem):
                for p2, label, s2, m2 in self.dealer_act(s1, m1):
                    out.append((p0 * p1 * p2, label, s2, m2))
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
        """An own item costs one copy; a stolen one costs one of the Dealer's
        Adrenalines and p1's copy (lines 243-256)."""
        if not stolen:
            return with_seat(st, DEALER, items=drop_item(st.seats[DEALER].items, it))
        st = with_seat(st, DEALER, items=drop_item(st.seats[DEALER].items, ADR))
        return with_seat(st, PLAYER, items=drop_item(st.seats[PLAYER].items, it))

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
        # The scan reads the item list the previous pass built (lines 113-116):
        # p1's items are on it when the Dealer then held an Adrenaline, and a
        # steal does not take them off.  The first pass of a turn uses the
        # Dealer's current Adrenaline instead.
        adr_list = has_adr if mem.adr_list is None else mem.adr_list
        has_cigs = CIG in me.items or (CIG in you.items and adr_list)
        mem = mem._replace(adr_list=has_adr)

        for stolen, holder in ((False, me), (True, you)):
            if stolen and not has_adr:
                break
            for it in DEALER_SCAN:
                if it in holder.items and self.dealer_wants(st, mem, it, has_cigs):
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

    def analyze(self, st: State):
        v = self.value(st)
        acts: list[tuple[str, float]] = []
        if self.dealer and st.turn == DEALER:
            return v, acts, self.nodes    # the Dealer plays by its script
        if st.seats[self.seat].hp > 0 and st.slots and \
                sum(1 for s in st.seats if s.hp > 0) > 1:
            acts = sorted(self.node_values(st).items(), key=lambda kv: (-kv[1], kv[0]))
        return v, acts, self.nodes


# --------------------------------------------------------------------------
# Position notation
# --------------------------------------------------------------------------

SEAT_RE = re.compile(r"^p(\d+)=(\d+)(?:/(\d+))?(?:\[([^\]]*)\])?$")
TURN_RE = re.compile(r"^turn=p(\d+)(?:\[([^\]]*)\])?$")
TUBE_RE = re.compile(r"^tube=(\d+)l(\d+)b$")
KNOWN_RE = re.compile(r"^known=p(\d+):(.+)$")
SHELL_RE = re.compile(r"^(\d+)([lb])$")


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


def parse_position(text: str, budget: int) -> State:
    hp: dict[int, int] = {}
    mx: dict[int, int] = {}
    items: dict[int, list[int]] = {}
    tube = None
    turn = 1
    cuffs: set[int] = set()
    sawed = inverted = False
    direction = 1
    known: list[tuple[int, int, str]] = []

    for tok in text.split():
        low = tok.lower()
        if low == "sawed":
            sawed = True
            continue
        if low == "inverted":
            inverted = True
            continue
        if low.startswith("dir="):
            d = low[4:]
            if d not in ("cw", "ccw"):
                raise ValueError(f"bad direction {tok!r}")
            direction = 1 if d == "cw" else -1
            continue
        m = TURN_RE.match(low)
        if m:
            turn = int(m.group(1))
            items.setdefault(turn, []).extend(parse_items(m.group(2)))
            continue
        if low.startswith("cuffed="):
            for part in low[7:].split(","):
                part = part.strip()
                if part:
                    if not part.startswith("p"):
                        raise ValueError(f"bad cuffed target {part!r}")
                    cuffs.add(int(part[1:]))
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
    if turn not in hp or hp[turn] <= 0:
        raise ValueError("turn must name a living seat")

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

    seats = []
    for i in range(1, n + 1):
        it = tuple(sorted(items.get(i, [])))
        if len(it) > TABLE_LIMIT:
            raise ValueError(f"p{i} holds more than {TABLE_LIMIT} items")
        if mx[i] < hp[i] or mx[i] < 1:
            raise ValueError(f"p{i} has a bad charge count")
        seats.append(Seat(hp=hp[i], max_hp=mx[i], items=it,
                          cuffed=(i in cuffs), skipped=False))
    return State(
        seats=tuple(seats), slots=tuple(slots), live=live, blank=blank,
        inverted=inverted, sawed=sawed, turn=turn - 1, direction=direction,
        cuffs_used=False, budget=budget,
    )


# --------------------------------------------------------------------------
# Self-test.  Every expected value below is computed by hand; the arithmetic
# is written out in the comment above each case.
# --------------------------------------------------------------------------


def solve_position(st: State, seat: int, mode: str = "don", opponent: str = "optimal"):
    """Solve `st` for the 0-based `seat`, averaging over the shells only another
    seat has looked at.

    Every branch is the same position to this seat, so the ranking is one list
    whose rows are averaged over them.  This is the only entry point that may
    be handed a raw parsed position: the solver itself reads a state as the
    truth, so anything that reaches it must already have been cut down to one
    seat's information.

    When the solved seat is to move, its information state is exactly this
    weighted mixture of branches, so it picks the best averaged row and that
    row is the value.  Every other root value is the weighted average of the
    branch values.

    A cuffed seat to move is skipped first.  The skips read no shell, so they
    are the same in every branch.  Under the Dealer opponent a branch where
    p2 is to move is the start of a Dealer turn and lists no actions."""
    if opponent == "dealer" and len(st.seats) != 2:
        raise ValueError("the Dealer opponent needs exactly two seats")
    solver = Solver(seat, mode, opponent)
    st = skip_cuffed_mover(st)
    branches, _, _ = knowledge_branches(st, seat)
    value = 0.0
    rows: dict[str, float] = {}
    for weight, branch in branches:
        v, acts, _ = solver.analyze(branch)
        value += weight * v
        for text, av in acts:
            rows[text] = rows.get(text, 0.0) + weight * av
    order = sorted(rows.items(), key=lambda kv: (-kv[1], kv[0]))
    if order and st.turn == seat:
        # Inside one branch the solver would choose from a fully blinded copy
        # of that branch, which is a coarser view than the mixture itself.
        value = order[0][1]
    return value, order, solver.nodes


def run(pos: str, seat: int = 1, reloads: int = 2, mode: str = "don",
        opponent: str = "optimal"):
    value, order, _ = solve_position(parse_position(pos, reloads), seat - 1, mode, opponent)
    return value, dict(order), order


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

    # 4. p1=1/2 p2=1/2 tube=1L3B turn=p1[inv].  Chamber live prob 1/4 before the
    #    inverter and 1 - 1/4 = 3/4 after it, so the inverter strictly helps.
    st = parse_position("p1=1/2 p2=1/2 tube=1L3B turn=p1[inv]", 1)
    check("inverter: chamber live before", chamber_live_prob(st), 0.25)
    sv = Solver(0, "don")
    stripped = with_seat(st, 0, items=())
    after = sv.use(stripped, 0, INV, None)[0][1]
    check("inverter: chamber live after", chamber_live_prob(after), 0.75)
    v, d, acts = run("p1=1/2 p2=1/2 tube=1L3B turn=p1[inv]", reloads=1)
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

    # 6. p1=1/1 p2=1/1 tube=1L1B turn=p1[cuff].  Cuff p2, then shoot p2:
    #    1/2 live -> p2 dead -> 1 ; 1/2 blank -> p2 is skipped (cuffs come off)
    #    and p1 fires the remaining certain live shell at p2 -> 1.  = 1.0
    v, d, _ = run("p1=1/1 p2=1/1 tube=1L1B turn=p1[cuff]", reloads=1)
    check("handcuffs: value", v, 1.0)
    check("handcuffs: cuff p2", d["use Handcuffs on p2"], 1.0)

    # 7. p1=1/2 p2=1/1 tube=1L0B turn=p1[med].  Expired Medicine:
    #    1/2 -> hp min(1+2, 2) = 2, then the certain live shell kills p2 -> 1
    #    1/2 -> hp 1-1 = 0, p1 is dead -> 0.  = 0.5 ; shooting p2 is 1.0
    v, d, _ = run("p1=1/2 p2=1/1 tube=1L0B turn=p1[med]", reloads=1)
    check("medicine: value", v, 1.0)
    check("medicine: use it", d["use Expired Medicine"], 0.5)

    # 8. p1=1/1 p2=2/2 tube=1L0B turn=p1[saw].  Saw first, then the certain live
    #    shell deals 2 and ends p2 -> 1.0
    v, d, _ = run("p1=1/1 p2=2/2 tube=1L0B turn=p1[saw]", reloads=1)
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

    dealer_selftest(check, check_true)

    print("\n".join(log))
    print(f"{'ALL PASS' if fails == 0 else str(fails) + ' FAILURE(S)'}"
          f" ({len(log)} checks)")
    return 1 if fails else 0


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

    # D6. p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed.  Every shot clears the saw.
    #     ENDLESS: L < B, coin 0, shoot self.  Live (1/3): 2 damage -> 1.  Blank
    #     (2/3): fresh turn on 1L1B, fair coin.  Self: live (1/2) p2 to 1, p1
    #     empties the tube, 2/(2+1) = 2/3; blank (1/2) the last live shell hits
    #     p1, 1/(1+2) = 1/3; so 1/2.  p1: live (1/2) p1 to 1 and then the tube
    #     empties, 1/3; blank (1/2) p1 hits p2 with the last shell, 2/3; so 1/2.
    #     Value 1/3 + 2/3 * 1/2 = 2/3.
    #     STORY: fair coin.  Self = 1/3 + 2/3 * 1/2 = 2/3 as above.  p1: live
    #     (1/3) 2 damage -> 0; blank (2/3) p1 to move on 1L1B with 2 charges
    #     each, where either shot is worth 1/2; so 1/3.  Value 1/2.
    v, _, _ = dealer("p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed", "don")
    check("dealer don sawed root: value", v, 2.0 / 3.0, tight)
    v, _, _ = dealer("p1=2/2 p2=2/2 tube=1L2B turn=p2 sawed", "story")
    check("dealer story sawed root: value", v, 0.5, tight)

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
    #     is live with probability 1/2 in both branches, so the glass is offered.
    #     Shoot p2: blank -> p2 knows the last live shell and shoots p1 -> 0;
    #     live -> 1; so 1/2.  Shoot self: blank -> p1 keeps the gun and shoots
    #     the last live shell at p2 -> 1; live -> 0; so 1/2.  Glass: p1 sees the
    #     chamber and plays it right in both branches -> 1.  Value 1.
    #     p1=1/3[mg] p2=1/3 tube=1L0B turn=p1 known=p2:0L: the counts already
    #     tell p1 the chamber is live, so the glass is not offered.  Value 1.
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
        check_true(f"{opp} glass on a deducible chamber: not offered",
                   "use Magnifying Glass" not in d, f"rows={sorted(d)!r}")

    # ---- single passes ------------------------------------------------
    def first_pass(pos: str, mode: str = "don", skips: bool = False):
        st = parse_position(pos, 0)
        if skips:
            st = skip_cuffed_mover(st)
        solver = Solver(PLAYER, mode, "dealer")
        return solver, solver.dealer_pass(st, FRESH)

    def mass(outs) -> dict[str, float]:
        out: dict[str, float] = {}
        for p, label, _, _ in outs:
            out[label] = out.get(label, 0.0) + p
        return out

    def check_mass(name: str, outs, want: dict[str, float]) -> None:
        got = mass(outs)
        ok = set(got) == set(want) and all(abs(got[k] - want[k]) < tight for k in want)
        check_true(name, ok, f"{ {k: round(v, 6) for k, v in sorted(got.items())} }")

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

    # Beer after the glass showed a blank: the stale target survives.  ENDLESS
    # forgets the shell but keeps the target, STORY keeps both.
    for mode in ("don", "story"):
        sv, outs = first_pass("p1=2/2 p2=2/2[mg,beer] tube=1L2B turn=p2", mode)
        _, _, s, m = next(o for o in outs if o[2].slots[0][0] == "B")
        after = sv.dealer_pass(s, m)
        check_mass(f"dealer {mode} beer after glass: beer", after, {"use Beer": 1.0})
        m2 = after[0][3]
        want = (Memory(True, "B", DEALER, False, False) if mode == "story"
                else Memory(False, None, DEALER, False, False))
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
    # cigarettes are reachable.
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
    _, outs = first_pass("p1=2/2[cig] p2=3/4[adr,med] tube=2L2B turn=p2")
    check_mass("dealer medicine: stealable cigarettes", outs,
               {"steal Cigarettes from p1 and use it": 1.0})

    # A stolen item costs an Adrenaline and p1's copy, and acts for p2.
    _, outs = first_pass("p1=2/2[cig] p2=1/2[adr] tube=2L2B turn=p2")
    s = outs[0][2]
    check_true("dealer steal: pays and heals p2",
               s.seats[DEALER].hp == 2 and s.seats[DEALER].items == ()
               and s.seats[PLAYER].items == (), f"p2={s.seats[DEALER]} p1 items={s.seats[PLAYER].items}")

    # The stale item list: the stolen glass spends the last Adrenaline, but p1's
    # cigarettes stay on the list and keep blocking the Dealer's own medicine.
    sv, outs = first_pass("p1=2/2[mg,cig] p2=2/4[adr,med] tube=2L2B turn=p2")
    check_mass("dealer stale list: stolen glass", outs,
               {"steal Magnifying Glass from p1 and use it": 1.0})
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
    st = parse_position("p1=1 p2=1 tube=0L0B", 1)
    dealt = Solver(PLAYER, "story").reload_branches(st)[0][1]
    check_true("story deal", dealt.seats[0].items == tuple(sorted((MG, BEER)))
               and dealt.seats[1].items == tuple(sorted((BEER, CIG))),
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
    global HEAL_FLOOR
    HEAL_FLOOR = args.heal_floor

    st = parse_position(args.position, args.reloads)
    if not 1 <= args.seat <= len(st.seats):
        ap.error(f"--seat must be in 1..{len(st.seats)}")
    if args.opponent == "dealer":
        if len(st.seats) != 2:
            ap.error("--opponent dealer needs a position with exactly two seats")
        if args.mode not in BRAINS:
            ap.error("--opponent dealer needs --mode don or story")
        if args.seat != 1:
            ap.error("--opponent dealer solves for p1 only: use --seat 1")
    value, acts, nodes = solve_position(st, args.seat - 1, args.mode, args.opponent)
    print(json.dumps({
        "value": value,
        "actions": [{"action": a, "value": v} for a, v in acts],
        "nodes": nodes,
    }))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(2)
