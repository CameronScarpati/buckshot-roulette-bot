# Rules

## Scope

This document is the rule set the engine in `engine/` implements, written so that a reader can
check every statement against the code and against the game's own scripts. It covers the
single-player modes (the story stages and Double or Nothing) and the multiplayer mode, since all
three are the same state machine under different settings (`engine/Config.h`). Most rules below
are read from a decompilation of the game's scripts, pinned and linked under
[The scripted dealer](#the-scripted-dealer). Some depend on the game's scene data, which that
decompilation does not contain, some come from the community wiki, and some are approximations
the search makes in order to finish. Every rule carries a marker saying which, and every rule
that is a setting names the `RuleConfig` field that changes it.

Two words are used in one sense throughout. A **round** is one fight, from its first load until
one seat is left standing; the game's scripts call it a batch, and the three story stages are
three rounds. A **load** is one tube of shells, from the moment the shotgun is loaded until it
is empty; the scripts call each one a round, and so does the wiki.

## How to read the confidence markers

**Verified.** Read from the logic the game ships, in the third-party decompilation of Buckshot
Roulette v2.2.0 hotfix 6 pinned under [The scripted dealer](#the-scripted-dealer), or confirmed
by watching it happen in play. A rule read from the scripts cites a file and line range there.
This project has not compared that decompilation with a build of the game. A reader who
disagrees with one of these should expect to be shown the line.

**Sourced.** Stated by the game's community wiki, which is cited by page name in the sources
section at the end of this document. A wiki is a secondary source and can be wrong, so this
marker sits below Verified. It does mean a reader can check the claim without owning the game,
and it means the engine is not guessing.

**Reported.** Consistent with public descriptions of the game and with how the game is
commonly played, but not confirmed against the game directly and not carried by a citable
source. These are probably right. They are not evidence.

**Assumed.** The scripts do not settle the rule, usually because the answer is held in the
game's scene data rather than in its scripts, and the engine still had to pick a value in order
to produce a number. Each one is an entry under
[Assumptions pending extraction](#assumptions-pending-extraction), or a setting whose field is
named, or a multiplayer rule, since the pinned scripts cover only the single-player game. An
assumed rule is not a claim about the game. It is a statement about this engine.

Separately from these markers, the search makes a few approximations in order to finish. They
are not rule questions, and they are listed under [Approximations](#approximations).

## Core mechanics

Seats are numbered from zero internally and printed as p1, p2 and so on. In the single-player
modes p1 is the player and p2 is the dealer. A round runs until one seat is left standing
(`engine/State.h`, `roundOver`).

| Rule | What the game and the engine do | Confidence | Setting |
|---|---|---|---|
| Double or Nothing draws one of seven loads | A load is a total of 2 to 8 shells with half of them live, rounded down and at least one ([RoundManager.gd 148-152][rm148]), so it is one of (1 live, 1 blank), (1, 2), (2, 2), (2, 3), (3, 3), (3, 4) and (4, 4), each with chance one in seven. Live shells never outnumber blanks. The preset writes these seven into `loadTable` (`engine/Config.cpp`, `doubleOrNothing`) | Verified | `loadTable` |
| Story and multiplayer loads come from the general distribution | A total uniform over 2 to 8, then a live count uniform over `[1, total - 1]`, so every load holds at least one of each (`engine/Rules.cpp`, `loadDistribution`). The story stages take their loads from scene data that has not been extracted | Assumed, entries 1 and 2 under [Assumptions pending extraction](#assumptions-pending-extraction); multiplayer Assumed | `loadTable` |
| Every load of a round is drawn afresh | The game steps through a list of loads set up for the round and repeats the last one once the list runs out ([RoundManager.gd 197][rm197]); in Double or Nothing every entry of that list is drawn at random when the round is set up ([RoundManager.gd 142-157][rm142]). How long the list is lives in the scene data, so the engine draws every load afresh | Assumed, entry 1 | none |
| The counts of a load are public, the order is hidden | Every load shows its shells to the table before they go into the tube ([ShellSpawner.gd 55-57][ss55]); the counts are also spelled out in text in the story stages before the last ([ShellSpawner.gd 71-81][ss71]). The tube stores public counts plus a per-seat knowledge mask, and a position nobody has seen is exchangeable with every other such position (`engine/Tube.h`) | Verified for the counts; Assumed for the order, entry 3 | none |
| Shooting yourself with a blank keeps the turn | The player goes again after a blank into itself ([ShotgunShooting.gd 119-143][sg119]), and so does the dealer ([DealerIntelligence.gd 372-387][di372]). The turn passes only when the shell was live, the shooter died, or the target was another seat (`engine/Rules.cpp`, `apply`) | Verified | none |
| Shooting yourself with a live shell costs charges and passes the turn | Damage is applied to the shooter, then the turn advances ([ShotgunShooting.gd 119-143][sg119], `engine/Rules.cpp`, `apply`) | Verified | none |
| Shooting another seat passes the turn whatever the shell was | The self-shot on a blank is the only shot that holds the turn ([ShotgunShooting.gd 119-143][sg119], [RoundManager.gd 264-295][rm264]) | Verified | none |
| Any living seat may be shot, including yourself | Legal shots are self plus every other living seat (`engine/Rules.cpp`, `legalActions`) | Verified | none |
| A sawed barrel deals two charges | The Hand Saw sets the next shot's damage to 2 ([ItemInteraction.gd 153-158][ii153]); the engine applies 2 when the barrel is sawed and 1 otherwise (`engine/Rules.cpp`, `apply`) | Verified | none |
| A shot spends the saw, with one exception | The player's turn ends through the end of turn, which regrows the barrel ([RoundManager.gd 264-273][rm264], [SegmentManager.gd 14-17][sm14]), so every shot the player fires spends it, a blank into itself included. The dealer's turn goes on after a blank into itself without that step ([DealerIntelligence.gd 305-324][di305], [387][di387]), so the barrel stays sawed for its next shot while shells remain. Ejecting a shell with Beer is not a shot and leaves the saw alone (`engine/Rules.cpp`, `apply`) | Verified; in multiplayer every shot spends it, Assumed | `dealerSeatBlankKeepsSaw` |
| A sawed barrel across a reload | A Beer that empties the tube starts the next load at once, without the end of turn ([ItemInteraction.gd 130-141][ii130], [RoundManager.gd 196-234][rm196]), so the saw carries into the new load. A shot that empties the tube has already spent it (`engine/Rules.cpp`, `reloadInto`) | Verified for the single-player modes; Assumed in multiplayer, where it does not survive | `sawSurvivesReload` |
| A seat at zero charges is out | Damage floors at zero ([ShotgunShooting.gd 119-143][sg119]) and a seat is alive while it has a charge (`engine/State.h`). Overkill from a sawed barrel does not carry over | Verified | none |
| The last story stage's faded charges | In the third stage a hit that leaves a seat on 2 or fewer charges cuts its wire and sets it to 1; from then on the next hit kills and healing does nothing ([HealthCounter.gd 91-105][hc91], [DeathManager.gd 51-53][dm51], [94-100][dm94]). The engine shows the stage as five charges with `healFloor` at 2, and a shot on a seat at or above the floor leaves it on at least 1 (`engine/Rules.cpp`, `shotDamage`). See the stage 3 paragraph under Modes for what a charge count means there | Verified for the wire; the stage's charges are Assumed, entry 2 | `healFloor` |
| The round ends when one seat remains | `roundOver()` is true at one or fewer living seats (`engine/State.h`) | Verified | none |
| An empty tube starts the next load | The end of turn starts the next load when the tube is empty ([RoundManager.gd 264-295][rm264]). In the engine `needsReload()` is the empty tube; the load itself is `rules::reloadOutcomes` for a solver and `Table::load` for a live game (`engine/Rules.h`, `engine/Table.h`) | Verified | none |
| A load deals new items to every living seat | Each living seat draws the load's count of items from the pool, one at a time ([ItemManager.gd 229-236][im229], [346-347][im346]), stopping at the table limit | Verified for Double or Nothing; the story counts are Assumed, entry 2 | `itemsPerLoad`, `itemLimit`, `itemPool` |
| Double or Nothing deals 2 to 5 items a load | The count is drawn from 2 to 5 for every load, and both seats take that many ([RoundManager.gd 154][rm154]). A live game draws it (`Table::load`); the solver models it at 4, listed under [Approximations](#approximations) | Verified | `itemsPerLoad`, `itemsPerLoadMax` |
| The first load of a round at 2 charges deals no Hand Saw | Both seats draw from the pool without the Hand Saw on the first load of a round that starts at 2 charges ([ItemManager.gd 235-236][im235], [357-358][im357]). Only `play` deals a first load (`Table::poolFor`); a reload the solver looks through is never one | Verified | none |
| Items stay on the table from load to load | The table is cleared only when a new round begins ([ItemManager.gd 125-135][im125]), so a deal adds to what each seat holds (`engine/Rules.cpp`, `reloadOutcomes`) | Verified; story loads Assumed, entry 4, and the Double or Nothing clear rests on entry 9 | none |
| A seat holds at most eight items | Placement stops at 8 ([ItemManager.gd 258][im258], [279][im279], [346-347][im346]). The deal stops at the limit, and a written position with a ninth item is refused | Verified for single player; Assumed for multiplayer | `itemLimit` |
| Every Double or Nothing round draws its charges | Each round starts both seats on the same count, drawn from 2 to 4 ([RoundManager.gd 145][rm145]). `play` draws it for every round unless `--charges` is given; the advisor takes the charges a position states | Verified | `charges` |
| p1 moves first after every load | After a load the player is handed the shotgun whatever happened before it ([ShellLoader.gd 92-98][sl92], [RoundManager.gd 264-295][rm264]). Multiplayer defaults to the seat that was to move keeping the turn (`engine/Rules.cpp`, `reloadInto`) | Verified for single player; Assumed for multiplayer | `reloadTurn` |
| A load releases handcuffs | Every restraint comes off before a load is dealt, including a turn a seat was still owed ([RoundManager.gd 200][rm200], [HandcuffManager.gd 40-56][hm40]; `engine/Rules.cpp`, `reloadInto`) | Verified for single player; Assumed for multiplayer | `reloadClearsCuffs` |
| Shells that are fired or ejected are public | Both the shot and Beer resolve the chamber with every seat as an observer (`engine/Rules.cpp`, `apply` and `applyItemEffect`) | Verified | none |
| Using an item does not end the turn | `apply` advances the turn after an item only when the user died from it (`engine/Rules.cpp`, `apply`) | Verified | none |

### What a seat may do

Every move the game allows is offered, including uses that change nothing but the hand, such as
a Magnifying Glass on a chamber the user already knows, Cigarettes at full charges, or a Burner
Phone with one shell left. Such a use spends the item, as it does in the game
([ItemInteraction.gd 117-182][ii117]; `engine/Rules.cpp`, `legalActions`). The advisor, `play`
and the web page rank it after any move it ties with and never star it beside one, since past
the reloads the search looks through the item may still be worth keeping. When it ranks first
outright they say why: another seat holds Adrenaline and could take the item, as the dealer
takes the player's Cigarettes to heal ([DealerIntelligence.gd 243-257][di243]), or the hand is
full and the next deal needs the room (`solver/Solver.cpp`, `spendsOnly` and `spendReason`).
The game refuses four things, and so does the engine:

1. a Hand Saw on a sawed barrel ([PermissionManager.gd 65-80][pm65]);
2. a restraint on a seat that is already restrained or is owed a turn
   ([PermissionManager.gd 65-80][pm65], [DealerIntelligence.gd 177-180][di177]);
3. a second restraint in one turn, which at two seats is the second rule again, since the
   first restraint is still on;
4. taking an Adrenaline with Adrenaline ([PermissionManager.gd 65-80][pm65]).

Which item each of the game's three refusal flags names is held in the scene data (entry 11
under [Assumptions pending extraction](#assumptions-pending-extraction)). Adrenaline used on its own is a legal move: the steal runs out of time and the Adrenaline is
spent with nothing taken ([ItemManager.gd 439-455][im439], [470-480][im470]).

A hand is a list in the order its items arrived, at most eight long. Copies of a type that sit
next to each other are one move, since using any of them leaves the same hand; a type held in
more than one place gets one move per run, which the advisor names with `#k`
(`engine/Rules.h`, `legalActions`). The order matters only to the scripted dealer, which scans
items in that order. Under the other opponent models the solver sorts every hand first.

## Items

Eleven items exist. Five are the base set, four more are added by Double or Nothing, and two
exist only in multiplayer (`engine/Items.h`). The pool a mode uses is built in
`engine/Config.cpp`. Every draw takes one type from the pool with equal chance, with no cap on
how many of one type a seat holds (entries 5 and 6).

| Item | Effect | Modes | Confidence |
|---|---|---|---|
| Magnifying Glass | Shows the user the chamber, and nobody else ([ItemInteraction.gd 142-146][ii142]). Only the user's bit is set in the knowledge mask (`engine/Rules.cpp`, `applyItemEffect`), and the knowledge survives the turn change because it is stored on the tube by offset | all | Verified |
| Beer | Ejects the chamber with every seat watching ([ItemInteraction.gd 130-141][ii130]). The counts fall by one of the right kind. A Beer on the last shell starts the next load at once | all | Verified |
| Cigarettes | One charge back, capped at the round's starting charges, and nothing at all to a seat whose wire is cut ([ItemInteraction.gd 147-152][ii147], [HealthCounter.gd 141-148][hc141]) | all | Verified |
| Handcuffs | The other seat loses its next turn ([ItemInteraction.gd 118-129][ii118]). See the handcuffs section | story, Double or Nothing | Verified |
| Hand Saw | The next shot deals two charges ([ItemInteraction.gd 153-158][ii153]). Refused on a sawed barrel, so it does not stack | all | Verified |
| Burner Phone | Tells the user the type of one shell past the chamber, and nobody else ([ItemInteraction.gd 172-176][ii172], [BurnerPhone.gd 13-15][bp13]). Each shell past the chamber is equally likely, except that with 8 shells loaded a pick of the eighth becomes the seventh, so the seventh is named with chance 2 in 7 and the eighth never (`engine/Rules.cpp`, `phoneOffsetWeights`). It can name a shell the user has already seen, and with one shell left it names nothing and is spent ([BurnerPhone.gd 32][bp32]). The branch probabilities are the unresolved pool, so learning one shell shifts the odds on every other. The dealer's phone is described under [The scripted dealer](#the-scripted-dealer) | Double or Nothing, multiplayer | Verified; multiplayer seats are Assumed to use the player's phone |
| Adrenaline | Starts a steal: the user takes one item from another living seat and uses it at once, paying with the Adrenaline and the victim's copy ([ItemInteraction.gd 177-182][ii177], [64-66][ii64]; `engine/Rules.cpp`, `apply`). The thief picks the copy and, for a restraint, the victim. Adrenaline cannot be taken. Used alone it is spent with nothing taken | Double or Nothing, multiplayer | Verified; entries 7, 11 and 12 |
| Inverter | Flips the chamber, and nobody is shown the new type ([ItemInteraction.gd 165-171][ii165]). A chamber somebody has pinned down flips and the public counts move with it; a seat that had not seen it keeps the counts from before the flip, with the flip still to come (`engine/Tube.h`, `invertChamber` and `unflipFor`). A chamber nobody has seen keeps its place in the unresolved pool and records the flip, so the shell stays exchangeable while its odds invert. The dealer's Inverter is different and is described under [The scripted dealer](#the-scripted-dealer) | Double or Nothing, multiplayer | Verified |
| Expired Medicine | Even odds ([MedicineManager.gd 72-75][mm72]) of `medicineHeal` (2) charges back, capped at the starting charges and nothing with a cut wire, or one charge lost, which can kill ([ItemInteraction.gd 159-164][ii159], [MedicineManager.gd 17-38][mm17], [25-27][mm25], [HealthCounter.gd 141-148][hc141]). Both branches are real chance branches, so the solver prices the gamble rather than averaging it | Double or Nothing, multiplayer | Verified for single player; Sourced for multiplayer |
| Jammer | Marks another seat to be skipped, the same mechanism handcuffs use, and legal only in multiplayer (`engine/Rules.cpp`, `applyItemEffect` and `legalActions`). It shares the one-restraint-per-turn allowance with handcuffs, so a seat may apply at most one of either in a turn | multiplayer | Sourced as the handcuffs replacement, Assumed on sharing the one-per-turn limit |
| Remote | Reverses the turn direction (`engine/Rules.cpp`, `applyItemEffect`). Offered only in multiplayer and only while more than two seats are alive, since reversing direction at two seats changes nothing (`engine/Rules.cpp`, `legalActions`) | multiplayer | Verified |

The four items above that carry information or chance are the ones worth reading twice. The
Magnifying Glass and the Burner Phone write into a per-seat knowledge mask, so the engine
knows not only what is true but who is entitled to know it, and the solver reads a position
through the information set of the seat it is advising. The Inverter is the one item that
changes the truth without telling anybody, which is why the engine keeps an unseen shell in
the unresolved pool and inverts the odds instead of resolving it. Expired Medicine is the only
item whose outcome is a coin flip rather than a shell draw.

## Handcuffs

Handcuffs are the rule most worth stating exactly, because a small difference in wording
changes whether a seat can be locked out of the game entirely.

The engine implements three rules together, all read from the single-player scripts:

1. **A cuffed seat loses its next turn, and is owed the one after.** When the turn reaches a
   cuffed seat it is skipped ([RoundManager.gd 308-325][rm308] for the player,
   [DealerIntelligence.gd 39-57][di39] for the dealer), and its cuffs break when its following
   turn begins. The engine marks the skipped seat as owed a turn (`skipConsumed`,
   `engine/State.h`) and consumes the skip while the turn is handed on (`engine/Rules.cpp`,
   `advanceTurn`), or before the seat is asked for a move (`applyPendingSkip`).
2. **One restraint per turn.** Applying handcuffs sets `cuffUsedThisTurn`, and the flag blocks
   a second restraint until the turn changes (`engine/Rules.cpp`, `legalActions` and
   `advanceTurn`).
3. **A seat that is restrained or owed a turn cannot be restrained again until it has taken a
   turn.** The game refuses it ([PermissionManager.gd 65-80][pm65],
   [DealerIntelligence.gd 177-180][di177]), and in the engine a seat that is cuffed or carries
   `skipConsumed` is not a legal target (`engine/Rules.cpp`, `restrainableSeats`). The flag
   clears when the seat actually takes its turn.

The third rule is the one that matters. Without it, a seat holding two pairs of handcuffs
can cuff, take its turn, watch the skip consume the cuffs, and cuff again immediately, so the
cuffed seat never plays at all while the cuffer empties the tube. With it, the cuffed seat is
guaranteed one turn between any two pairs, which is what the game does.

A load removes every restraint, including a turn a seat was still owed
([RoundManager.gd 200][rm200], [HandcuffManager.gd 40-56][hm40]).

Handcuffs are absent from the multiplayer pool (`engine/Config.cpp`, `multiplayerPool`) and are
refused as a move there even if a position is typed in with a pair in hand (`engine/Rules.cpp`,
`legalActions`). Multiplayer uses the Jammer instead.

## Modes

**Story stages** (`engine/Config.cpp`, `storyRound`). Three stages, each one round. The engine
gives stage 1 two charges and no items, stage 2 four charges and 2 items per load from the base
five, and stage 3 the six charges described below and 4 items per load from the same pool. These
numbers match the Story Mode page; the game holds them in its scene data, so they are Assumed
until it is extracted (entry 2). After every load p1 moves first
([ShellLoader.gd 92-98][sl92]), and a sawed barrel survives a load that a Beer starts.

**Stage 3, and what a charge count means there.** In the third stage a hit that leaves a seat
on 2 or fewer charges cuts its wire and sets it to 1. From then on the next hit kills, and
healing does nothing ([HealthCounter.gd 91-105][hc91], [130][hc130], [141-148][hc141];
[DeathManager.gd 51-53][dm51], [94-100][dm94]). The engine shows the stage as **five charges
with a heal floor of two** (`healFloor`, `engine/Config.h`): the game's 6, 5, 4 and 3 are the
engine's 5, 4, 3 and 2, and a cut wire is the engine's 1. A shot on a seat at 2 or more leaves
it on at least 1, so a sawed shot on a seat at 2 cuts the wire rather than killing, and any shot
on a seat at 1 kills (`engine/Rules.cpp`, `shotDamage`). **A seat showing 1/5 has its wire cut,
and a seat at full health is 5/5 where the game shows 6.** Cigarettes are offered there as
anywhere else, and once the wire is cut they spend the item and change nothing. A failed Expired
Medicine is plain subtraction, though the story deals none (entry 10). The floor is a setting,
`--heal-floor`, and it is 1 everywhere else, which means healing works on any living seat.

The Story Mode page says stage 1 and the first load of stage 2 are fixed (1 live and 2 blank,
then 3 live and 2 blank, then 1 live and 1 blank). Those loads live in the scene data, which has
not been extracted, so the engine draws every story load from the general distribution
(entries 1 and 2). It takes a load as given in a position and draws only when it looks past a
reload, so a fixed load can be typed in, and `loadTable` is there for anybody who wants to pin
one. A story stage played with `play`, against either opponent, draws every load from the
general distribution, which can deal loads the stage never does.

The engine models one round at a time, so the sequencing of the three stages belongs to the
caller, not to the rules.

**Double or Nothing** (`engine/Config.cpp`, `doubleOrNothing`). Two seats and the nine-item
pool: the base five plus Burner Phone, Adrenaline, Inverter and Expired Medicine. This is the
default configuration (`engine/Config.h`), because it is the mode most positions come from.
When a round is set up the game draws, for every load in its list, the charges (2 to 4), the
shells (one of the seven loads above) and the item count (2 to 5), and turns items on for every
load ([RoundManager.gd 142-157][rm142]). The round's charges are those of its first load, both
seats start with them, and healing is capped at them ([HealthCounter.gd 126-135][hc126],
[141-148][hc141]). The advisor takes the charges a position states and defaults to 4; `play`
draws them for every round. The engine carries the item count as a range (`itemsPerLoad` and
`itemsPerLoadMax`) rather than one number. The pot, the doubling and the decision to cash out
are not part of the rules the engine implements; they sit outside a round and change nothing
inside one.

**Multiplayer** (`engine/Config.cpp`, `multiplayer`). Two to four seats (`engine/Tube.h`,
clamped in `cli/play.cpp`). No handcuffs. The pool is the nine minus Handcuffs, plus Jammer and
Remote. Turn order runs around the table in a direction the state carries (`engine/State.h`),
the next seat is the next living seat in that direction (`engine/State.cpp`, `nextSeat`), and a
Remote flips the direction for everybody. A seat at zero charges is out for the round and is
skipped by the turn order from then on. The pinned scripts cover only the single-player game,
so the multiplayer rules are Sourced from the Multiplayer page where it says something and
Assumed where it does not. The default after a reload is that whoever was to move keeps the
turn, a sawed barrel does not survive a reload, every shot spends the saw, and each seat is
dealt 2 items a load.

## Settings

Some rules are fields on `RuleConfig` (`engine/Config.h`), either because the scripts settle
them for the single-player modes and not for multiplayer, or because a reader may want to price
the other answer. `RuleConfig::describe()` prints the ones that change answers in one line
(`engine/Config.cpp`), and the solver attaches that line to every result it returns
(`solver/Solver.cpp`, `describeAssumptions`), so no answer is ever quoted without the rules it
was computed under.

Eight of these settings are also command line options on both binaries. The name of the option
is the name of the field, written the way options are written.

| Field | Option | Values |
|---|---|---|
| `reloadTurn` | `--reload-turn` | `keep`, `p1`, `dealer` |
| `sawSurvivesReload` | `--saw-survives` | `yes`, `no` |
| `reloadClearsCuffs` | `--clear-cuffs` | `yes`, `no` |
| `itemsPerLoad`, `itemsPerLoadMax` | `--items-per-load` | 0 to 8, or a range such as `2-5` |
| `itemLimit` | `--item-limit` | 1 to 8 |
| `medicineSuccess` | `--med-success` | 0 to 1 |
| `medicineHeal` | `--med-heal` | 0 to 8 |
| `healFloor` | `--heal-floor` | 1 to 8 |

The advisor also takes them one at a time as `rule <name> <value>` while a round is being
narrated, and `rules` prints what they are currently set to. Each mode starts from these values:

| Setting | Story | Double or Nothing | Multiplayer | Confidence |
|---|---|---|---|---|
| Turn owner after a mid-round reload (`reloadTurn`) | p1 first | p1 first | the seat to move keeps the turn | Verified for single player ([ShellLoader.gd 92-98][sl92]); Assumed for multiplayer |
| Sawed barrel across a reload (`sawSurvivesReload`) | survives | survives | does not survive | Verified for single player ([ItemInteraction.gd 130-141][ii130], [RoundManager.gd 196-234][rm196]); Assumed for multiplayer |
| A blank the dealer's seat fires into itself keeps the saw (`dealerSeatBlankKeepsSaw`, no option) | yes | yes | no | Verified for single player ([DealerIntelligence.gd 305-324][di305]); Assumed for multiplayer |
| A reload releases handcuffs (`reloadClearsCuffs`) | yes | yes | yes, for the Jammer | Verified for single player ([RoundManager.gd 200][rm200]); Assumed for multiplayer |
| Items dealt per load (`itemsPerLoad`, `itemsPerLoadMax`) | 0, 2 and 4 by stage | 2 to 5, modelled at 4 | 2 | Verified for Double or Nothing ([RoundManager.gd 154][rm154]); Assumed for story (entry 2) and multiplayer |
| Item limit (`itemLimit`) | 8 | 8 | 8 | Verified for single player ([ItemManager.gd 258][im258]); Assumed for multiplayer |
| Expired Medicine (`medicineSuccess`, `medicineHeal`) | not dealt (entry 10) | 50 percent, 2 back, 1 lost | the same | Verified for single player ([MedicineManager.gd 72-75][mm72]); Sourced for multiplayer |
| Heal floor (`healFloor`) | 2 in stage 3, 1 before it | 1 | 1 | Verified ([HealthCounter.gd 91-105][hc91]) |
| Charges (`charges`) | 2, 4 and 5 by stage | 4 in the advisor; drawn from 2 to 4 per round in `play` ([RoundManager.gd 145][rm145]) | 4 | Verified for Double or Nothing; Assumed for story (entry 2) and multiplayer |
| Shell loads (`loadTable`) | the general distribution | the seven loads | the general distribution | Verified for Double or Nothing ([RoundManager.gd 148-152][rm148]); Assumed otherwise (entries 1 and 2) |
| Item pool (`itemPool`) | the base five | the nine | the nine minus Handcuffs, plus Jammer and Remote | Sourced; Assumed on per-type caps (entry 5) |

The settings with a different answer in multiplayer are the ones worth trying. The turn owner
after a reload decides whether emptying the tube hands the initiative away or keeps it, which
changes whether racking the last shell with Beer is good or terrible. If restraints survived a
reload, a pair applied just before the tube empties would buy a skip in the new load as well.
Medicine at the older odds of 40 percent, from before version 1.2.1, makes the item a last
resort rather than a positive gamble. More items per load makes the item game dominate the shell
game and raises the value of reaching a reload alive.

## Assumptions pending extraction

The game's scripts read some of their rules from scene data, which the pinned decompilation does
not contain. Until those fields are extracted the engine follows the assumption under each one.
The solver points here with every answer.

1. Assumed, pending extraction of `RoundManager.batchArray` in `res://scenes/main.tscn` (each
   RoundBatch's `batchIndex`, `roundArray` size and RoundClass ids). Every load of a round is
   drawn afresh.
2. Assumed, pending extraction of the story RoundClass fields `amountLive`, `amountBlank`,
   `numberOfItemsToGrab`, `usingItems`, `startingHealth` and `isFirstRound`. Story loads use the
   general load distribution, items 0, 2 and 4, and charges 2, 4 and 6 (stage 3 modelled as 5
   with a heal floor).
3. Assumed, pending extraction of the RoundClass fields `insertingInRandomOrder` and
   `shufflingArray`. The firing order is a hidden uniform shuffle.
4. Assumed, pending extraction of RoundClass `usingItems` on the story entries. Held items stay
   usable by both seats on every load.
5. Assumed, pending extraction of `Amounts.array_amounts` (`itemName`, `amount_main`,
   `amount_don` and `amount_active`). No per-type caps; each mode's pool is as listed.
6. Assumed, pending extraction of the `array_amounts` entry count and names. Each type is listed
   once, so the draw is uniform over types.
7. Assumed, pending extraction of the `array_amounts[0]` `itemName` and the stealable types'
   amounts. Steals do not change later deals.
8. Assumed, pending extraction of the CompartmentManager `animator_compartment` "clear items"
   track. Counters reset with the table.
9. Assumed, pending extraction of the signals or animation tracks on the Double or Nothing path
   that call `ItemManager.ItemClear_Remote` (`SetupItemClear`), or that set `newBatchHasBegun`
   before `BeginItemGrabbing`. The table and the Dealer's item list are cleared at each new
   round.
10. Assumed, pending extraction of the "expired medicine" `amount_main` and the stage 3
    `startingHealth`. Expired Medicine is not dealt in story, and a failed dose costs one charge;
    if Expired Medicine were dealt in stage 3, a seat left at 2 or less by a failed dose would be
    cut to 1 by the next non-lethal hit on either seat ([HealthCounter.gd 91-105][hc91]).
11. Assumed, pending extraction of `PermissionManager.stackDisabledItemArray` at indices 0, 4
    and 5. The three refusals name the Hand Saw, Handcuffs and Adrenaline.
12. Assumed, pending extraction of the camera socket "enemy items", the player-side
    PickupIndicator colliders and the raycast mask. A steal cannot be combined with an own item.

## The scripted dealer

The single-player dealer plays by a script the game ships. `--opponent dealer` on the advisor
and on `play` puts that script in seat p2 in place of the minimising opponent described under
What is not modelled. The script is implemented in `engine/Dealer.h` and `engine/Dealer.cpp`
(`dealer::step` is one pass of it), and the solver values a dealer turn as a chance node over
the script's branches (`solver/Solver.cpp`, `dealerTurn`). The opponent model stays a choice:
the default is still the minimising opponent, named `solver` in both programs
(`--opponent solver`), and every answer names the model it used.

The rules in this document are read from a third-party decompilation of Buckshot Roulette
v2.2.0 hotfix 6, pinned at commit `34531a4c5e26ec44320c5197e2f678ce1a7b8d00` of
[thecatontheceiling/buckshotroulette](https://github.com/thecatontheceiling/buckshotroulette/tree/34531a4c5e26ec44320c5197e2f678ce1a7b8d00).
Each rule cites a file and line range there. A rule read from that script is marked Verified,
since the script is the logic the game ships. The places where this engine departs from the
script are listed under [Approximations](#approximations).

**Where it applies.** Two seats only: p1 is the player and the advised seat, p2 is the dealer.
The script has two sets of decision rules, chosen by whether the game is in its endless mode
([DealerIntelligence.gd 96][di96], [173][di173], [423][di423]). Double or Nothing runs the
endless rules and the story stages run the story rules (`dealer::brainFor`). Multiplayer has
no scripted dealer, and both binaries refuse the option there.

**A turn is a sequence of passes.** Each pass either uses one item, after which the script
calls itself again for the next pass ([DealerIntelligence.gd 266][di266]), or fires one shot
and ends the turn ([275-276][di275]). A blank fired into itself keeps the gun, and the next
turn starts afresh without the end of turn, so a sawed barrel stays sawed while shells remain
([387][di387], [394][di394], [305-324][di305]); so does the turn the dealer gets when the
player's turn is skipped by handcuffs ([RoundManager.gd 315][rm315]). Verified.

**What it remembers within a turn.** Four fields, all cleared at the end of a shot
([DealerIntelligence.gd 277-279][di277]) or at the start of a turn ([68][di68]): whether it
knows the chamber (`dealerKnowsShell`), what it knows it to be (`knownShell`), whom it means to
shoot (`dealerTarget`), and whether it has taken Expired Medicine this turn (`usingMedicine`)
([71-77][di71]). Across turns it also remembers which shells it has seen
(`sequenceArray_knownShell`, [31][di31]), a record that shifts down whenever a shell leaves the
tube ([ShellEjectManager.gd 32][se32], [44][se44], [56][se56], [70][se70]) and is emptied at
every load ([ShellSpawner.gd 113][ss113]). Verified. (This engine keeps that record as the
dealer's bit in the tube's knowledge mask, which already shifts as shells leave and is reset at
a load.)

Each pass then runs these steps in order (`dealer::step`).

**1. Deduction, endless rules only.** When it does not already know the chamber, the dealer
works it out ([DealerIntelligence.gd 96-104][di96r], `FigureOutShell` at [282-303][di282]) if it
has seen the chamber ([283][di283]), if the tube holds only one type ([288-294][di288]), or if
the shells it has seen account for every live shell or every blank one ([296-301][di296]). It
then knows the chamber and aims at the player when the shell is live and at itself when it is
blank. Verified.

**2. The last shell, both rule sets.** With one shell left the dealer knows it and aims the
same way ([DealerIntelligence.gd 106-112][di106]). Verified.

**3. The item scan, both rule sets.** The dealer lists its own items in the order they arrived
and then, when it holds Adrenaline, the player's items in the order they arrived
([DealerIntelligence.gd 118-149][di118], Adrenaline found at [127-129][di127]). It walks the list
and uses the first item whose condition holds, which ends the pass, and the use spends the first
copy of that item on its list ([261][di261]). Every condition reads the position and the memory
as they stand after steps 1 and 2. Adrenaline, Jammer and Remote are never chosen. Verified.

| Item | Used when | Effect | Confidence |
|---|---|---|---|
| Magnifying Glass ([152-159][di152]) | it does not know the chamber, and more than one shell is left | it sees the chamber, knows it, and aims at the player on a live shell and at itself on a blank | Verified |
| Cigarettes ([160-164][di160]) | it is below its starting charges | it heals one charge, capped at its starting charges, and nothing with its wire cut ([HealthCounter.gd 126-135][hc126]) | Verified |
| Expired Medicine ([165-169][di165]) | it is below its starting charges, holds no cigarettes within reach, has not taken medicine this turn, and is not on one charge | even odds ([231-235][di231]) of two charges back, capped, or one charge lost while its wire is intact ([HealthCounter.gd 131-134][hc131]) | Verified |
| Beer ([170-176][di170]) | it does not know the chamber to be live, and more than one shell is left | the chamber is ejected and everybody sees it. Under the endless rules it then forgets the chamber but keeps its target ([173-175][di173r]); under the story rules it forgets nothing | Verified |
| Handcuffs ([177-180][di177]) | the player can be cuffed, and more than one shell is left | the player is cuffed, exactly as when a seat cuffs another | Verified |
| Hand Saw ([181-186][di181]) | the barrel is not sawed, and it knows the chamber is live | the barrel is sawed | Verified |
| Burner Phone ([187-194][di187]) | more than two shells are left | it reads one shell past the chamber, each equally likely ([190][di190]), including one it or anybody else has already seen; unlike the player's phone, a pick of the eighth shell stays the eighth. Nobody else is told which shell it read. Its target and memory of the chamber are unchanged | Verified |
| Inverter ([195-201][di195]) | it knows the chamber is blank | a live shell is written into the chamber ([199][di199]), it knows it is live, and it aims at the player | Verified |

**Cigarettes within reach.** Whether the dealer holds cigarettes, for Expired Medicine, is read
from the item list the previous pass built ([DealerIntelligence.gd 113-116][di113]), before this
pass builds its own. That list held the dealer's own items and, when that pass began with the
dealer holding Adrenaline, the player's as well. Between passes a deal adds to it
([ItemManager.gd 368][im368]) and the dealer's uses and the player's steals take from it
([DealerIntelligence.gd 261][di261], [HandManager.gd 156][hm156]); the player's use of its own
items does not touch it. So the dealer counts as holding cigarettes when it holds some itself,
or when its previous pass began with Adrenaline in its hand and the player still held
cigarettes when that pass ended. The engine carries the second case as one bit,
`GameState::dealerListCigs`, written at the end of every pass (`dealer::listCigsAfterPass`),
and a written position states it with `listcigs`. Verified.

**Stealing.** An item taken from the player's side costs the dealer's first Adrenaline and the
player's first copy of that item, and acts for the dealer: the dealer heals, the player is
cuffed, the dealer sees the shell ([DealerIntelligence.gd 243-257][di243]). The dealer's own
copy is used first when it has one ([244-245][di244]). Using an item never passes the turn.
Verified.

**4. The saw coin, both rule sets.** When the scan used nothing, a saw is within its reach
(its own, or the player's through Adrenaline, [DealerIntelligence.gd 204-205][di204]), the
barrel is not sawed, and it does not know the chamber to be blank, the dealer flips its coin
([206-215][di206]). On 0 it aims at itself and goes on to the shot. On 1 it saws the barrel
with its own saw, or with the player's through Adrenaline, aims at the player, and the pass
ends. Verified.

**5. The shot.** The dealer shoots whoever it is aiming at, and when it is aiming at nobody it
flips its coin: 0 shoots itself and 1 shoots the player ([DealerIntelligence.gd 268-280][di268],
`ChooseWhoToShootRandomly` at [326-329][di326]). The shot is the engine's ordinary shot. The
turn is over. Verified.

**The coin** (`CoinFlip`, [DealerIntelligence.gd 421-431][di421]). Under the story rules it is
a fair coin ([424][di424]). Under the endless rules it counts the whole tube, which every seat
can count: more live shells than blanks always gives 1, more blanks always gives 0, and only
an even tube is a fair flip ([426-430][di426]). Verified.

**The two sets of rules, side by side.** The endless rules add three things to the story
rules: the deduction in step 1, the Beer forgetting the chamber, and the coin weighted by the
tube. Everything else is shared.

**What the player is told about the dealer's phone.** Nothing: the game does not show which
shell the dealer's phone read ([DealerIntelligence.gd 187-194][di187]). The solver averages
over every shell it could have named. A written position records each dealer phone use in the
current load by the tube size it was used at, as `phoned=p2@5,4`, and a use at two shells or
fewer is refused under the dealer model, since the dealer uses a phone only with more than two
shells in the tube. Verified.

**A written position partway through a dealer turn.** A position can carry what the dealer
settled earlier in its turn as `dealer=`: `seen` when it has seen the chamber, which the
position must also record as `known=p2:0L` or `known=p2:0B`; `believes:B` when a story-rules
Beer on a blank it had seen left it believing the next chamber is blank; `aim:self` when an
endless-rules Beer on a blank it had seen left it aiming at itself; `aim:p1` when its saw coin
chose the player and it sawed the barrel; and `med` after it took Expired Medicine this turn,
alone or after one of the others ([DealerIntelligence.gd 71-77][di71], [165-169][di165],
[170-176][di170], [206-215][di206]). `believes:B` arises only under the story rules and
`aim:self` only under the endless ones. The solver goes on from that memory. A dealer that has
seen the chamber saws only a live one ([181-186][di181]), so a sawed barrel with `dealer=seen`
tells every seat the chamber is live. While a dealer turn is narrated the advisor keeps the
memory itself and prints it with the position. Without `dealer=`, a position with the dealer to
move is taken as the start of its turn, listed under [Approximations](#approximations).

**Loads.** The dealer model changes nothing about a load. A search under it looks through the
same loads and deals as under the other models: the seven loads in Double or Nothing, and the
general distribution in the story stages. `play` against the dealer draws each load the same
way, with 2 to 5 items in Double or Nothing unless `--items-per-load` is given, so a survival
rate from a story stage in `play` is not measured on the game's own loads.

## Approximations

These are the places where the solver, the advisor or `play` departs from the game's rules,
most of them so that the search can finish. None of them is a claim about the game. The solver
prints entries 1 to 7 with every answer they apply to, in this order (`solver/Solver.cpp`,
`describeAssumptions`), and adds 8 and 9 when they apply; the advisor prints 10 when it applies.
Entries 7 and 11, which concern the scripted dealer's own rules, are also stated in
`engine/Dealer.h`.

1. **The reload budget.** The search looks through a set number of reloads (`--reloads`,
   default 2) and scores a round still going past that by each seat's share of the charges left
   (`solver/Solver.cpp`, `boundaryValue`). An answer that reached that boundary is flagged as
   truncated.
2. **One fixed spread of dealt items.** A reload deals each seat one fixed set of items, taken
   from the pool in order starting at the seat's own index, rather than a random draw
   (`engine/Rules.cpp`, `reloadOutcomes`). The game draws every item at random
   ([ItemManager.gd 229-236][im229]). Enumerating the deals would multiply the work by thousands
   per reload, for a value conditioned on what happens a reload or more ahead.
3. **One item count per reload.** Every reload deals the same number of items, the middle of the
   mode's range rounded up, so the 2 to 5 of Double or Nothing is modelled at 4
   (`RuleConfig::itemsDealtPerLoad`). The game draws the count for every load
   ([RoundManager.gd 154][rm154]). The model line at the head of every answer names the count.
4. **No inference from other seats' choices.** A choice does not infer a shell's type from what
   another seat chose to do, such as a dealer Inverter used only on a chamber it knows is blank
   (`solver/Solver.cpp`, `blindChoiceValue`). A player who makes that inference can do better
   than the value says.
5. **A limit on what another seat has seen, minimising opponent only.** At most
   `opponentKnowledgeLimit` (4) shells that only another seat has looked at are averaged over,
   and past that they are treated as seen by nobody (`solver/Solver.h`). The answer says when
   this happened. Against the dealer every such shell is averaged over, since the dealer works
   out the chamber from all of them ([DealerIntelligence.gd 282-303][di282]).
6. **Choices between moves a seat cannot tell apart.** A seat that has not seen a shell picks
   evenly between moves it cannot tell apart, ranking them as though no other seat had seen that
   shell either (`solver/Solver.cpp`, `blindChoiceValue`).
7. **The dealer's Expired Medicine below the heal floor.** A failed dose always costs the dealer
   a charge, where the script leaves a dealer whose wire is cut as it was
   ([HealthCounter.gd 131-134][hc131]). The script's guard against medicine on one charge keeps
   this from arising at the heal floors the modes use, so only `--heal-floor` above 2 can
   produce it, and the answer says so then.
8. **The node limit.** The search counts the positions it meets for the first time, and once
   that count passes `--node-limit` (default 40,000,000, accepted from 1 to 10,000,000,000) it
   scores each further new position by the charges in hand without searching it or keeping the
   score (`solver/Solver.h`, `nodeLimit`). The answer then ends with "The search stopped early;
   these chances may be off.", and the JSON output sets `nodeLimitHit`.
9. **The dealer to move with no `dealer=` memory.** A position with the dealer to move and no
   `dealer=` memory is taken as the start of its turn, so anything it settled on earlier in that
   turn (the target a coin chose before it sawed the barrel, medicine already taken, what it
   worked out about the chamber) is not carried over. The solver ranks no moves there and
   prints p1's chance.
10. **The dealer's memory after a Beer, in the advisor.** After a Dealer Beer that follows a
    shell it may have worked out from its own phone, the advisor takes the Dealer's memory as
    fresh. It prints a line when this happens.
11. **The dealer's item list at a new round.** The Dealer's item list starts every round empty.
    The game clears it with the table when a new round begins ([ItemManager.gd 125-135][im125];
    entry 9 on the Double or Nothing path), except when that round begins with no item on the
    table and no clear has yet been requested in the session. `play` starts every round with
    the list empty, and the advisor reads it from `listcigs`.

**What the item count costs.** On the position
`p1=2/4[saw,mg] p2=4/4[beer,cuff] tube=2L3B turn=p1` in Double or Nothing, against the
minimising opponent and looking through one reload, the search meets 76,253 positions for the
first time when a load deals 2 items a seat, 263,550 at 3, 1,544,973 at 4, which is the count
the solver uses, and 2,515,762 at 5. A position counts once however many lines reach it, and a
dealer pass counts separately from the position it starts from. None of these runs reached the
node limit. Each extra item is another move at every turn that does not end one, so the cost
grows faster than the count, and that is why the solver takes one count rather than a
distribution over four.

## What is not modelled

The solver answers from the information state of the seat it is advising: every shell that
seat has not seen goes back into the unresolved pool before anything is computed, so a
position that says another seat has looked at a shell does not tell the advised seat what is
in it. It also treats every seat other than the one it is advising as choosing from that
seat's own information state. A shell that the advised seat has revealed privately is put back
into the unresolved pool before the other seat picks a move, and its choice is then played out
in the position as it really is. When two of its moves look identical from what it has seen, it
is assumed to pick between them evenly, since nothing it knows separates them. Without this,
the search would hand a seat knowledge it has no way of having, and a private reveal would
make a position look worse rather than better.

**A shell only somebody else has looked at.** Erasing such a shell and stopping there would be
the wrong answer, because it would model an opponent who is as ignorant as the advised seat.
The solver instead splits the position into the ways the shell could have fallen, weighted by
drawing without replacement from the pool the advised seat cannot account for, and solves each
branch against a seat that knows which branch it is in
(`solver/Solver.cpp`, `knowledgeBranches`). Every ranked value and the value of the position
are the average over those branches. Two consequences are worth stating. The advised seat
never reads the shell, so naming it live or blank in the position gives byte-identical
answers, which is what the invariance test in `tests/test_opponent_knowledge.cpp` checks.
And only the move being asked about gets the treatment: deeper in the search the advised seat
picks as though nobody had looked, which understates the other seat in those lines. The number
of such shells the solver will enumerate against the minimising opponent is capped by
`opponentKnowledgeLimit` (`solver/Solver.h`), and a result says how many it averaged over and
whether the cap dropped any. Against the dealer there is no cap, because the dealer works out
the chamber from every shell it has seen, and every shell it has seen is drawn at the root.

**The default opponent.** Unless `--opponent dealer` is given, the other seat is not the
game's dealer. It minimises the advised seat's survival probability (`engine/Config.h`,
`OpponentModel`), within three limits: it chooses from its own information state and picks
evenly between moves it cannot tell apart (`solver/Solver.cpp`, `blindChoiceValue`), it spends
no Magnifying Glass or Burner Phone (`solver/Solver.cpp`, `legalFor`), and positions past the
reload budget are scored by each seat's share of the charges left (`solver/Solver.cpp`,
`boundaryValue`). That is a different opponent from the dealer, and it is not the strongest
opponent possible either, so an answer is not a worst-case answer. It is the chance of winning
under this stated opponent model, looking a set number of reloads ahead, and the solver prints
the model with every result (`solver/Solver.cpp`, `describeAssumptions`). The dealer itself is
described under [The scripted dealer](#the-scripted-dealer).

**Other seats spending information items.** By default the search does not let other seats use
a Magnifying Glass or a Burner Phone (`solver/Solver.h`, `opponentUsesInfoItems`, filter in
`solver/Solver.cpp`, `legalFor`). The reason is that modelling their private knowledge inside a
search run for one seat would let the search read shells it has no right to see. The effect is
that opponents are modelled slightly weaker than a player who tracks shells. The flag
`opponentUsesInfoItems` turns the behaviour back on for anybody who wants the other extreme.

**Anything above the level of one round.** The engine solves a round. The sequence of three
story stages, the Double or Nothing pot and cash-out decision, and any match record across
rounds live outside it. A round is the unit because a round is where the probabilities are.

**The dealer as a table runner in multiplayer.** Multiplayer seats are symmetric in this
engine. There is no non-competing seat that runs the table.

**Search depth.** The search looks through a fixed number of reloads and stops at a node limit,
both listed under [Approximations](#approximations). A result that touched either boundary is
flagged as truncated (`solver/Solver.h`, `truncated`), and one that hit the node limit also sets
`nodeLimitHit`. That flag is part of the answer, not a detail: a truncated value is an estimate,
and an untruncated value is the full search result under the stated rules and approximations.

## Sources

The rows marked Sourced come from the Buckshot Roulette community wiki. Its Terminology page
uses "round" for one shotgun load, which this document calls a load, so "items per round" there
means items per load.

- [Terminology](https://buckshot-roulette.fandom.com/wiki/Terminology)
- [Shotgun](https://buckshot-roulette.fandom.com/wiki/Shotgun)
- [Story Mode](https://buckshot-roulette.fandom.com/wiki/Story_Mode)
- [Double or Nothing](https://buckshot-roulette.fandom.com/wiki/Double_or_Nothing)
- [Multiplayer](https://buckshot-roulette.fandom.com/wiki/Multiplayer)
- [Hand Saw](https://buckshot-roulette.fandom.com/wiki/Hand_Saw)
- [Handcuffs](https://buckshot-roulette.fandom.com/wiki/Handcuffs)
- [Expired Medicine](https://buckshot-roulette.fandom.com/wiki/Expired_Medicine)
- [Jammer](https://buckshot-roulette.fandom.com/wiki/Jammer)
- [Remote](https://buckshot-roulette.fandom.com/wiki/Remote)

The rows marked Verified are read from the decompiled scripts pinned and linked under
[The scripted dealer](#the-scripted-dealer), not from the wiki. What the scripts leave to the
game's scene data is listed under
[Assumptions pending extraction](#assumptions-pending-extraction), and each entry names the
field that would settle it.

[bp13]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/BurnerPhone.gd#L13-L15
[bp32]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/BurnerPhone.gd#L32
[di31]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L31
[di39]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L39-L57
[di68]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L68
[di71]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L71-L77
[di96]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L96
[di96r]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L96-L104
[di106]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L106-L112
[di113]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L113-L116
[di118]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L118-L149
[di127]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L127-L129
[di152]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L152-L159
[di160]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L160-L164
[di165]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L165-L169
[di170]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L170-L176
[di173]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L173
[di173r]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L173-L175
[di177]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L177-L180
[di181]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L181-L186
[di187]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L187-L194
[di190]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L190
[di195]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L195-L201
[di199]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L199
[di204]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L204-L205
[di206]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L206-L215
[di231]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L231-L235
[di243]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L243-L257
[di244]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L244-L245
[di261]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L261
[di266]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L266
[di268]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L268-L280
[di275]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L275-L276
[di277]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L277-L279
[di282]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L282-L303
[di283]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L283
[di288]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L288-L294
[di296]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L296-L301
[di305]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L305-L324
[di326]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L326-L329
[di372]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L372-L387
[di387]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L387
[di394]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L394
[di421]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L421-L431
[di423]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L423
[di424]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L424
[di426]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L426-L430
[dm51]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DeathManager.gd#L51-L53
[dm94]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DeathManager.gd#L94-L100
[hc91]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HealthCounter.gd#L91-L105
[hc126]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HealthCounter.gd#L126-L135
[hc130]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HealthCounter.gd#L130
[hc131]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HealthCounter.gd#L131-L134
[hc141]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HealthCounter.gd#L141-L148
[hm156]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HandManager.gd#L156
[hm40]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/HandcuffManager.gd#L40-L56
[ii117]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L117-L182
[ii118]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L118-L129
[ii130]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L130-L141
[ii142]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L142-L146
[ii147]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L147-L152
[ii153]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L153-L158
[ii159]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L159-L164
[ii165]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L165-L171
[ii172]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L172-L176
[ii177]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L177-L182
[ii64]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemInteraction.gd#L64-L66
[im125]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L125-L135
[im229]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L229-L236
[im235]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L235-L236
[im258]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L258
[im279]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L279
[im346]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L346-L347
[im357]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L357-L358
[im368]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L368
[im439]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L439-L455
[im470]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ItemManager.gd#L470-L480
[mm17]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/MedicineManager.gd#L17-L38
[mm25]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/MedicineManager.gd#L25-L27
[mm72]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/MedicineManager.gd#L72-L75
[pm65]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/PermissionManager.gd#L65-L80
[rm142]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L142-L157
[rm145]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L145
[rm148]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L148-L152
[rm154]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L154
[rm196]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L196-L234
[rm197]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L197
[rm200]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L200
[rm264]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L264-L295
[rm308]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L308-L325
[rm315]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/RoundManager.gd#L315
[se32]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellEjectManager.gd#L32
[se44]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellEjectManager.gd#L44
[se56]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellEjectManager.gd#L56
[se70]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellEjectManager.gd#L70
[sg119]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShotgunShooting.gd#L119-L143
[sl92]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellLoader.gd#L92-L98
[sm14]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/SegmentManager.gd#L14-L17
[ss55]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellSpawner.gd#L55-L57
[ss71]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellSpawner.gd#L71-L81
[ss113]: https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/ShellSpawner.gd#L113
