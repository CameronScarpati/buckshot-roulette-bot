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
                               [--items-per-load N] [--saw-survives yes|no]
python3 tools/oracle/oracle.py --selftest
```

It writes one JSON object on stdout:

```
{"value": 0.6666666666666666,
 "actions": [{"action": "shoot p2", "value": 0.666...}, {"action": "shoot self", "value": 0.333...}],
 "nodes": 11, "nodeLimitHit": false, "opponentKnowledgeDropped": false}
```

`actions` is sorted best first (highest probability for the solved seat first,
ties broken by action text). Under `--opponent dealer`, when p2 is to move at the
root, `actions` is `[]` and `value` is the value of the Dealer's turn. `nodes`
counts distinct states expanded (positions and Dealer passes), i.e. cache misses,
so it is a cost signal and **not** something to compare against the C++.
`nodeLimitHit` is always `false`: this tool has no node limit, and the field is
there so both tools write the same keys. `opponentKnowledgeDropped` is `true`
when, under the minimising opponent, the root had more shells seen by other
seats than it averages over (see "Root value") and forgot them. Under
`--opponent dealer` it is always `false`.
`--json` is accepted and ignored, so the same argv works for both binaries; note
the C++ advisor also emits a `truncated` field, which this tool does not.

A position that parses but cannot be solved under the flags given (a `dealer=`
token without `--opponent dealer`, a Dealer memory the chosen brain never
reaches, a `phoned=` read for the seat being advised, or a Dealer read at two
shells or fewer under `--opponent dealer`) exits 2 and writes
`{"refused": true, "assumptions": ["Not solved: <reason>"], "actions": [], "nodeLimitHit": false}`
on stdout, with the same reason on stderr.

Position notation, tokens in any order, case-insensitive:

```
p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1 cuffed=p2 sawed inverted dir=ccw known=p1:0L,2B
```

`pN=3` means 3 out of 3. Item tokens: `mg beer cig cuff saw phone adr inv med
jam rem`. A seat lists its items in the order it received them, oldest first,
and holds at most 8. `known=pN:<offset><L|B>` records that seat N has seen that
position; offset 0 is the chamber. Items go on the seat token: `turn=p1[beer]`
is refused with `items go on the seat, as in p1=2/2[beer], not on turn=`.
A bad position exits 2 with a one-line reason on stderr.

The other tokens:

* `restraintused`: a pair of Handcuffs (or a Jammer) was already applied this
  turn. `skipped=pN`: seat N was just skipped by its cuffs and cannot be cuffed
  again until its next turn. A restrained seat loses one turn and is freed when
  its next turn starts (`RoundManager.gd` 308-325), so a seat named in both
  `cuffed=` and `skipped=` is refused, and so is `skipped=` on the seat to move
  while two or more seats are alive.
* `listcigs` (two seats only): the item list the Dealer built on its last pass
  holds p1's Cigarettes. See "Opponent models".
* `phoned=pS@n[,n...]`: seat S used a Burner Phone at each of these tube sizes
  this load, and the seat being advised never learned what it read. Each size is
  2 to 8 and at least the current tube size; at most 8 sizes per seat and one
  `phoned=` per seat. A read whose shell has since left the tube names nothing.
  Naming the seat being advised is refused at solve time: it saw its own read,
  so write it as `known=` instead. Under `--opponent dealer` a read by p2, the
  Dealer, is 3 to 8: the Dealer uses a Burner Phone, its own or a stolen one,
  only with more than two shells in the tube (`DealerIntelligence.gd` 187), so
  a smaller size is refused at solve time.
* `dealer=<core>[,med]` or `dealer=med` (two seats, p2 to move, p2 not cuffed,
  shells in the tube): the Dealer is in the middle of its turn with this
  memory. See "Dealer memory in a written position".

The tool prints positions in a fixed order: seats, `tube=`, `turn=`, `sawed`,
`inverted`, `restraintused`, `dir=ccw`, `cuffed=`, `skipped=`, `known=` by seat,
`listcigs`, `phoned=` by seat with sizes largest first, then `dealer=`.

Move texts: `shoot self`, `shoot p2`, `use Beer`, `use Handcuffs on p2`,
`use Adrenaline`, `steal Beer from p2 and use it`. A seat's items form runs of
equal neighbours, and each run is one move. When a seat holds two or more runs
of one item, each run is named by the position of its first copy among that
item's copies: `[beer,mg,beer]` offers `use Beer #1` and `use Beer #2`, while
`[beer,beer,mg]` offers one `use Beer`. A move takes the first copy of its run.

`--mode` picks the item pool dealt on a reload: `don` (Double or Nothing, the
default), `story` (Magnifying Glass, Beer, Cigarettes, Handcuffs, Hand Saw) or
`mp` (multiplayer). `--heal-floor` sets the charge count below which healing does
nothing; `--heal-floor 2` gives the third story stage's faded band, where a shot
that hits a seat at or above the floor leaves it at least one charge (a failed
Expired Medicine dose is not a shot and is subtracted in full). `story` deals 2
items per seat at a reload, as the second stage does (the advisor's
`--mode story2`). The third stage deals 4, so `--mode story --heal-floor 2`
matches it (the advisor's `--mode story3`) only while the search does not reach a
reload, for example with `--reloads 0`.

`--items-per-load N` sets the items each seat is dealt at a reload (0 to 8),
for every mode. `--saw-survives yes|no` overrides whether a sawed barrel stays
sawed through a reload.

`--selftest` runs hand-computed positions (the arithmetic for each is written
out in a comment above it), prints PASS or FAIL per check, and exits non-zero on
any failure.

## Comparing against the C++

`tools/compare_solvers.py` runs the advisor and this tool on the same positions
and fails when a value or a row differs by more than `--tolerance` (1e-9 by
default) or the two move lists differ:

```
python3 tools/compare_solvers.py --advisor build/advisor [--opponent solver|dealer] [--mode don|story]
       [--reloads 0] [--heal-floor 1] [--random N --seed S] [--max-items K] [--max-shells K]
       [--items-per-load N] [--node-limit N] [--jobs N] [--quiet]
```

It checks a fixed list of positions (one list per opponent model) and `--random`
more drawn from `--seed`. Random hands are drawn in a random order, with at most
`--max-items` items per seat (2 by default, 3 under `--opponent dealer`), and
random tubes hold at most `--max-shells` shells. A fifth of the random positions
carry a `phoned=` read for the seat not being advised, made at 3 shells or more
when that seat is p2, which sits where the Dealer does; under `--opponent dealer`
a quarter of the Double or Nothing positions where p1 holds Cigarettes, and a
tenth of the others, carry `listcigs`, and a quarter of those with an uncuffed
p2 to move carry a `dealer=` memory the Dealer can have. `--items-per-load` goes
to both tools; `--node-limit` goes to the advisor only, and a position it stops
at is printed as `SKIP <position>` and counted apart. `--jobs` checks that many
positions at once and keeps the report in order. A position both tools refuse
to solve (a `phoned=` read by the seat being advised, for example) agrees; one
refused by only one of them disagrees. The last line reads `A of B positions
agree, S skipped`, and the exit status is non-zero only when a position
disagrees.

## Opponent models

`--opponent optimal` (the default): every other seat plays to minimise the
solved seat's probability, and no other seat ever spends a Magnifying Glass or a
Burner Phone (including via Adrenaline), which keeps the search from reading
shells the solved seat cannot see. This model ignores hand order and the
`listcigs` flag: it solves the position with every hand sorted and the flag off.

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
and whether it has used Expired Medicine. The memory is discarded after the
shot, so a blank the Dealer fires into itself starts a new turn with a fresh
memory. One pass:

1. **Pending inversion.** A chamber marked `inverted` in a written position
   becomes an ordinary shell of the type it fires as, with the tube's counts moved to
   match. The search itself never leaves one behind for the Dealer.
2. **Deduction** (Double or Nothing only, lines 96 to 104 and `FigureOutShell`
   from line 282). The Dealer knows the chamber when it has seen it, when the
   tube holds no live or no blank shells, or when the shells it has seen account
   for every live or every blank one. A known live shell is aimed at p1 and a
   known blank at the Dealer itself.
3. **Last shell** (lines 106 to 112). With one shell left the Dealer knows it.
4. **Item scan** (lines 113 to 201). The Dealer walks its own items in the order
   they sit in its hand, then, if it holds an Adrenaline, p1's items in the
   order they sit in p1's hand. Adrenaline, Jammer and Remote never match. The
   first item whose condition holds is used; its own item costs its first copy
   of that item, and a steal costs its first Adrenaline and p1's first copy:
   * Magnifying Glass when it does not know the chamber and more than one shell
     is left;
   * Cigarettes when it is below its maximum charges;
   * Expired Medicine when it is below its maximum, not on one charge, has not
     used one this turn, and no Cigarettes are on its item list: it holds none,
     and the list its previous pass built (the `listcigs` flag) holds none of
     p1's;
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

At the end of every pass the `listcigs` flag is rewritten: it is set when the
Dealer held an Adrenaline at the start of the pass and p1 holds Cigarettes after
it, and cleared otherwise. The flag outlives the Dealer's turn: p1's moves and
reloads leave it as it is, and the Dealer's next turn reads it on its first pass.

The coin (`CoinFlip`, line 421) is fair in story mode. In Double or Nothing it is
1 when the tube holds more live than blank shells, 0 when it holds fewer, and
fair on a tie.

## Dealer memory in a written position

`known=p2:<offset><L|B>` records only that the Dealer has seen that shell.
Unless the same fact is also written for p1, the root knowledge branches redraw
its type from p1's unresolved pool, and the Dealer remembers the redrawn type.
Every such shell is redrawn, however many there are, since the Dealer acts on
all it has seen (lines 187 to 191 and `FigureOutShell` from line 282).
To pin a Dealer memory, write it for both seats (`known=p1:1B known=p2:1B`).

`dealer=` puts the root in the middle of a Dealer turn. The core is one of:

* `seen`: the Dealer knows the chamber, and aims at p1 if it is live and at
  itself if it is blank. It needs `known=p2:0L` or `known=p2:0B`; unless p1 has
  seen the chamber too, it is redrawn like any other shell p2 has seen, and the
  memory follows each draw. With `sawed` the chamber is live and every seat
  knows it, since the Dealer saws a chamber it has seen only when that chamber
  is live (lines 181 and 203 to 215); it is not redrawn.
* `believes:B`: the Dealer drank a Beer on a blank it had seen and still
  believes the chamber blank, aiming at itself. Story mode only.
* `aim:self`: the Double or Nothing Dealer forgot the chamber after that Beer
  but still aims at itself. Double or Nothing only.
* `aim:p1`: the coin sawed the barrel and aimed at p1. It needs `sawed`.

`,med` after the core, or `dealer=med` alone, records that it has used Expired
Medicine this turn. The Dealer saws only when it aims at p1, so `sawed` goes
only with `aim:p1`, with `seen` on a live chamber, or with `med` alone. A
`believes:B` under `--mode don`, an `aim:self` under `--mode story`, or any
`dealer=` without `--opponent dealer` is refused at solve time.

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

A `phoned=` read adds the shells it may have named: each way the read could have
fallen is weighted by the phone's own odds, and the shell it names is redrawn
and marked seen by the reading seat. Under `--opponent dealer` the root redraws
every shell another seat has seen or may have read, however many there are.
Under the minimising opponent it averages over at most 4 of them, counting both
kinds; with more, it forgets every shell other seats have seen and solves the
position as the solved seat's own knowledge describes it, and
`opponentKnowledgeDropped` is `true`. The limit belongs to the
minimising model, which the game's scripts do not describe.

The solved seat's legal moves come from what it knows, at the root and at every
later node. When the seat to move cannot see a shell another seat has resolved,
it chooses from its own information state: moves it cannot tell apart are
equally likely, and the copies of one item count as one choice, valued at the
copy that is best for the mover.

## The phone

A seat's Burner Phone reads one of the later shells. The player's phone
(p1, and every seat in `mp`) reads offsets 1 to n-1 with equal odds, except that
on an 8-shell tube offset 7 shows offset 6, so offset 6 is read twice as often.
In a two-seat `don` or `story` game p2's phone follows the Dealer's rule, every
offset with equal odds, under both opponent models. On the last shell the phone
is spent and reads nothing.

## Legal moves

Every use the game allows is offered, including uses that do nothing:
Cigarettes at full charge, a Magnifying Glass on a chamber the seat already
knows, a Burner Phone on the last shell. The only refusals are a Hand Saw on a
sawed barrel, Handcuffs or a Jammer when one was already applied this turn or no
seat can be restrained, Handcuffs in `mp` or with more than two seats, Jammer
and Remote outside `mp` (Remote needs three seats alive), and the information
rule of the optimal model above. `use Adrenaline` spends the Adrenaline on its own and
leaves the turn as it is. An Adrenaline steals any run of another seat's items
except Adrenaline, provided the thief could use the item itself.

## Approximations, shared with the C++ on purpose

Two parts of the model are deliberate approximations, made the same way on both
sides so the two can be compared at all. They are not claims about the real game.

* **The item deal is deterministic.** On a reload, seat *i* (0-based, so p1 is
  seat 0) takes a fixed number of items from the pool in order starting at pool
  index *i*, cycling, and appends them to its hand, stopping at the 8-item table
  limit: 4 per load for `don` (the game deals 2 to 5) and 2 for `story` and
  `mp`. The real game deals at random; a random deal would make the state space
  unbounded.
* **The reload boundary is a heuristic.** Recursing into a reload costs one unit
  of `--reloads`. At zero the position is valued at `my_hp / sum of all hp`
  ("charges in hand") instead of being searched further. Values therefore depend
  on the reload budget, and only runs with the *same* `--reloads` are comparable.

At a reload the shells are drawn from the mode's distribution (in `don`, seven
compositions from 1L1B to 4L4B, each 1/7), cuffs and skips are cleared, p1
moves first, the `listcigs` flag is kept, and a sawed barrel stays sawed except
in `mp`.

Both opponent models are modelling choices too; see "Opponent models". The
minimising model's limit of 4 shells other seats have seen at the root is one of
its choices (see "Root value"); the Dealer model has no such limit.

## Where the spec left room, and what this file chose

Genuine disagreements are worth chasing; these are the spots where a
disagreement may be a reading difference rather than a bug. Each is one edit
away in `oracle.py`.

* **Deal start index** (`DEAL_INDEX_BASE = 0`): seat *i* is the 0-based seat
  index, so p1 starts at pool index 0 and p2 at pool index 1 (in `story`, p1 is
  dealt Magnifying Glass and Beer, p2 Beer and Cigarettes). Set the constant to 1
  to shift every seat one place along the pool.
* **Inverting the chamber**: the Inverter flips the chamber's shell and shows
  nobody its type (`ItemInteraction.gd` 165-171). The shell keeps its place and
  the type any seat saw, and a flag makes it fire as the complement, whether or
  not some seat has seen it. A seat that saw the chamber reads its new type from
  the flag. A seat that did not still holds the tube's counts from before the
  flip, so to it the chamber is a shell drawn from that pool and the chance it
  fires live becomes 1 − p. When the shell is finally drawn, the *drawn* type is
  what leaves the tube and the complement is what fires, which is the net effect
  of the shell becoming its complement and then being consumed. A second
  Inverter clears the flag.
* **Jammer and the once-per-turn cuff limit**: "only one pair of handcuffs may
  be applied per turn" is enforced as one cuff application per turn of any kind,
  so Handcuffs and Jammer share the flag.
* **Adrenaline** offers only steals whose stolen item is legally usable right
  now, and the one action text `steal <Item> from pN and use it` covers every
  target of a stolen Handcuffs or Jammer: the reported value is the best (for
  the seat to move) over those targets.
* **Held-but-off-pool items are usable.** `don`/`story`/`mp` choose the reload
  pool; a seat that is *given* an Inverter in a `story` position may still use
  it. Jammer and Remote are the exception: they work only in `mp`.
* When `turn` is not the solved seat, the ranking lists the mover's moves,
  still ordered by the solved seat's probability (under `--opponent dealer` the
  Dealer lists none).
* **A written `inverted` chamber that is also written as seen**:
  `known=pN:0L inverted` says seat N saw a live shell that now fires blank,
  which is how this file prints an Inverter used on a chamber a seat had seen.
  With the Dealer to move, the Dealer model turns it into its complement as it
  does an unseen one, and the seats that saw it keep their observation of the
  new type.
* **The saw after a blank into the shooter**: a blank that p2 fires into itself
  in a two-seat `don` or `story` game keeps the barrel sawed while shells remain,
  as the Dealer does in the game; this holds under both opponent models. Every
  other shot spends the saw.
* **A `phoned=` read on a shell the seat already knows** adds the reading seat
  to that shell's observers. Past the minimising model's limit of 4 those
  observers are kept on the shells the solved seat knows and dropped elsewhere.

## Cost

Search cost is driven by `--reloads`, then by seat count and item count. Two
seats with a handful of items and `--reloads 1` finish in a couple of seconds;
three or four seats carrying items with `--reloads 1` can run for minutes, since
each reload deals fresh items to every seat (4 each in `don`). Use `--reloads 0`
or `1` for wide positions, `--items-per-load 0` to keep a reload from dealing,
and keep both implementations on the same budget. Under `--opponent dealer` p2's
turn follows a script, so p2 adds chance branches (its coin, its phone, its
medicine) but no choice to the search.
