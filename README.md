<a id="readme-top"></a>

<div align="center">

# Buckshot Roulette Solver

### An expectiminimax move advisor and a playable opponent

[![Build](https://github.com/CameronScarpati/buckshot-roulette-bot/actions/workflows/build.yml/badge.svg)](https://github.com/CameronScarpati/buckshot-roulette-bot/actions/workflows/build.yml)

Describe any position from [Buckshot Roulette](https://store.steampowered.com/app/2835570/Buckshot_Roulette/)
and this ranks the moves worth considering by the probability that you are the last player
standing, under a stated opponent model, along with the assumptions that produced the number.
It also plays.

</div>

## What it does

- **Advises.** Give it a position, by one line of notation or by narrating the round as it
  happens, and it ranks the legal moves it considers useful with a win probability, marks
  ties, and names the opponent model it used.
- **Plays.** Take a seat against the solver or against the game's scripted dealer, or watch
  the solver play the dealer with every move explained. The shells and the dealer's coins come
  from a seed, so a whole round replays exactly.
- **Models the dealer, as an option.** The single-player dealer's script, read rule by rule
  from a decompilation of the game, can stand in for the opponent in the advisor and in play.
  The default opponent model is unchanged: it plays to minimise your chance.
- **Covers the game.** Two to four seats, all eleven items, and the single-player and
  multiplayer rule sets as settings on one engine.

The numbers are probabilities, not scores. A move worth 0.60 wins the round three times in
five against the model described in [docs/RULES.md](docs/RULES.md), and that document marks
every rule as verified against the game or its decompiled script, sourced to a citable page, or
assumed by this engine because nothing settles it.

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
  * shoot p2                          0.3978
    use Magnifying Glass              0.3666   (-0.0312)
    use Hand Saw                      0.3607   (-0.0371)
    shoot self                        0.2591   (-0.1387)
  Note: the search hit its reload budget in some lines, so those were valued by charges in hand.
  Model: double or nothing, 4 charges, 1 to 5 items dealt per load, modelled at 3, after a reload seat 1 acts first, a sawed barrel does not survive a reload, a reload clears handcuffs. The other seat plays to minimise your chance of surviving the round, spending no magnifying glasses or burner phones, so it is modelled slightly weaker than a player who tracks shells, and choosing from what it has seen rather than from what you have seen, picking evenly between moves it cannot tell apart. The answer is given from what the advised seat has seen: a shell only somebody else has looked at is unknown to you and known to them, and the value averages over how it could have fallen. Values are the probability of being the last player standing in this round, looking through 1 reload.
  1597228 states examined.
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

### Changing a rule

Three rules of this game are documented nowhere reliable, so the engine had to pick a value
and say so. Several more are documented and still worth being able to change, because seeing
what a rule is worth is the point. Each is an option, which means a rule can be settled by
playing a round and then checking the answer rather than by argument.

```sh
./build/advisor --position "p1=4/4[mg] p2=4/4 tube=2L3B turn=p1"
./build/advisor --position "p1=4/4[mg] p2=4/4 tube=2L3B turn=p1" --reload-turn keep
```

| Who acts first after a mid-round reload | Best move | Its value |
|---|---|---|
| Seat 1, the default | shoot p2 | 0.6375 |
| Whoever was to move | shoot p2 | 0.4506 |

This is also the setting the results table further down calls the chair. The default is the
rule the game uses, and playing the same hundred rounds under the other value takes seat 1
from 82 of 100 to 66 of 100, which is the clearest measure of how much rests on it.

```sh
./build/play --selfplay 100 --seed 1 --charges 2 --reloads 0
./build/play --selfplay 100 --seed 1 --charges 2 --reloads 0 --reload-turn keep
```

`--help` on either binary lists every setting, and
[docs/RULES.md](docs/RULES.md) maps each one to the field it changes.

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
  * use Magnifying Glass              0.6815
    shoot p2                          0.6725   (-0.0090)
    use Hand Saw                      0.6232   (-0.0583)
    shoot self                        0.6108   (-0.0707)

tube: 2 live, 2 blank
> p1  4/4 charges  items: Hand Saw  knows: shell 1 is blank
  p2  4/4 charges  items: Beer Handcuffs

Advising seat p1, to move: p1
  * shoot self                        0.6197
    shoot p2                          0.5303   (-0.0894)
    use Hand Saw                      0.4824   (-0.1373)
```

Knowing the chamber is blank turns shooting yourself from the worst move into the best one,
because a blank fired at yourself costs nothing and keeps the turn. `help` lists every
command: shots, ejections, reveals, item use, edits to charges and inventories, and `undo`.

### Against the scripted dealer

By default p2 plays to minimise your chance (`--opponent solver`). `--opponent dealer`
replaces it with the single-player dealer as the game scripts it: it uses items, picks a
target and flips its coin by the rules in
[docs/RULES.md](docs/RULES.md#the-scripted-dealer), so the value is your chance against that
dealer rather than against a minimising opponent. It needs two seats and p1 advised, and
`--mode` chooses between Double or Nothing (`don`, the default) and the story stages
(`story1`, `story2`, `story3`), which run the dealer's simpler story rules.

```sh
./build/advisor --position "p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1" --reloads 1 --opponent dealer
```

```
tube: 2 live, 3 blank
> p1  2/4 charges  items: Magnifying Glass Hand Saw
  p2  4/4 charges  items: Beer Handcuffs

Advising seat p1, to move: p1
  * shoot p2                          0.4967
    use Magnifying Glass              0.4952   (-0.0015)
    use Hand Saw                      0.4507   (-0.0460)
    shoot self                        0.3629   (-0.1337)
  Note: the search hit its reload budget in some lines, so those were valued by charges in hand.
  Model: double or nothing, 4 charges, [...] The other seat is the dealer, and it plays the game's own dealer script with its endless rules: [...]
  240573 states examined.
```

This is the position at the top of Asking about a position, and the values differ from the
ones there because the opponent model differs. When the dealer is to move there is nothing to
rank, since it does not choose between moves, so the advisor prints your chance from that
position instead:

```sh
./build/advisor --position "p1=3/4[saw] p2=3/4[mg,beer] tube=2L2B turn=p2" --reloads 1 --opponent dealer
```

```
tube: 2 live, 2 blank
  p1  3/4 charges  items: Hand Saw
> p2  3/4 charges  items: Magnifying Glass Beer

Advising seat p1. The dealer (p2) is to move and plays by its script.
  Your chance of being the last player standing: 0.7792
  [...]
```

While narrating a round, `opponent dealer` and `opponent solver` switch between the two.

## Playing

```sh
./build/play --seed 7 --charges 4
```

You take seat 1 and choose from a numbered menu. The solver answers from the same engine
that produced the advice above, and with each of its moves it says what it rates its own
chances at. The board you see shows the shells you have seen and none that the solver has
looked at.

### Against the dealer

```sh
./build/play --opponent dealer --seed 6 --charges 2 --reloads 0
```

Seat 2 is now the scripted dealer. Each of its moves is drawn from its rules with the seeded
generator, and you are told what it does but not what it learns: the board you see leaves out
the shells it has looked at, as the game does. A turn you lose to its handcuffs is reported
when it is lost.

```
tube: 4 live, 4 blank
> p1  2/2 charges  items: Magnifying Glass x2 Beer Cigarettes Inverter
  p2  2/2 charges  items: Magnifying Glass x2 Handcuffs Burner Phone Expired Medicine

Your move:
  1) shoot self
  2) shoot p2
  3) use Magnifying Glass
  4) use Beer
  5) use Inverter
> 2
  The shell was blank.

tube: 4 live, 3 blank
  p1  2/2 charges  items: Magnifying Glass x2 Beer Cigarettes Inverter
> p2  2/2 charges  items: Magnifying Glass x2 Handcuffs Burner Phone Expired Medicine
The dealer uses its Magnifying Glass.
  It looks into the chamber.
The dealer uses its Handcuffs.
  You are handcuffed and will lose the next turn.
The dealer uses its Burner Phone.
  It listens to the type of one shell past the chamber.
The dealer shoots you.
  The shell was LIVE.
You are handcuffed and lose this turn.
```

`--mode story1`, `story2` or `story3` plays a story stage instead, with that stage's charges.
The game takes a story stage's loads from fixed data for each round, which is not in the
decompiled scripts, so here a story stage draws its loads at random from the solver's
distribution instead.

### Watching the solver play the dealer

```sh
./build/play --watch --seed 6 --charges 2 --reloads 0
```

The solver takes seat 1. Before each of its moves it prints the board and up to three of the
moves it ranks highest, with their values. A star marks each move worth the most, and when
several tie, a line says so and that p1 plays the first one listed. What p1's own glass or
phone shows is printed after its move. When some of the values stop at the reload budget, the
weighing calls its chance estimated, and the first such weighing in a round adds a note that
says what that means. Every dealer move comes with the rule that produced it. A spectator is
shown what the dealer learned privately, and each such line says so. Shells are numbered from
the chamber, which is shell 1, so a shell's number drops as the shells ahead of it leave.
`--pace 800` waits 800 milliseconds before each move, so a round can be followed as it plays.

```
tube: 4 live, 3 blank
> p1  2/2 charges  items: Beer Cigarettes Inverter  knows: shell 1 is live
  p2  2/2 charges  items: Magnifying Glass x2 Handcuffs Burner Phone Expired Medicine
p1 (solver) weighs, by its estimated chance of surviving the round:
  * shoot p2                                    0.3100
    use Inverter                                0.2017
    use Beer                                    0.1792
p1 plays: shoot p2
  The shell was LIVE.

tube: 3 live, 3 blank
  p1  2/2 charges  items: Beer Cigarettes Inverter
> p2  1/2 charges  items: Magnifying Glass x2 Handcuffs Burner Phone Expired Medicine
The dealer uses its Magnifying Glass.
  Seen only by the dealer: the chamber is blank.
  Why: it does not know the chamber and more than one shell is left.
The dealer uses its Handcuffs.
  p1 is handcuffed and will lose the next turn.
  Why: p1 is free to be handcuffed and more than one shell is left.
The dealer uses its Burner Phone.
  Heard only by the dealer: shell 3 is live.
  Why: more than two shells are left.
The dealer shoots itself.
  Why: it keeps the target it chose earlier this turn, when the glass showed the
       chamber was blank.
  The shell was blank.
  A blank at itself lets the dealer move again.

tube: 3 live, 2 blank
  p1  2/2 charges  cuffed  items: Beer Cigarettes Inverter
> p2  1/2 charges  items: Magnifying Glass Expired Medicine  knows: shell 2 is live
The dealer uses its Magnifying Glass.
  Seen only by the dealer: the chamber is blank.
  Why: it does not know the chamber and more than one shell is left.
The dealer shoots itself.
  Why: it keeps the target it chose earlier this turn, when the glass showed the
       chamber was blank.
  The shell was blank.
  A blank at itself lets the dealer move again.

tube: 3 live, 1 blank
  p1  2/2 charges  cuffed  items: Beer Cigarettes Inverter
> p2  1/2 charges  items: Expired Medicine  knows: shell 1 is live
The dealer shoots p1.
  Why: it heard this shell on a Burner Phone earlier and p1 did not, so only it
       knows it is live.
  The shell was LIVE.
p1 is handcuffed and loses this turn.

tube: 2 live, 1 blank
  p1  1/2 charges  lost a turn  items: Beer Cigarettes Inverter
> p2  1/2 charges  items: Expired Medicine
The dealer shoots p1.
  Why: with no target and more live than blank shells, it always shoots p1.
  The shell was blank.

tube: 2 live, 0 blank
> p1  1/2 charges  items: Beer Cigarettes Inverter
  p2  1/2 charges  items: Expired Medicine
p1 (solver) weighs, by its estimated chance of surviving the round:
  * shoot p2                                    1.0000
  * use Beer                                    1.0000
  * use Cigarettes                              1.0000
  4 moves tie at the top (3 shown), and p1 plays the first one listed.
p1 plays: shoot p2
  The shell was LIVE.

tube: 1 live, 0 blank
  p1  1/2 charges  items: Beer Cigarettes Inverter
  p2  0/2 charges  out  items: Expired Medicine
p1 (solver) wins the round.
```

### A batch against the dealer

```sh
./build/play --dealer 20 --seed 1 --charges 2 --reloads 0
./build/play --dealer 20 --seed 1 --reloads 0 --mode story2
```

`--dealer ROUNDS` plays the solver as seat 1 against the scripted dealer for that many rounds,
each seeded from the seed plus its round number, and prints how many rounds seat 1 survived
and a 95 percent Wilson score interval for that rate: the range of long-run survival rates
that the count is consistent with, which for a batch this small is wide. Like the other
batches it stops searching at the load in the tube with `--reloads 0`.

In Double or Nothing every game against the dealer, whether you play it, watch it or run it as
a batch, draws the counts for each load the way the game's script does: 2 to 8 shells with the
live count half the total, rounded down, and 2 to 5 items a seat unless `--items-per-load` is
given. So live shells never outnumber blanks when a load is dealt. A story stage draws its
loads from the solver's distribution instead, a total of 2 to 8 and a live count anywhere from
1 to one less than the total, which includes loads the stage never deals, so a rate from a
story stage batch is not measured on the game's own loads. In both modes the search itself
still looks past a reload through the solver's distribution.

## How it works

**The tube is an information state, not a shuffled list.** A load is a live count and a
blank count. Each position carries the type it has been resolved to, if anything has forced
the question, and a record of which seats have seen it. A position nobody has seen is
exchangeable with every other unseen position, so the chance the chamber is live is the
live shells a seat cannot account for over the shells it cannot account for. A magnifying
glass tells one seat and nobody else. An inverter flips the chamber without revealing it,
so an unseen shell keeps its place in the pool and its odds invert.

**The search is exhaustive within a set number of reloads, not sampled.** The value of a
position is computed from the values of the positions it leads to, weighted by their
probabilities, and memoised. Within a load the graph is acyclic, because every move consumes
a shell or an item. Across a reload the search continues for a stated number of reloads and
then stops at a boundary, where a position is scored by each seat's share of the charges
left, and any answer that touched the boundary says so. Inside that budget the answer is
still only as good as the opponent model below, the single item deal it assumes at each
reload, and the moves it considers: an item that cannot help, such as cigarettes at full
charges, is left out of the ranking even though the game would let you use it.

**Every answer is given from what you have seen, and the other seat gets what it has seen.**
A shell only the other seat has looked at counts as unseen to you, so the advisor can never
tell you what is in the chamber on the strength of somebody else having looked. Erasing it
there would be the other mistake, because it would price the position against an opponent as
ignorant as you are. The search instead splits the position into the ways that shell could
have fallen, weighted by what you cannot account for, and solves each one against a seat that
knows which it is in. What you are shown is the average, and the answer is identical whichever
type the position names for a shell you did not see.

**The opponent model is stated, because the word optimal means nothing without one.** By
default the other seat plays to minimise your chance of surviving the round, and it chooses
from what it has seen rather than from the position as it really is. A shell you revealed
privately is not one it can act on, and when two of its moves look the same to it, it is
assumed to pick between them evenly. It is also modelled as spending no magnifying glasses
and no burner phones, which keeps the search from branching on knowledge you cannot see, at
the cost of modelling it slightly weaker than a player who tracks shells. Every answer
prints all of this.

## What it does not do

- **The default opponent is not the dealer.** Unless `--opponent dealer` is given, the
  opponent minimises your chance within the limits described above: it spends no magnifying
  glasses and no burner phones, and it picks evenly between moves it cannot tell apart. That
  is not the strongest opponent possible, so a number is not a worst case, and it is not a
  bound on how you would fare against the real dealer either. It is the chance of winning
  under the stated opponent model, looking a set number of reloads ahead, with positions past
  that horizon scored by each seat's share of the charges left.
- **The dealer model is not the game, exactly.** It walks its items by type in a fixed order
  rather than in the order they sit on the table, it reads one cigarette check on the first
  pass of a turn from the items it holds then, and a blank it fires into itself clears a sawed
  barrel. Your own moves never infer a shell's type from what the dealer chose to do. It plays
  only at a two-seat table. And the reloads the search sees are this engine's: in Double or
  Nothing the game draws shells and items for a load differently, so a value that looks past a
  reload is not the game's for that reason. A game played against the dealer draws a Double or
  Nothing load's counts the way the game's script does, but a story stage's loads at random
  rather than from the stage's fixed data, so a story stage played or batched here can meet
  loads the game never deals. [docs/RULES.md](docs/RULES.md#the-scripted-dealer) lists each
  rule and each approximation with its source line.
- **The reload boundary is a cutoff.** Looking through more reloads costs more time, and at
  the end of the budget a position is valued by charges in hand.
- **The item deal at a reload is one fixed spread, not a distribution.** Inside the search,
  each living seat takes items from the pool in turn, starting at its own seat index and
  stopping at the per-load count or the item limit, rather than drawing at random. Enumerating
  every multiset would multiply the state space by thousands, and
  [docs/RULES.md](docs/RULES.md) records this as an approximation, along with the item count,
  which a live game redraws at every load and the search takes at the middle of its range.
- **Three rules are still assumptions.** Whether a restraint survives a mid-round reload, how
  many items a seat's tray holds, and the shape of the shell composition inside the range the
  game uses are all settings, because nothing reliable documents them.
  [docs/RULES.md](docs/RULES.md) marks every rule as verified, sourced or assumed, and names
  the field that changes each.

## Results

Two seeded batches, each a hundred rounds at two charges a seat, and both run in seconds.
Every number below comes from the command above it, measured on Linux against libstdc++ with
a Release build from GCC 13.3.0 and again from Clang 18.1.3, which gave the same counts. The
seed fixes the shells, so the same build prints the same count every time. The seed drives
`std::mt19937` through the standard library's distributions, and their algorithms differ
between standard libraries, so a build against libc++ or the MSVC library can deal different
shells and items from the same seed and print a different count. On the same standard library,
a different compiler can still move a count by a few rounds: when two moves are worth the same,
rounding in the last digits can decide which is ranked first, and the batch plays whichever
comes first. For that reason CI does not pin these counts. `tools/check_play.sh` checks that a
seed replays byte for byte and that a shorter self-play batch (ten rounds, seed 3, a reload
budget of zero) does not fall below 7 wins in 10 for seat 1.

```sh
./build/play --selfplay 100 --seed 1 --charges 2 --reloads 0
./build/play --baseline 100 --seed 1 --charges 2 --reloads 0
```

| Batch | Seat 1 survives |
|---|---|
| The solver against itself | 82 of 100 rounds |
| The solver against the heuristic in `cli/play.cpp` | 86 of 100 rounds |

Read those two rows together, because neither means much alone.

The first row is not a measure of strength. Both seats play the same way, so what it
measures is the chair. Every round opens with a reload, so the rule that seat 1 acts first
after a reload also decides who makes the first move of the round, and the 32 points over an
even split are the two together. The same batch can be rerun with the reload rule changed:

```sh
./build/play --selfplay 100 --seed 1 --charges 2 --reloads 0 --reload-turn keep
./build/play --selfplay 100 --seed 1 --charges 2 --reloads 0 --reload-turn dealer
```

With `keep`, a reload leaves the turn where it was, so seat 1 still makes the first move of the
round and nothing more, and seat 1 survives 66 of 100. The reload rule is therefore worth about
16 of the 32 points and the first move of the round the other 16. With `dealer`, seat 2 acts
first at every load, including the first, and seat 1 survives 22 of 100, roughly the default
reflected. (Same builds as above. A hundred rounds is a small sample, so read these
as rough sizes.) The rule is the game's, not a guess, and it is still a setting, which is the
only way to see what it is worth. [docs/RULES.md](docs/RULES.md) names the field that changes
it.

The second row is the one about strength, and the claim it supports is the difference
between the rows, not the 86. Swapping a copy of the solver for a heuristic opponent is
worth about four points to the seat facing it. The heuristic is written out in
`cli/play.cpp`: it knows the odds and the obvious tactics and searches nothing, so it is a
floor rather than a serious opponent. A hundred rounds is a small sample, and both numbers
move with the charges, the reload budget and the seed, which is why all three are printed
next to them.

Both batches stop at the load in the tube. Reloads still happen while the round is played,
which is why the chair shows up at all; what the budget of zero says is that the solver does
not search past one, and values a position it reaches by charges in hand. Raising the budget
to one is the same measurement against a stronger solver and costs about a minute and a half
a round, because a solved reload deals every seat a fresh handful of items and each item is
another move at every turn that does not end one. The state counts behind that are in
[docs/RULES.md](docs/RULES.md).

## Testing

| Layer | What it covers |
|---|---|
| Unit tests | Tube arithmetic, every rule transition with its probability mass, the notation round trip |
| Golden values | Solver values pinned to positions worked out by hand, with the arithmetic in the test |
| Invariance | A shell the advised seat never saw must give the same answer whichever type the position names it, so naming it live and naming it blank are compared directly |
| Differential | An independently written Python solver in `tools/oracle/`, compared move by move over fixed and random positions by `tools/compare_solvers.py`, against both the minimising opponent and the scripted dealer |
| Determinism | The same seed replays the same batch and the same round against the dealer byte for byte, the same position gives the same answer, a seeded batch is pinned to a band, and a Double or Nothing load against the dealer never holds more live shells than blanks, by `tools/check_play.sh` |
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
  Dealer.*         The single-player dealer's script, one pass at a time
  Notation.*       Positions as one line of text
solver/          The expectiminimax search over the rules
cli/             advisor (ranks moves) and play (plays a round or a batch)
tests/           Unit, rule, notation, golden value and invariance tests
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

- [Buckshot Roulette](https://store.steampowered.com/app/2835570/Buckshot_Roulette/) by Mike Klubnika
- [Expectiminimax](https://en.wikipedia.org/wiki/Expectiminimax_tree), the classical name for
  a search over chance nodes

<p align="right">(<a href="#readme-top">back to top</a>)</p>
