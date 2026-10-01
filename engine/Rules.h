#pragma once

#include <array>
#include <vector>

#include "engine/Config.h"
#include "engine/State.h"

namespace bsr {

/// The rules, as pure functions over a state. Nothing here reads input, writes
/// output, holds a random number generator or owns memory beyond its return
/// value, which is what lets the same code run the game, the solver and the
/// tests.
namespace rules {

/// Every action the seat to move may take, which is every item use and steal
/// the game allows, including ones that change nothing but the hand: the game
/// refuses only a saw on a sawed barrel, a restraint on a seat that is already
/// restrained or owed a turn, a second restraint in one turn, and stealing an
/// Adrenaline (PermissionManager.gd 65-80, ItemInteraction.gd 117-182,
/// DealerIntelligence.gd 177-180).
///
/// One action is listed per run of adjacent copies of a type, because using
/// any copy in a run leaves the same hand; a type held in more than one run
/// gets one action per run, marked `named`. The order is fixed: shoot self,
/// shoot the other seats in seat order, own uses by type then copy then
/// restraint victim, Adrenaline on its own, then steals by victim seat, type,
/// copy and restraint victim.
std::vector<Action> legalActions(const GameState& state, const RuleConfig& config);

/// Apply `action`, returning every branch with its probability. Deterministic
/// actions return one branch. Chance enters in three places: resolving a shell
/// the model has not pinned down, the position a Burner Phone points at, and
/// Expired Medicine.
///
/// The returned states may have an empty tube, in which case the caller decides
/// how the reload happens: `reloadOutcomes` for a solver, `Table::load` for a
/// live game. Probabilities in the result sum to one.
std::vector<Outcome> apply(const GameState& state, const Action& action, const RuleConfig& config);

/// Every load a reload can produce, with probabilities, for a solver. When
/// `dealItems` is set, each seat is dealt one deterministic spread of items
/// from the pool rather than a distribution over deals, because enumerating
/// item multisets would multiply the state space by thousands per reload. The
/// game draws every item at random; docs/RULES.md lists this spread, and the
/// one count it deals, among the approximations.
std::vector<Outcome> reloadOutcomes(const GameState& state, const RuleConfig& config,
                                    bool dealItems);

/// The shell compositions a reload may draw, as (live, blank, probability).
std::vector<std::tuple<std::uint8_t, std::uint8_t, double>> loadDistribution(
    const RuleConfig& config);

/// Load `live` and `blank` shells into an empty tube and apply what a load does
/// to the table: a sawed barrel stays sawed where the rules say so, handcuffs
/// come off where the rules say so, the turn goes to the seat the rules name
/// (or the next living seat after it), and no restraint has been spent this
/// turn. Items are dealt separately, and the dealer's list is left alone.
void reloadInto(GameState* state, std::uint8_t live, std::uint8_t blank, const RuleConfig& config);

/// Charges a shot that hits `player` for `hit` takes away. With a heal floor
/// above one, the third story stage, a hit on a seat at or above the floor
/// leaves it on at least one charge: the game cuts the seat's life support
/// instead of killing it (DeathManager.gd 51-53, 94-100; HealthCounter.gd 91,
/// 96-105). Below the floor any hit is plain subtraction.
void shotDamage(PlayerState* player, int hit, const RuleConfig& config);

/// The chance that a Burner Phone used by `seat` at a table of `playerCount`
/// names each offset of a tube of `size` shells, indexed by offset. The
/// player's phone picks 1 to size - 1 and moves a pick of 7 to 6
/// (BurnerPhone.gd 13-15); the dealer's, seat 2 at a two-seat table, picks 1 to
/// size - 1 evenly (DealerIntelligence.gd 187-194). A tube of one shell gives
/// nothing.
std::array<double, kMaxShells> phoneOffsetWeights(int seat, int playerCount, int size);

/// Whether the seat to move is about to be skipped by handcuffs, and the state
/// that results from consuming the skip. Callers apply this before asking for
/// legal actions.
bool applyPendingSkip(GameState* state);

}  // namespace rules
}  // namespace bsr
