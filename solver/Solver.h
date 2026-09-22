#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/Config.h"
#include "engine/Rules.h"
#include "engine/State.h"

namespace bsr {

struct SolveOptions {
  /// The seat whose chance of surviving the round is being maximised.
  int seat = 0;

  OpponentModel opponent = OpponentModel::Optimal;

  /// How many reloads the search looks through before it stops recursing. Each
  /// extra reload multiplies the work, and two is enough to settle the ranking
  /// in the positions this was tried on.
  int reloadBudget = 2;

  /// Whether other seats may spend magnifying glasses and burner phones. Off by
  /// default: modelling knowledge that this seat cannot see would let the search
  /// read shells it has no right to, so the opponent is modelled as playing on
  /// public information, which understates them slightly.
  bool opponentUsesInfoItems = false;

  /// Guard rail. The search reports truncation rather than running forever.
  long long nodeLimit = 40000000;
};

struct ActionValue {
  Action action;
  double value = 0.0;  ///< probability that the solved seat survives the round
};

struct SolveResult {
  double value = 0.0;
  std::vector<ActionValue> ranked;  ///< best first
  long long nodes = 0;
  bool truncated = false;  ///< a node hit the reload budget or the node limit
  std::string assumptions;

  bool hasTie() const;
  /// The actions that tie at the front of the ranking, which is the best play
  /// for whichever seat is holding the gun.
  std::vector<Action> bestActions(double tolerance = 1e-9) const;
};

/// Solve a position exactly under the stated model. The value is the
/// probability that `options.seat` is the last player standing in this round.
SolveResult solve(const GameState& state, const RuleConfig& config, const SolveOptions& options);

/// The value of a position without ranking the moves, for tests and the oracle
/// comparison.
double solveValue(const GameState& state, const RuleConfig& config, const SolveOptions& options);

/// One line naming every assumption that could change the answer.
std::string describeAssumptions(const RuleConfig& config, const SolveOptions& options);

}  // namespace bsr
