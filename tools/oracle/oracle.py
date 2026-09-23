#!/usr/bin/env python3
"""Independent exact solver for Buckshot Roulette (differential-testing oracle).

This file is derived from a written rules specification only.  It is NOT a port
of the C++ engine/solver in this repository and deliberately shares no code with
it, so that a disagreement between the two is evidence that one of them is
wrong.  See README.md in this directory.

Value computed: the probability that the nominated seat is the last seat alive
in THIS round, under a minimax model where every other seat plays to minimise
that probability.

Python 3.11, standard library only.
"""

from __future__ import annotations

import argparse
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

# Item pool for "double or nothing" (default) and for multiplayer.
POOL_DON = (MG, BEER, CIG, CUFF, SAW, PHONE, ADR, INV, MED)
POOL_MP = (MG, BEER, CIG, SAW, PHONE, ADR, INV, MED, JAM, REM)
POOLS = {"don": POOL_DON, "mp": POOL_MP}

# Double or Nothing draws 1 to 5 items per load; a solved reload takes the
# middle of that range, matching RuleConfig::itemsDealtPerLoad.
ITEMS_PER_LOAD = 3
TABLE_LIMIT = 8
# Deterministic deal: seat i (1-based) takes items starting at pool index i.
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
    def __init__(self, seat: int, mode: str = "don") -> None:
        self.seat = seat                 # 0-based nominated seat
        self.pool = POOLS[mode]
        self.memo: dict[State, float] = {}
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
                        for k in range(ITEMS_PER_LOAD):
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
            t, seen = st.slots[0]
            if t is not None:
                return a not in seen
            ul, ub = unresolved_counts(st)
            return ul > 0 and ub > 0
        if it == CIG:
            return me.hp < me.max_hp
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
            return [(1.0, finish(with_seat(st, a, hp=min(me.max_hp, me.hp + 1)), False))]
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
            good = with_seat(st, a, hp=min(me.max_hp, me.hp + 2))
            bad = with_seat(st, a, hp=max(0, me.hp - 1))
            return [(0.5, finish(good, False)), (0.5, finish(bad, False))]
        if it == REM:
            return [(1.0, finish(st._replace(direction=-st.direction), False))]
        raise ValueError(f"unusable item {it}")

    # ---- entry point ----------------------------------------------------

    def analyze(self, st: State):
        v = self.value(st)
        acts: list[tuple[str, float]] = []
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


def solve_position(st: State, seat: int, mode: str = "don"):
    """Solve `st` for the 0-based `seat`, averaging over the shells only another
    seat has looked at.

    Every branch is the same position to this seat, so the ranking is one list
    whose rows are averaged over them.  This is the only entry point that may
    be handed a raw parsed position: the solver itself reads a state as the
    truth, so anything that reaches it must already have been cut down to one
    seat's information."""
    solver = Solver(seat, mode)
    branches, _, _ = knowledge_branches(st, seat)
    value = 0.0
    rows: dict[str, float] = {}
    for weight, branch in branches:
        v, acts, _ = solver.analyze(branch)
        value += weight * v
        for text, av in acts:
            rows[text] = rows.get(text, 0.0) + weight * av
    order = sorted(rows.items(), key=lambda kv: (-kv[1], kv[0]))
    return value, order, solver.nodes


def run(pos: str, seat: int = 1, reloads: int = 2, mode: str = "don"):
    value, order, _ = solve_position(parse_position(pos, reloads), seat - 1, mode)
    return value, dict(order), order


def selftest() -> int:
    fails = 0
    log: list[str] = []

    def check(name: str, got: float, want: float) -> None:
        nonlocal fails
        ok = abs(got - want) < 1e-9
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

    print("\n".join(log))
    print(f"{'ALL PASS' if fails == 0 else str(fails) + ' FAILURE(S)'}"
          f" ({len(log)} checks)")
    return 1 if fails else 0


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
    ap.add_argument("--mode", choices=("don", "mp"), default="don")
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

    st = parse_position(args.position, args.reloads)
    if not 1 <= args.seat <= len(st.seats):
        ap.error(f"--seat must be in 1..{len(st.seats)}")
    value, acts, nodes = solve_position(st, args.seat - 1, args.mode)
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
