# oracle.py — independent exact solver (differential-testing oracle)

`oracle.py` computes, exactly, the probability that a nominated seat is the last
seat alive in the current round of Buckshot Roulette. It exists to be compared,
position by position, against the C++ solver in this repository.

**It is an independent implementation.** It was written from a prose rules
specification alone, without reading `engine/` or `solver/`, and it shares no
code with them. That independence is the whole point: if both sides agree on a
position, the agreement is evidence; if they disagree, **one of the two is
wrong**, and the disagreement is worth chasing down rather than papering over.

Python 3.11, standard library only, no third-party packages.

## Running it

```
python3 tools/oracle/oracle.py --position "<notation>" [--seat 1] [--reloads 2] [--mode don|mp]
python3 tools/oracle/oracle.py --selftest
```

It writes one JSON object on stdout:

```
{"value": 0.6666666666666666,
 "actions": [{"action": "shoot p2", "value": 0.666...}, {"action": "shoot self", "value": 0.333...}],
 "nodes": 11}
```

`actions` is sorted best first (highest probability for the solved seat first,
ties broken by action text). `nodes` counts distinct states expanded, i.e. cache
misses, so it is a cost signal and **not** something to compare against the C++.
`--json` is accepted and ignored, so the same argv works for both binaries; note
the C++ advisor also emits a `truncated` field, which this tool does not.

Position notation, tokens in any order, case-insensitive:

```
p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1 cuffed=p2 sawed inverted dir=ccw known=p1:0L,2B
```

`pN=3` means 3 out of 3. Item tokens: `mg beer cig cuff saw phone adr inv med
jam rem`. `known=pN:<offset><L|B>` records that seat N has seen that position;
offset 0 is the chamber. Items may also be attached to the `turn=pN[...]` token.
A bad position exits 2 with a one-line reason on stderr.

`--selftest` runs hand-computed positions (the arithmetic for each is written
out in a comment above it), prints PASS or FAIL per check, and exits non-zero on
any failure.

## Approximations — shared with the C++ on purpose

Two parts of the model are deliberate approximations, made the same way on both
sides so the two can be compared at all. They are not claims about the real game.

* **The item deal is deterministic.** On a reload, seat *i* takes
  `items_per_load` (2) items from the pool in order starting at pool index *i*,
  cycling, subject to the 8-item table limit. The real game deals at random; a
  random deal would make the state space unbounded.
* **The reload boundary is a heuristic.** Recursing into a reload costs one unit
  of `--reloads`. At zero the position is valued at `my_hp / sum of all hp`
  ("charges in hand") instead of being searched further. Values therefore depend
  on the reload budget, and only runs with the *same* `--reloads` are comparable.

The opponent model is also a modelling choice: every other seat plays to
minimise the solved seat's probability, and no other seat ever spends a
Magnifying Glass or a Burner Phone (including via Adrenaline), which keeps the
search from reading shells the solved seat cannot see.

## Where the spec left room, and what this file chose

Genuine disagreements are worth chasing; these are the spots where a
disagreement may be a reading difference rather than a bug. Each is one edit
away in `oracle.py`.

* **Deal start index** (`DEAL_INDEX_BASE = 1`): seat *i* is taken to be the
  1-based seat number, so seat 1 starts at pool index 1. Flip the constant to 0
  to start seat 1 at pool index 0.
* **Inverting an unseen chamber**: the shell keeps its place in the unresolved
  pool and a flag makes it fire as the complement. The chance the chamber fires
  live becomes 1 − p. When the shell is finally drawn, the *drawn* type is what
  leaves the tube and the complement is what fires — the net effect of the
  shell becoming its complement and then being consumed.
* **Jammer and the once-per-turn cuff limit**: "only one pair of handcuffs may
  be applied per turn" is enforced as one cuff application per turn of any kind,
  so Handcuffs and Jammer share the flag.
* **Adrenaline** offers only steals whose stolen item is legally usable right
  now, and the one action text `steal <Item> from pN and use it` covers every
  target of a stolen Handcuffs or Jammer: the reported value is the best (for
  the seat to move) over those targets.
* **Held-but-off-pool items are usable.** `don`/`mp` choose the reload pool; a
  seat that is *given* a Jammer in a `don` position may still use it.
* When `turn` is not the solved seat, the ranking lists the mover's moves,
  still ordered by the solved seat's probability.

## Cost

Search cost is driven by `--reloads`, then by seat count and item count. Two
seats with a handful of items and `--reloads 1` finish in a couple of seconds;
three or four seats carrying items with `--reloads 1` can run for minutes, since
each reload deals fresh items to every seat. Use `--reloads 0` or `1` for wide
positions, and keep both implementations on the same budget.
