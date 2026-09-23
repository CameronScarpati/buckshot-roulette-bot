#pragma once

#include <vector>

#include "engine/Config.h"
#include "engine/State.h"

namespace bsr {

/// The rules, as pure functions over a state. Nothing here reads input, writes
/// output, holds a random number generator or owns memory beyond its return
/// value, which is what lets the same code run the game, the solver and the
/// tests.
namespace rules {

/// Every action the seat to move may legally take, with obviously wasted moves
/// filtered out: a magnifying glass on a shell the seat already knows, a saw on
/// an already sawed barrel, cigarettes at full charges, handcuffs on a seat that
/// is already cuffed or a second pair in one turn.
std::vector<Action> legalActions(const GameState& state, const RuleConfig& config);

/// Apply `action`, returning every branch with its probability. Deterministic
/// actions return one branch. Chance enters in three places: resolving a shell
/// the model has not pinned down, the position a Burner Phone points at, and
/// Expired Medicine.
///
/// The returned states may have an empty tube, in which case the caller decides
/// how the reload happens: `reloadOutcomes` for a solver, `sampleReload` for a
/// live game. Probabilities in the result sum to one.
std::vector<Outcome> apply(const GameState& state, const Action& action, const RuleConfig& config);

/// Every load a reload can produce, with probabilities, for a solver. Item
/// deals are folded in as the expected deal when `dealItems` is set, because
/// enumerating item multisets exactly would multiply the state space by
/// thousands for no change in the ranking of the current move.
std::vector<Outcome> reloadOutcomes(const GameState& state, const RuleConfig& config,
                                    bool dealItems);

/// The shell compositions a reload may draw, as (live, blank, probability).
std::vector<std::tuple<std::uint8_t, std::uint8_t, double>> loadDistribution(
    const RuleConfig& config);

/// Whether the seat to move is about to be skipped by handcuffs, and the state
/// that results from consuming the skip. Callers apply this before asking for
/// legal actions.
bool applyPendingSkip(GameState* state);

}  // namespace rules
}  // namespace bsr
