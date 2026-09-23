# Rules

## Scope

This document is the rule set the engine in `engine/` actually implements, written so that a
reader can check every statement against the code. It covers the single-player modes (the
story rounds and Double or Nothing) and the multiplayer mode, since all three are the same
state machine under different settings (`engine/Config.h`). The game itself has never
published a rule book, so some of what follows is verified against the game, some is sourced
to a page a reader can open, some is drawn from how the game is commonly described, and some
is a choice this engine had to make in order to compute anything at all. Every rule below
carries a marker saying which of those four it is, and every rule that is a choice names the
`RuleConfig` field that changes it.

## How to read the confidence markers

**Verified.** Confirmed against the game itself, either by watching the behaviour happen in
play or by reading the logic the game ships. A reader who disagrees with one of these should
expect to be shown the counterexample.

**Sourced.** Stated by the game's community wiki, which is cited by page name in the sources
section at the end of this document. A wiki is a secondary source and can be wrong, so this
marker sits below Verified. It does mean a reader can check the claim without owning the game,
and it means the engine is not guessing.

**Reported.** Consistent with public descriptions of the game and with how the game is
commonly played, but not confirmed against the game directly and not carried by a citable
source. These are probably right. They are not evidence.

**Assumed.** The game does not say, or the answer was never checked, and the engine still had
to pick a value in order to produce a number. Every assumed rule is a setting, or is named in
the assumptions section with the code that decides it. An assumed rule is not a claim about
the game. It is a statement about this engine.

## Core mechanics

Seats are numbered from zero internally and printed as p1, p2 and so on. A round runs until
one seat is left standing (`engine/State.h:42`).

| Rule | What the engine does | Confidence | Setting |
|---|---|---|---|
| The tube holds 2 to 8 shells | `kMaxShells` is 8 (`engine/Tube.h:10`) and the default load draws a total of 2 to 8 (`engine/Rules.cpp:394`) | Verified | `loadTable` |
| Every load holds at least one live and at least one blank | The default generator draws a live count uniform in `[1, total - 1]`, so neither count can be zero (`engine/Rules.cpp:396`) | Verified | `loadTable` |
| The exact split varies from load to load | Total uniform over 2 to 8, then live uniform over the remaining range, so blank-heavy and live-heavy loads both occur (`engine/Rules.cpp:393-400`) | Assumed | `loadTable` |
| Both counts are announced, the order is hidden | The tube stores public counts plus a per-seat knowledge mask, and an unresolved position is exchangeable with every other unresolved position (`engine/Tube.h:30-40`, `engine/Tube.cpp:21-43`) | Verified | none |
| Shooting yourself with a blank keeps the turn | The turn passes only when the shell was live, the shooter died, or the target was another seat (`engine/Rules.cpp:350-353`) | Verified | none |
| Shooting yourself with a live shell costs a charge and passes the turn | Damage is applied to the target seat, then the turn advances (`engine/Rules.cpp:347-353`) | Verified | none |
| Shooting another seat passes the turn whatever the shell was | The self-shot test at `engine/Rules.cpp:350` is the only thing that holds the turn | Verified | none |
| Any living seat may be shot, including yourself | Legal shots are self plus every other living seat (`engine/Rules.cpp:267-271`) | Verified | none |
| A sawed barrel deals two charges | The hit is 2 when the barrel is sawed and 1 otherwise (`engine/Rules.cpp:344`) | Verified | none |
| The saw is consumed by the next shot, live or blank | `sawed` is cleared after every shot (`engine/Rules.cpp:349`). Ejecting a shell with Beer does not clear it, because an ejection is not a shot (`engine/Rules.cpp:128-136`) | Verified | none |
| A sawed barrel does not survive a reload | The reload clears `sawed` unless the setting says otherwise (`engine/Rules.cpp:421`) | Sourced, and the real rule is stronger | `sawSurvivesReload` |
| A seat at zero charges is out | Damage floors at zero (`engine/Rules.cpp:17-19`) and a seat is alive while `hp > 0` (`engine/State.h:23`). Overkill from a sawed barrel does not carry over | Verified | none |
| The third story stage's faded charges are one hit that cannot be healed | Stage 3 is modelled as five charges with `healFloor` at 2 (`engine/Config.cpp`, `storyRound`). Healing a seat below the floor does nothing (`engine/Rules.cpp`, `heal`), and cigarettes are not offered there at all (`engine/Rules.cpp`, `itemIsUseful`). See the faded band paragraph in the Modes section for what a charge count means in that stage | Sourced | `healFloor` |
| The round ends when one seat remains | `roundOver()` is true at one or fewer living seats (`engine/State.h:42`) | Verified | none |
| An empty tube forces a reload | `needsReload()` is the empty tube (`engine/State.h:44`); the reload itself is `rules::reloadOutcomes` for a solver (`engine/Rules.cpp:410`) and the sampling version in `cli/play.cpp:53` for a live game | Verified | none |
| A reload deals a fresh batch of items to every living seat | Each living seat gains the same number of items, capped by the table limit (`engine/Rules.cpp:428-450`) | Verified | `itemsPerLoad`, `itemLimit`, `itemPool` |
| Double or Nothing redraws the item count at every load, 1 to 5 | The range is the setting, and the two seats are dealt the same count from it. A live game draws the count fresh per load (`cli/play.cpp:40-55`); the solver takes the middle of the range (`engine/Config.cpp`, `itemsDealtPerLoad`). See the items dealt per load row in the assumptions section | Sourced | `itemsPerLoad`, `itemsPerLoadMax` |
| Items already on a table are kept across a reload | The deal adds to the existing counts and never clears them (`engine/Rules.cpp:446`) | Verified | none |
| A seat holds at most eight items | Room is computed before the deal and the surplus is dropped (`engine/Rules.cpp:442-443`) | Verified | `itemLimit` |
| Whoever acts first after a mid-round reload | Controlled by a setting, defaulting to seat p1 in the single-player modes and to whoever was to move in multiplayer (`engine/Rules.cpp:451-460`, `engine/Config.cpp:54, 64, 74`) | Sourced for single player, Assumed for multiplayer | `reloadTurn` |
| A reload releases handcuffs | Every seat is uncuffed when a load is dealt (`engine/Rules.cpp:422-427`) | Verified for single player, Assumed for multiplayer | `reloadClearsCuffs` |
| Shells that are fired or ejected are public | Both the shot and Beer resolve the chamber with every seat as an observer (`engine/Rules.cpp:340` and `engine/Rules.cpp:129`, mask built at `engine/Rules.cpp:13-15`) | Verified | none |
| Using an item does not end the turn | `apply` advances the turn after an item only when the user died from it (`engine/Rules.cpp:370-376`) | Verified | none |
| Obviously wasted item uses are not offered | A magnifying glass on a shell the seat has already placed, a saw on a sawed barrel, cigarettes at full charges and a second pair of handcuffs in one turn are all filtered out of the legal move list (`engine/Rules.cpp:276-305`) | Engine choice, see below | none |

The filter in the last row is a convenience, not a rule of the game. It removes moves that
cannot change the position, so it cannot change which move is best. The one case where it is
visible is Expired Medicine, which stays legal at full charges (`engine/Rules.cpp:295-296`)
because the failure branch still costs a charge, so the move is not a no-op.

## Items

Eleven items exist. Five are the base set, four more are added by Double or Nothing, and two
exist only in multiplayer (`engine/Items.h:13-25`). The pool a mode uses is built in
`engine/Config.cpp:8-32`.

| Item | Effect in the engine | Modes | Confidence |
|---|---|---|---|
| Magnifying Glass | Resolves the chamber and records the answer for the user alone, so only the user's bit is set in the knowledge mask (`engine/Rules.cpp:124-127`). Nobody else learns anything, and the knowledge survives the turn change because it is stored on the tube by offset | all | Verified |
| Beer | Resolves the chamber with every seat watching, then ejects it (`engine/Rules.cpp:128-136`). The counts fall by one of the right kind, and everybody sees which | all | Verified |
| Cigarettes | One charge back, capped at the seat's starting charges (`engine/Rules.cpp:137-143`, cap at `engine/Rules.cpp:21-24`) | all | Verified |
| Handcuffs | Marks another seat to be skipped. See the handcuffs section | story, Double or Nothing | Verified |
| Hand Saw | Sets the barrel to deal two charges on the next shot (`engine/Rules.cpp:153-159`). No stacking: the move is not offered on an already sawed barrel | all | Verified |
| Burner Phone | Tells the user the type of one shell past the chamber, chosen uniformly among the positions past the chamber that the user has not already seen (`engine/Rules.cpp:170-208`, candidates at `engine/Rules.cpp:87-93`). The answer is private to the user, and the branch probabilities are the unresolved pool, so learning a shell correctly shifts the odds on every other position | Double or Nothing, multiplayer | Sourced on what it excludes, Assumed on the distribution |
| Adrenaline | Takes one item from another living seat and uses it immediately, paying for both (`engine/Rules.cpp:229-241`, `engine/Rules.cpp:361-363`). Adrenaline cannot take Adrenaline (`engine/Rules.cpp:323`) | Double or Nothing, multiplayer | Reported |
| Inverter | Flips the chamber. A chamber somebody has already pinned down flips outright and the public counts move with it. A chamber nobody has seen keeps its place in the unresolved pool and records the flip, so the shell stays exchangeable while its odds invert (`engine/Rules.cpp:160-169`, `engine/Tube.cpp:87-102`, odds at `engine/Tube.cpp:24-29`, counts corrected when the shell finally fires at `engine/Tube.cpp:50-66`). Nobody learns the type from an inversion | Double or Nothing, multiplayer | Verified |
| Expired Medicine | With probability `medicineSuccess` the user gains `medicineHeal` charges, capped at the starting charges; otherwise the user loses one charge, and may die from it (`engine/Rules.cpp:209-221`). Both branches are returned as real chance branches, so the solver prices the gamble rather than averaging it | Double or Nothing, multiplayer | Sourced on both the outcomes and the odds |
| Jammer | Marks another seat to be skipped, the same mechanism handcuffs use, and legal only in multiplayer (`engine/Rules.cpp:144-152`, `engine/Rules.cpp:297-298`). It shares the one-restraint-per-turn allowance with handcuffs, so a seat may apply at most one of either in a turn | multiplayer | Sourced as the handcuffs replacement, Assumed on sharing the one-per-turn limit |
| Remote | Reverses the turn direction (`engine/Rules.cpp:222-228`). Offered only in multiplayer and only while more than two seats are alive, since reversing direction at two seats changes nothing (`engine/Rules.cpp:299-300`) | multiplayer | Verified |

The four items above that carry information or chance are the ones worth reading twice. The
Magnifying Glass and the Burner Phone write into a per-seat knowledge mask, so the engine
knows not only what is true but who is entitled to know it, and the solver reads a position
through the information set of the seat it is advising. The Inverter is the one item that
changes the truth without telling anybody, which is why the engine keeps the shell in the
unresolved pool and inverts the odds instead of resolving it. Expired Medicine is the only
item whose outcome is a coin flip rather than a shell draw.

## Handcuffs

Handcuffs are the rule most worth stating exactly, because a small difference in wording
changes whether a seat can be locked out of the game entirely.

The engine implements three rules together:

1. **A cuffed seat is skipped when the turn reaches it, and the cuffs come off at that
   moment.** The skip is consumed while the turn is being handed on
   (`engine/Rules.cpp:29-45`), or before the seat is asked for a move
   (`engine/Rules.cpp:248-258`). The cuffs are not removed when they are applied, and not at
   the end of the cuffer's turn. They are removed by the skip they cause.
2. **One pair per turn.** Applying handcuffs sets `cuffUsedThisTurn`
   (`engine/Rules.cpp:149`), and the flag blocks a second pair until the turn changes
   (`engine/Rules.cpp:288`, cleared at `engine/Rules.cpp:44`).
3. **A seat that was just skipped cannot be cuffed again until it has taken a turn.** The
   skip sets `skipConsumed` on that seat (`engine/State.h:21`, set at `engine/Rules.cpp:35`
   and `engine/Rules.cpp:252`), and a seat carrying that flag is not a legal target
   (`engine/Rules.cpp:105-114`). The flag clears when the seat actually takes the turn
   (`engine/Rules.cpp:43`, `engine/Rules.cpp:255`).

The third rule is the one that matters. Without it, a seat holding two pairs of handcuffs
can cuff, take its turn, watch the skip consume the cuffs, and cuff again immediately, so the
cuffed seat never plays at all while the cuffer empties the tube. With it, the cuffed seat is
guaranteed one turn between any two pairs, which is what the game does.

Handcuffs are absent from the multiplayer pool (`engine/Config.cpp:22-32`) and are refused as
a move there even if a position is typed in with a pair in hand
(`engine/Rules.cpp:288`). Multiplayer uses the Jammer instead.

## Modes

**Story rounds** (`engine/Config.cpp`, `storyRound`). Three stages. Stage 1 gives 2 charges
and deals no items at all, and uses no pool. Stage 2 gives 4 charges and deals 2 items per
load from the base five. Stage 3 gives 4 normal charges **and two faded ones**, and deals 4
items per load from the same pool. The charges and the item counts are Sourced from the Story
Mode page. After a reload the turn returns to p1, which is Sourced from the Shotgun page and
holds in every single-player stage.

**The faded band, and what a charge count means in stage 3.** The Story Mode page says that
once a seat loses its last normal charge the machine cuts the cables above its side, any
damage from then on is fatal, and healing items stop having any effect. So the faded pair is
not two more hits: it is exactly one more hit, and it cannot be healed. The engine models the
stage as **five charges with a heal floor of two** (`healFloor`, `engine/Config.h`), which
reproduces that rule exactly: a seat absorbs four hits, survives on one charge, dies to the
fifth, and cannot be healed once it is there. The arithmetic is `charges here = normal charges
in the game + 1`, so **a seat showing 1/5 in a stage 3 position has no normal charges left and
a seat at full health is 5/5 where the game draws four**. Cigarettes are not even offered in
the band, because they would change nothing (`engine/Rules.cpp`, `itemIsUseful`). Expired
Medicine stays offered, because its failing branch still kills. The floor is a setting,
`--heal-floor`, and it is 1 everywhere else, which means healing works on any living seat.

One thing on that page is deliberately not implemented. Stage 1 and the first round of stage 2
use fixed loads rather than random ones (1 live and 2 blank, then 3 live and 2 blank, then 1
live and 1 blank). The engine takes a load as given in a position and draws only when it looks
past a reload, so a fixed load is typed in rather than configured, and `loadTable` is there for
anybody who wants to pin it.

The engine models one round at a time, so the sequencing of the three stages belongs to the
caller, not to the rules.

**Double or Nothing** (`engine/Config.cpp`, `doubleOrNothing`). Two seats and the nine-item
pool: the base five plus Burner Phone, Adrenaline, Inverter and Expired Medicine
(`engine/Config.cpp:13-20`). This is the default configuration (`engine/Config.h`), because it
is the mode most positions come from. The mode randomises three quantities, and the Sourced
ranges are 2 to 8 shells, 1 to 5 items per load, and 2 to 4 charges, with both seats given
matching counts and each drawing its own items. Charges are settled before a position is typed
in, so the engine takes them as given and defaults to 4; the item count is redrawn at every
load, so the engine carries the range (`itemsPerLoad` and `itemsPerLoadMax`) rather than one
number. The pot, the doubling and the decision to cash out are not part of the rules the engine
implements; they sit outside a round and change nothing inside one.

**Multiplayer** (`engine/Config.cpp:68-76`). Two to four seats (`engine/Tube.h:11`, clamped in
`cli/play.cpp:137`). No handcuffs. The pool is the nine minus Handcuffs, plus Jammer and
Remote (`engine/Config.cpp:22-32`). Turn order runs around the table in a direction the state
carries (`engine/State.h:35`), the next seat is the next living seat in that direction
(`engine/State.cpp:36-45`), and a Remote flips the direction for everybody
(`engine/Rules.cpp:222-228`). A seat at zero charges is out for the round and is skipped by
the turn order from then on. The default after a reload in multiplayer is that whoever was to
move keeps the turn (`engine/Config.cpp`), which is a different default from the single-player
modes. The single-player answer is sourced; this one is not, because nothing documents it, and
the Multiplayer page covers the item pool and nothing about a reload.

## Assumptions, and how to change them

Everything in this section is a rule the engine had to pick a value for, or a rule that is
known and is still a setting because the answer changes what a move is worth. None of the
rows marked Assumed is a claim about the game. Each one is a field on `RuleConfig`
(`engine/Config.h`), or, where noted, a modelling choice in the code with no setting behind it.
`RuleConfig::describe()` prints the ones that change answers in one line
(`engine/Config.cpp`), and the solver attaches that line to every result it returns
(`solver/Solver.cpp`, `describeAssumptions`), so no answer is ever quoted without the
assumptions it was computed under.

Eight of these settings are also command line options on both binaries, so a rule can be
checked against a real game without a compiler. The name of the option is the name of the
field, written the way options are written.

| Field | Option | Values |
|---|---|---|
| `reloadTurn` | `--reload-turn` | `keep`, `p1`, `dealer` |
| `sawSurvivesReload` | `--saw-survives` | `yes`, `no` |
| `reloadClearsCuffs` | `--clear-cuffs` | `yes`, `no` |
| `itemsPerLoad`, `itemsPerLoadMax` | `--items-per-load` | 0 to 8, or a range such as `1-5` |
| `itemLimit` | `--item-limit` | 1 to 8 |
| `medicineSuccess` | `--med-success` | 0 to 1 |
| `medicineHeal` | `--med-heal` | 0 to 8 |
| `healFloor` | `--heal-floor` | 1 to 8 |

The advisor also takes them one at a time as `rule <name> <value>` while a round is being
narrated, and `rules` prints what they are currently set to. The remaining assumptions in
this section have no setting behind them and are named as such in the table below.

| Assumption | Current value | Field | What a different choice changes |
|---|---|---|---|
| Turn owner after a mid-round reload | Seat p1 acts first in the single-player modes, the seat to move keeps the turn in multiplayer (`engine/Rules.cpp:451-460`) | `reloadTurn` | No longer an assumption in single player: the player is handed the shotgun first after every load whatever happened before it. It stays a setting because multiplayer is not sourced and because it decides whether emptying the tube hands the initiative away or keeps it, which changes whether racking the last shell with Beer is good or terrible |
| A sawed barrel across a reload | Does not survive (`engine/Rules.cpp:421`) | `sawSurvivesReload` | No longer an assumption: the barrel regrows at the end of the turn, which is stronger than clearing it at a reload and is what the engine already does. The setting stays so the other answer can be priced |
| A reload releasing handcuffs | Releases them (`engine/Rules.cpp:422-427`) | `reloadClearsCuffs` | If cuffs survive a reload, a pair applied just before the tube empties buys a skip in the new load as well, which roughly doubles what a late pair is worth |
| The shell composition generator | Total uniform over 2 to 8, then live count uniform over `[1, total - 1]` (`engine/Rules.cpp:393-400`) | `loadTable` | This is the distribution every probability in the engine is conditioned on before any shell is seen. A different table changes the value of every position that looks past a reload. A fixed list of `(live, blank)` pairs in `loadTable` replaces the generator outright and is drawn uniformly (`engine/Rules.cpp:383-389`) |
| Items dealt per load | 1 to 5 per seat in Double or Nothing, 2 in story stage 2, 4 in story stage 3, none in stage 1, and 2 in multiplayer; table limit 8 throughout (`engine/Config.cpp`) | `itemsPerLoad`, `itemsPerLoadMax`, `itemLimit`, `itemPool` | Only the multiplayer count and the table limit are still assumed. More items per load makes the item game dominate the shell game, and it raises the value of reaching a reload alive |
| The item count a solved reload hands out | The middle of the range, rounded up, so Double or Nothing deals 3 (`engine/Config.cpp`, `itemsDealtPerLoad`) | none | A live game draws the count fresh at every load (`cli/play.cpp:40-55`). The solver takes one count because the deal is already priced at one representative outcome over which items come out of the box, so a chance node over how many come out would multiply the branching without making the answer truer. The range is what the flag sets and what `describe()` prints, so the rounding is visible in every answer |
| Expired Medicine odds and heal | 50 percent success, 2 charges on success, 1 charge lost on failure (`engine/Config.h`, applied at `engine/Rules.cpp:209-221`) | `medicineSuccess`, `medicineHeal` | No longer an assumption: these are the numbers the game has used since version 1.2.1, before which it was 40 percent. The setting stays because the older odds make the item a last resort rather than a positive gamble, and because it is the cleanest way to see how much the ranking depends on a number |
| Charges per round | 4 by default, 2 then 4 then 4 in the story stages (`engine/Config.h`, `engine/Config.cpp`) | `charges` | Charges set how long a round runs and therefore how many reloads a position can reach. They also set the cap on healing, since cigarettes and medicine both cap at the starting value |
| Multiplayer item pool and the absence of handcuffs | The nine minus Handcuffs, plus Jammer and Remote (`engine/Config.cpp:22-32`) | `itemPool` | Restoring handcuffs to the multiplayer pool would restore the skip chain rules with them, since the engine applies the same code either way |
| The item deal at a reload, inside the solver | Modelled as one deterministic spread over the pool rather than a distribution over multisets (`engine/Rules.cpp:428-450`, spread at `engine/Rules.cpp:445`) | none | See below |
| The position a Burner Phone points at | Uniform over the positions past the chamber that the user has not already seen (`engine/Rules.cpp:87-93`) | none | The exclusion is sourced: the phone never names the chamber, and the chamber is the only thing a Magnifying Glass reads, so the two descriptions cover the same set. The distribution over the rest is the assumption, and a phone that can repeat a position the user already knows is slightly worse than the engine prices it |
| The victim of a restraint taken with Adrenaline | Chosen by the thief. Every legal victim is a separate move, so the search values each one (`engine/Rules.cpp`, the Adrenaline case, and the move generator) | none | This is no longer an assumption. It is recorded because the engine used to pick the victim itself, which with three seats meant a stolen pair of handcuffs went back on the seat it was taken from |

**The item deal approximation.** When the solver looks through a reload, it does not enumerate
the item multisets each seat might receive. It gives each living seat `itemsPerLoad` items
spread deterministically over the pool (`engine/Rules.cpp:428-450`). The honest description is
that the solver prices the deal at one representative outcome rather than at its expectation
over every possible deal. Enumerating exactly would multiply the state space by several
thousand per reload, and the resulting number is a probability the search is conditioning on
two or more reloads out, where the ranking of the move being asked about is already stable.
This is the one approximation in the engine that is not a rule question: the live game deals
items randomly from the pool and draws the count fresh at every load (`cli/play.cpp:40-55`),
and only the solver rounds it.

The count itself is the most expensive number in the engine. On the position the README opens
with, looking through one reload, the search examines 434,575 states when a load deals 2 items
a seat, 1,597,228 at 3, and 9,429,635 at 4. Each extra item is another move at every turn that
does not end one, so the cost is closer to multiplicative than additive, and that is why the
solver takes one count rather than a distribution over five.

## What is not modelled

The solver answers from the information state of the seat it is advising: every shell that
seat has not seen goes back into the unresolved pool before anything is computed, so a
position that says another seat has looked at a shell does not tell the advised seat what is
in it. It also treats every seat other than the one it is advising as choosing from that
seat's own information state. A shell that the advised seat has revealed privately is put back into
the unresolved pool before the other seat picks a move, and its choice is then played out in
the position as it really is. When two of its moves look identical from what it has seen, it
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
of such shells the solver will enumerate is capped by `opponentKnowledgeLimit`
(`solver/Solver.h`), and a result says how many it averaged over and whether the cap dropped
any.

**The scripted dealer.** The single-player dealer follows a policy the game ships. That policy
is not implemented here, and it is deliberately absent rather than guessed
(`engine/Config.h:28-33`). The solver's opponent instead minimises the advised seat's
survival probability (`solver/Solver.h:14-15`, `solver/Solver.cpp:202-213`), within three
limits: it chooses from its own information state and picks evenly between moves it cannot
tell apart (`solver/Solver.cpp:233-253`), it spends no Magnifying Glass or Burner Phone
(`solver/Solver.cpp:263-280`), and positions past the reload budget are scored by each seat's
share of the charges left (`solver/Solver.cpp:28-38`). That is a different opponent from the
dealer, and it is not the strongest opponent possible either, so an answer is not a worst-case
answer. It is the chance of winning under this stated opponent model, looking a set number of
reloads ahead, and the solver prints the model with every result (`solver/Solver.cpp`,
`describeAssumptions`).

**Other seats spending information items.** By default the search does not let other seats use
a Magnifying Glass or a Burner Phone (`solver/Solver.h:24-28`, filter at
`solver/Solver.cpp:263-280`). The reason is that modelling their private knowledge inside a
search run for one seat would let the search read shells it has no right to see. The effect is
that opponents are modelled slightly weaker than a player who tracks shells. The flag
`opponentUsesInfoItems` turns the behaviour back on for anybody who wants the other extreme.

**Anything above the level of one round.** The engine solves a round. The sequence of three
story rounds, the Double or Nothing pot and cash-out decision, and any match record across
rounds live outside it. A round is the unit because a round is where the probabilities are.

**The dealer as a table runner in multiplayer.** Multiplayer seats are symmetric in this
engine. There is no non-competing seat that runs the table.

**Search depth.** The search looks through a fixed number of reloads
(`solver/Solver.h:19-22`). Beyond that boundary it stops and scores the position by charges in
hand (`solver/Solver.cpp:28-38`), and it flags any result that touched the boundary as
truncated (`solver/Solver.h:54`). That flag is part of the answer, not a detail: a truncated
value is an estimate, and an untruncated value is the full search result under the stated rules
and the item deal approximation.

## Sources

The rows marked Sourced come from the Buckshot Roulette community wiki. Its Terminology page
defines a round as one shotgun load, which is the unit the other pages use, so "items per
round" there means items per load.

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

Three rules are still Assumed and no source settles them: whether a restraint survives a
mid-round reload, the size of a seat's item tray, and the shape of the shell composition
distribution inside the range the wiki gives. Each is a setting, and each is worth an hour of
play to settle.
