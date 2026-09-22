<a id="readme-top"></a>

<div align="center">

# Buckshot Roulette Solver

### An exact move advisor and a playable opponent

[![Build](https://github.com/CameronScarpati/buckshot-roulette-bot/actions/workflows/build.yml/badge.svg)](https://github.com/CameronScarpati/buckshot-roulette-bot/actions/workflows/build.yml)

Describe any position from [Buckshot Roulette](https://store.steampowered.com/app/2537590/BUCKSHOT_ROULETTE/)
and this prints every legal move ranked by the probability that you are the last player
standing, along with the assumptions that produced the number. It also plays.

</div>

## What it does

- **Advises.** Give it a position, by one line of notation or by narrating the round as it
  happens, and it ranks every legal move with a win probability, marks ties, and names the
  opponent model it used.
- **Plays.** Take a seat against the solver. The shells come from a seed, so a whole round
  replays exactly.
- **Covers the game.** Two to four seats, all eleven items, and the single-player and
  multiplayer rule sets as settings on one engine.

The numbers are probabilities, not scores. A move worth 0.60 wins the round three times in
five against the model described in [docs/RULES.md](docs/RULES.md), and that document
separates what is verified about the game from what this engine had to assume.

## Quick start

```sh
git clone https://github.com/CameronScarpati/buckshot-roulette-bot.git
cd buckshot-roulette-bot
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Requires a C++17 compiler and CMake 3.16 or newer. The test suite fetches GoogleTest at
configure time, so the first build needs a network connection. These four commands are run
on a clean checkout by CI, so they cannot drift away from what works.

## Asking about a position

```sh
./build/advisor --position "p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1" --reloads 1
```

```
tube: 2 live, 3 blank
> p1  2/4 charges  items: Magnifying Glass Hand Saw
  p2  4/4 charges  items: Beer Handcuffs

Advising seat p1, to move: p1
  * shoot p2                          0.2720
    use Magnifying Glass              0.2476   (-0.0244)
    use Hand Saw                      0.2464   (-0.0256)
    shoot self                        0.1532   (-0.1189)
  Note: the search hit its reload budget in some lines, so those were valued by charges in hand.
  Model: double or nothing, 4 charges, 2 items dealt per load, after a reload seat 1 acts
  first, a sawed barrel does not survive a reload, a reload clears handcuffs. The other seat
  plays to minimise your chance of surviving the round, spending no magnifying glasses or
  burner phones, so it is modelled slightly weaker than a player who tracks shells. Values
  are the probability of being the last player standing in this round, looking through 1
  reload.
  434575 states examined.
```

### The notation

```
p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1 cuffed=p2 sawed inverted known=p1:0L,2B dir=ccw
```

| Token | Meaning |
|---|---|
| `p1=3/4[saw,beer]` | seat 1 has 3 charges of 4 and holds those items |
| `tube=2L3B` | two live and three blank shells remain |
| `turn=p1` | seat 1 is to move |
| `cuffed=p2` | seat 2 is handcuffed and will be skipped |
| `skipped=p2` | seat 2 already lost a turn to handcuffs and is owed one back |
| `restraintused` | the seat to move has already spent a restraint this turn |
| `sawed` | the barrel is sawed for the next shot |
| `inverted` | an inverter flipped a chamber nobody has seen |
| `known=p1:0L,2B` | seat 1 has seen the chamber (live) and shell 3 (blank) |
| `dir=ccw` | turn order runs the other way after a remote |

Item tokens: `mg`, `beer`, `cig`, `cuff`, `saw`, `phone`, `adr`, `inv`, `med`, `jam`, `rem`.

### Narrating a round

Run `./build/advisor` with no arguments and describe what happens as it happens. The advisor
tracks what each seat has seen, which is what makes an answer different from a table of odds.

```
set p1=4/4[mg,saw] p2=4/4[beer,cuff] tube=2L2B turn=p1
advise
mg blank
advise
```

```
Advising seat p1, to move: p1
  * shoot p2                          0.6008
    use Magnifying Glass              0.5922   (-0.0086)
    use Hand Saw                      0.5443   (-0.0565)
    shoot self                        0.4761   (-0.1247)

tube: 2 live, 2 blank
> p1  4/4 charges  items: Hand Saw  knows: shell 1 is blank
  p2  4/4 charges  items: Beer Handcuffs

Advising seat p1, to move: p1
  * shoot self                        0.5100
    shoot p2                          0.4038   (-0.1062)
    use Hand Saw                      0.3469   (-0.1631)
```

Knowing the chamber is blank turns shooting yourself from the worst move into the best one,
because a blank fired at yourself costs nothing and keeps the turn. `help` lists every
command: shots, ejections, reveals, item use, edits to charges and inventories, and `undo`.

## Playing

```sh
./build/play --seed 7 --charges 4
```

You take seat 1 and choose from a numbered menu. The solver answers from the same engine
that produced the advice above, and it says what it rates its own chances at before each
move.

## How it works

**The tube is an information state, not a shuffled list.** A load is a live count and a
blank count. Each position carries the type it has been resolved to, if anything has forced
the question, and a record of which seats have seen it. A position nobody has seen is
exchangeable with every other unseen position, so the chance the chamber is live is the
live shells a seat cannot account for over the shells it cannot account for. A magnifying
glass tells one seat and nobody else. An inverter flips the chamber without revealing it,
so an unseen shell keeps its place in the pool and its odds invert.

**The search is exact, not a depth-limited estimate.** The value of a position is computed
from the values of the positions it leads to, weighted by their probabilities, and
memoised. Within a load the graph is acyclic, because every move consumes a shell or an
item. Across a reload the search continues for a stated number of reloads and then stops at
a boundary value, and any answer that touched the boundary says so.

**Every answer is given from what you have seen.** A shell that only the other seat has
looked at counts as unseen, so the advisor can never tell you what is in the chamber on the
strength of somebody else having looked.

**The opponent model is stated, because the word optimal means nothing without one.** By
default the other seat plays to minimise your chance of surviving the round, and it chooses
from what it has seen rather than from the position as it really is. A shell you revealed
privately is not one it can act on, and when two of its moves look the same to it, it is
assumed to pick between them evenly. It is also modelled as spending no magnifying glasses
and no burner phones, which keeps the search from branching on knowledge you cannot see, at
the cost of modelling it slightly weaker than a player who tracks shells. Every answer
prints all of this.

## What it does not do

- **The scripted dealer is not modelled.** Its policy would have to be derived rule by rule
  from the game before it could honestly be called a model of the dealer, so the opponent is
  an exact minimiser instead. Against the real dealer, treat the numbers as a lower bound.
- **The reload boundary is a cutoff.** Looking through more reloads costs more time, and at
  the end of the budget a position is valued by charges in hand.
- **The item deal at a reload is averaged, not enumerated.** Enumerating every multiset would
  multiply the state space by thousands without changing which move is best.
- **Several rules are assumptions.** Who holds the turn after a mid-round reload, whether a
  sawed barrel survives one, the shell composition generator and the odds on expired
  medicine are all settings, and [docs/RULES.md](docs/RULES.md) lists all eleven of them with
  the field that changes each.

## Results

Two seeded batches, each a hundred rounds at two charges a seat with a reload budget of one.
Every number comes from the command above it and reproduces exactly.

```sh
./build/play --selfplay 100 --seed 1 --charges 2 --reloads 1
./build/play --baseline 100 --seed 1 --charges 2 --reloads 1
```

| Batch | Seat 1 survives |
|---|---|
| The solver against itself | 76 of 100 rounds |
| The solver against the heuristic in `cli/play.cpp` | 81 of 100 rounds |

Read those two rows together, because neither means much alone.

The first row is not a measure of strength. Both seats play the same way, so what it
measures is the chair: under the default rule, seat 1 acts first after every reload and not
only at the start of the round, and that single assumption is worth 26 points over an even
split. It is the clearest argument for keeping the rule a setting rather than a constant,
and [docs/RULES.md](docs/RULES.md) lists it among the eleven assumptions with the field that
changes it.

The second row is the one about strength, and the claim it supports is the difference
between the rows, not the 81. Swapping a copy of the solver for a heuristic opponent is
worth about five points to the seat facing it. The heuristic is written out in
`cli/play.cpp`: it knows the odds and the obvious tactics and searches nothing, so it is a
floor rather than a serious opponent. A hundred rounds is a small sample, and both numbers
move with the charges, the reload budget and the seed, which is why all three are printed
next to them.

## Testing

| Layer | What it covers |
|---|---|
| Unit tests | Tube arithmetic, every rule transition with its probability mass, the notation round trip |
| Golden values | Solver values pinned to positions worked out by hand, with the arithmetic in the test |
| Differential | An independently written Python solver in `tools/oracle/`, compared move by move over fixed and random positions by `tools/compare_solvers.py` |
| Sanitizers | The suite under the address and undefined behaviour sanitizers in CI |
| Build | Four compiler and configuration combinations, with warnings as errors |

The differential test is the one that matters most. Two implementations of the same rules
that share no code have to agree, and when they do not, one of them is wrong.

```sh
python3 tools/oracle/oracle.py --selftest
python3 tools/compare_solvers.py --advisor build/advisor --random 60 --seed 11
```

## Layout

```
engine/          Rules as pure functions: no input, no output, no global state
  Items.*          The eleven items
  Tube.*           The information state of the shotgun
  State.*          Seats, positions, actions
  Rules.*          Legal moves and transitions, each with its probability
  Config.*         Rule sets and the settings that separate them
  Notation.*       Positions as one line of text
solver/          The exact search over the rules
cli/             advisor (ranks moves) and play (plays a round)
tests/           Unit, rule, notation and golden value tests
tools/           The Python oracle and the differential comparison
docs/RULES.md    Every rule, its confidence, and the setting that controls it
```

## License

Distributed under the MIT License. See `LICENSE` for more information.

## Contact

Cameron Scarpati, cameronscarp@gmail.com

Project link:
[github.com/CameronScarpati/buckshot-roulette-bot](https://github.com/CameronScarpati/buckshot-roulette-bot)

## Acknowledgments

- [Buckshot Roulette](https://store.steampowered.com/app/2537590/BUCKSHOT_ROULETTE/) by Mike Klubnika
- [Expectiminimax](https://en.wikipedia.org/wiki/Expectiminimax_tree), the classical name for
  a search over chance nodes

<p align="right">(<a href="#readme-top">back to top</a>)</p>
