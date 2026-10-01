# oracle.py, an independent exact solver used as a differential-testing oracle

`oracle.py` computes, exactly, the probability that a nominated seat is the last
seat alive in the current round of Buckshot Roulette. It exists to be compared,
position by position, against the C++ solver in this repository.

**It is an independent implementation.** It was written from a prose rules
description alone, without reading the C++ engine, solver or command-line
advisor (`engine/`, `solver/`, `cli/`), and it shares no code with them. That
independence is the whole point: if both sides agree on a position, the
agreement is evidence; if they disagree, **one of the two is wrong**, and the
disagreement is worth chasing down rather than papering over.

Python 3.11, standard library only, no third-party packages.

## Running it

```
python3 tools/oracle/oracle.py --position "<notation>" [--seat 1] [--reloads 2] [--mode don|story|mp]
                               [--opponent optimal|dealer] [--heal-floor 1]
python3 tools/oracle/oracle.py --selftest
```

It writes one JSON object on stdout:

```
{"value": 0.6666666666666666,
 "actions": [{"action": "shoot p2", "value": 0.666...}, {"action": "shoot self", "value": 0.333...}],
 "nodes": 11}
```

`actions` is sorted best first (highest probability for the solved seat first,
ties broken by action text). Under `--opponent dealer`, when p2 is to move at the
root, `actions` is `[]` and `value` is the value of the Dealer's turn. `nodes`
counts distinct states expanded (positions and Dealer passes), i.e. cache misses,
so it is a cost signal and **not** something to compare against the C++.
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

`--mode` picks the item pool dealt on a reload: `don` (Double or Nothing, the
default), `story` (Magnifying Glass, Beer, Cigarettes, Handcuffs, Hand Saw) or
`mp` (multiplayer). `--heal-floor` sets the charge count below which healing does
nothing; `--heal-floor 2` gives the third story stage's faded band. `story` deals
2 items per seat at a reload, as the second stage does (the advisor's
`--mode story2`). The third stage deals 4, so `--mode story --heal-floor 2`
matches it (the advisor's `--mode story3`) only while the search does not reach a
reload, for example with `--reloads 0`.

`--selftest` runs hand-computed positions (the arithmetic for each is written
out in a comment above it), prints PASS or FAIL per check, and exits non-zero on
any failure.

## Opponent models

`--opponent optimal` (the default): every other seat plays to minimise the
solved seat's probability, and no other seat ever spends a Magnifying Glass or a
Burner Phone (including via Adrenaline), which keeps the search from reading
shells the solved seat cannot see.

`--opponent dealer`: p2 plays the scripted Dealer of the single-player modes and
p1 is the solved seat. It needs a two-seat position, `--seat 1` and `--mode don`
or `story`; anything else exits 2 with the reason. `don` plays the Double or
Nothing Dealer and `story` the story mode Dealer. The rules follow
`DealerIntelligence.gd` in the third-party decompilation of v2.2.0 hotfix 6, pinned
at commit `34531a4c5e26ec44320c5197e2f678ce1a7b8d00` of
[thecatontheceiling/buckshotroulette](https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L65);
line numbers below are in that file.

Wherever p2 is to move, after any cuff skip and any reload, the search plays a
Dealer turn instead of a minimising choice. A turn is a sequence of passes; each
pass uses one item (and another pass follows) or fires one shot (and the turn is
over). The Dealer carries a small memory through the turn (lines 65 to 77):
whether it knows the chamber, what it knows it to be, whom it means to shoot,
whether it has used Expired Medicine, and whether its last item list included
p1's items. The memory is discarded after the shot, so a blank the Dealer fires
into itself starts a new turn with a fresh memory. One pass:

1. **Pending inversion.** A chamber marked `inverted` in a written position
   becomes an ordinary shell of the type it fires as, with the tube's counts moved to
   match. The search itself never leaves one behind for the Dealer.
2. **Deduction** (Double or Nothing only, lines 96 to 104 and `FigureOutShell`
   from line 282). The Dealer knows the chamber when it has seen it, when the
   tube holds no live or no blank shells, or when the shells it has seen account
   for every live or every blank one. A known live shell is aimed at p1 and a
   known blank at the Dealer itself.
3. **Last shell** (lines 106 to 112). With one shell left the Dealer knows it.
4. **Item scan** (lines 113 to 201). The Dealer's own items, then p1's items if
   the Dealer holds an Adrenaline (a steal costs the Adrenaline and p1's copy),
   each side in the order Magnifying Glass, Cigarettes, Expired Medicine, Beer,
   Handcuffs, Hand Saw, Burner Phone, Inverter. The first item whose condition
   holds is used:
   * Magnifying Glass when it does not know the chamber and more than one shell
     is left;
   * Cigarettes when it is below its maximum charges;
   * Expired Medicine when it is below its maximum, not on one charge, has not
     used one this turn, and no Cigarettes are on its item list (its own, or
     p1's when the list included p1's items);
   * Beer when it does not know the chamber to be live and more than one shell
     is left (Double or Nothing then forgets the chamber but keeps its target;
     story mode keeps both);
   * Handcuffs when p1 is not cuffed, was not just skipped, and more than one
     shell is left;
   * Hand Saw when the barrel is whole and it knows the chamber is live;
   * Burner Phone when more than two shells are left: it reads one of the later
     shells, each with equal probability, including shells already seen;
   * Inverter when it knows the chamber is blank: the chamber becomes live.
5. **Saw fallback** (lines 203 to 215). With nothing used, a Hand Saw in reach,
   a whole barrel and no blank known, the coin decides: saw the barrel and aim
   at p1, or aim at itself.
6. **Shot** (lines 268 to 280). The Dealer shoots its target, or the coin's
   choice when it has none: itself on 0, p1 on 1. The shot is the ordinary shot.

The coin (`CoinFlip`, line 421) is fair in story mode. In Double or Nothing it is
1 when the tube holds more live than blank shells, 0 when it holds fewer, and
fair on a tie.

Dealer memory in a written position: `known=p2:<offset><L|B>` records only that
the Dealer has seen that shell. Unless the same fact is also written for p1, the
root knowledge branches redraw its type from p1's unresolved pool, and the Dealer
remembers the redrawn type. To pin a Dealer memory, write it for both seats
(`known=p1:1B known=p2:1B`).

Approximations in the Dealer model, made the same way on both sides:

* The game scans each side's items in the order they sit on the table; this
  model fixes the type order above. It matters only when two items' conditions
  hold at once.
* On the first pass of a turn the game reads the item list the previous Dealer
  turn left behind; this model uses the Dealer's current Adrenaline in its place.
  Later passes follow the game: a steal that spends the last Adrenaline leaves
  p1's Cigarettes on the list, and they keep blocking Expired Medicine.
* Every shot clears the saw, as in the rest of the model. In the game a blank the
  Dealer fires into itself keeps the barrel sawed; the two differ only for a
  written position with `sawed` and p2 to move.

## Root skips

Before anything is searched, with either opponent, a cuffed seat to move loses
its cuffs, is marked skipped, and the turn passes on, exactly as a skip in play.
So `turn=p1 cuffed=p1` is solved with p2 to move and p1 unable to be cuffed until
its next turn.

## Root value

A shell another seat has seen and the solved seat has not is redrawn from the
solved seat's unresolved pool, one root knowledge branch per way it could have
fallen, and each ranked row is that move's value averaged over the branches. When
the solved seat is to move at the root, those branches are exactly what it knows,
so `value` is the best averaged row (the first entry of `actions`). Otherwise
`value` is the average of the branch values.

The solved seat's legal moves also come from what it knows, at the root and at
every later node. A Magnifying Glass is offered only while the chamber's type is
uncertain to the seat using it: a shell only another seat has seen counts as
unresolved, so it neither settles the chamber nor tips the counts. With
`known=p2:1L` on a 1L1B tube the glass is offered (p1 still faces even odds),
and with `known=p2:0L` on a 1L0B tube it is not (the counts already say live).

## Approximations, shared with the C++ on purpose

Two parts of the model are deliberate approximations, made the same way on both
sides so the two can be compared at all. They are not claims about the real game.

* **The item deal is deterministic.** On a reload, seat *i* (0-based, so p1 is
  seat 0) takes a fixed number of items from the pool in order starting at pool
  index *i*, cycling, subject to the 8-item table limit: 3 per load for `don`
  and `mp` (the middle of Double or Nothing's 1 to 5), 2 for `story`. The real
  game deals at random; a random deal would make the state space unbounded.
* **The reload boundary is a heuristic.** Recursing into a reload costs one unit
  of `--reloads`. At zero the position is valued at `my_hp / sum of all hp`
  ("charges in hand") instead of being searched further. Values therefore depend
  on the reload budget, and only runs with the *same* `--reloads` are comparable.

Both opponent models are modelling choices too; see "Opponent models".

## Where the spec left room, and what this file chose

Genuine disagreements are worth chasing; these are the spots where a
disagreement may be a reading difference rather than a bug. Each is one edit
away in `oracle.py`.

* **Deal start index** (`DEAL_INDEX_BASE = 0`): seat *i* is the 0-based seat
  index, so p1 starts at pool index 0 and p2 at pool index 1 (in `story`, p1 is
  dealt Magnifying Glass and Beer, p2 Beer and Cigarettes). Set the constant to 1
  to shift every seat one place along the pool.
* **Inverting an unseen chamber**: the shell keeps its place in the unresolved
  pool and a flag makes it fire as the complement. The chance the chamber fires
  live becomes 1 − p. When the shell is finally drawn, the *drawn* type is what
  leaves the tube and the complement is what fires, which is the net effect of the
  shell becoming its complement and then being consumed.
* **Jammer and the once-per-turn cuff limit**: "only one pair of handcuffs may
  be applied per turn" is enforced as one cuff application per turn of any kind,
  so Handcuffs and Jammer share the flag.
* **Adrenaline** offers only steals whose stolen item is legally usable right
  now, and the one action text `steal <Item> from pN and use it` covers every
  target of a stolen Handcuffs or Jammer: the reported value is the best (for
  the seat to move) over those targets.
* **Held-but-off-pool items are usable.** `don`/`story`/`mp` choose the reload
  pool; a seat that is *given* a Jammer in a `don` position may still use it.
* When `turn` is not the solved seat, the ranking lists the mover's moves,
  still ordered by the solved seat's probability (under `--opponent dealer` the
  Dealer lists none).
* **A written `inverted` chamber that is also written as seen**, with the Dealer
  to move: the Dealer model turns it into its complement as it does an unseen
  one, and the seats that saw it keep their observation of the new type (as an
  Inverter used on a seen chamber does elsewhere in this file).

## Cost

Search cost is driven by `--reloads`, then by seat count and item count. Two
seats with a handful of items and `--reloads 1` finish in a couple of seconds;
three or four seats carrying items with `--reloads 1` can run for minutes, since
each reload deals fresh items to every seat. Use `--reloads 0` or `1` for wide
positions, and keep both implementations on the same budget. Under
`--opponent dealer` p2's turn follows a script, so p2 adds chance branches (its
coin, its phone, its medicine) but no choice to the search.
