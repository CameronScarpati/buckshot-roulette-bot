#include "solver/Solver.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace bsr {
namespace {

struct MemoKey {
  GameState state;
  int reloadsLeft;
  bool operator==(const MemoKey& other) const {
    return reloadsLeft == other.reloadsLeft && state == other.state;
  }
};

struct MemoHash {
  std::size_t operator()(const MemoKey& key) const noexcept {
    std::size_t h = std::hash<GameState>{}(key.state);
    h ^=
        static_cast<std::size_t>(key.reloadsLeft) * static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
    return h;
  }
};

/// Value used when the search runs out of reload budget. Stated rather than
/// hidden: the round is treated as decided by charges in hand, which is the
/// least committal assumption available at that boundary. Every result that
/// touches it is reported as truncated.
double boundaryValue(const GameState& state, int seat) {
  int mine = state.players[seat].hp;
  int total = 0;
  for (int i = 0; i < state.playerCount; ++i) total += state.players[i].hp;
  if (total <= 0) return 0.0;
  return static_cast<double>(mine) / static_cast<double>(total);
}

/// True when the tube holds a shell that has been pinned down but that `seat`
/// has not seen. That is exactly the case where a seat must not be allowed to
/// choose as though it could see it.
bool hasHiddenShell(const GameState& state, int seat) {
  const int shells = std::min<int>(state.tube.size(), kMaxShells);
  for (int i = 0; i < shells; ++i) {
    if (state.tube.truth[i] != Shell::Unknown && !state.tube.knows(seat, i)) return true;
  }
  return false;
}

/// The position as `seat` sees it: every shell it has not observed goes back
/// into the unresolved pool, where it is exchangeable again.
GameState blindedTo(const GameState& state, int seat) {
  GameState blind = state;
  const int shells = std::min<int>(blind.tube.size(), kMaxShells);
  for (int i = 0; i < shells; ++i) {
    if (blind.tube.knows(seat, i)) continue;
    blind.tube.truth[i] = Shell::Unknown;
    blind.tube.knownBy[i] = 0;
  }
  return blind;
}

class Search {
 public:
  Search(const RuleConfig& config, const SolveOptions& options)
      : config_(config), options_(options) {}

  double value(const GameState& start, int reloadsLeft) {
    GameState state = start;
    if (state.roundOver()) {
      return state.soleSurvivor() == options_.seat ? 1.0 : 0.0;
    }
    if (++nodes_ > options_.nodeLimit) {
      truncated_ = true;
      return boundaryValue(state, options_.seat);
    }
    if (state.needsReload()) {
      if (reloadsLeft <= 0) {
        truncated_ = true;
        return boundaryValue(state, options_.seat);
      }
      double total = 0.0;
      for (const Outcome& branch : rules::reloadOutcomes(state, config_, true)) {
        total += branch.probability * value(branch.state, reloadsLeft - 1);
      }
      return total;
    }
    // A seat that arrives cuffed is skipped before it can be asked for a move.
    while (rules::applyPendingSkip(&state)) {
      if (state.roundOver()) {
        return state.soleSurvivor() == options_.seat ? 1.0 : 0.0;
      }
    }

    const MemoKey key{state, reloadsLeft};
    auto found = memo_.find(key);
    if (found != memo_.end()) return found->second;

    const bool maximising = state.current == options_.seat;
    std::vector<Action> actions = legalFor(state);
    double best = maximising ? -1.0 : 2.0;
    if (actions.empty()) {
      best = boundaryValue(state, options_.seat);
      truncated_ = true;
    } else if (maximising || !hasHiddenShell(state, state.current)) {
      for (const Action& action : actions) {
        const double candidate = actionValue(state, action, reloadsLeft);
        best = maximising ? std::max(best, candidate) : std::min(best, candidate);
      }
    } else {
      best = opponentValue(state, actions, reloadsLeft);
    }
    memo_.emplace(key, best);
    return best;
  }

  /// The value of a position where the seat to move would otherwise be choosing
  /// with sight of a shell it has never seen.
  ///
  /// The seat picks from its own information state, which is the position with
  /// every shell it has not observed returned to the unresolved pool. Its
  /// choice is then played out in the position as it really is. When it cannot
  /// tell two options apart, it is assumed to pick among them evenly, which is
  /// the only assumption available: nothing it knows separates them.
  double opponentValue(const GameState& state, const std::vector<Action>& actions,
                       int reloadsLeft) {
    const GameState blind = blindedTo(state, state.current);
    std::vector<Action> chosen;
    double bestSeen = 2.0;
    for (const Action& action : actions) {
      const double candidate = actionValue(blind, action, reloadsLeft);
      if (candidate < bestSeen - 1e-12) {
        bestSeen = candidate;
        chosen.clear();
        chosen.push_back(action);
      } else if (std::abs(candidate - bestSeen) <= 1e-12) {
        chosen.push_back(action);
      }
    }
    if (chosen.empty()) return boundaryValue(state, options_.seat);
    double total = 0.0;
    for (const Action& action : chosen) total += actionValue(state, action, reloadsLeft);
    return total / static_cast<double>(chosen.size());
  }

  double actionValue(const GameState& state, const Action& action, int reloadsLeft) {
    double total = 0.0;
    for (const Outcome& branch : rules::apply(state, action, config_)) {
      total += branch.probability * value(branch.state, reloadsLeft);
    }
    return total;
  }

  std::vector<Action> legalFor(const GameState& state) const {
    std::vector<Action> actions = rules::legalActions(state, config_);
    if (state.current == options_.seat || options_.opponentUsesInfoItems) return actions;
    const auto readsShells = [](Item item) {
      return item == Item::MagnifyingGlass || item == Item::BurnerPhone;
    };
    actions.erase(
        std::remove_if(actions.begin(), actions.end(),
                       [&readsShells](const Action& action) {
                         if (action.kind != Action::Kind::UseItem) return false;
                         // Adrenaline can reach for one of these too.
                         return readsShells(action.item) ||
                                (action.item == Item::Adrenaline && readsShells(action.stolen));
                       }),
        actions.end());
    if (actions.empty()) actions.push_back(Action::shoot(state.current));
    return actions;
  }

  long long nodes() const { return nodes_; }
  bool truncated() const { return truncated_; }

 private:
  const RuleConfig& config_;
  const SolveOptions& options_;
  std::unordered_map<MemoKey, double, MemoHash> memo_;
  long long nodes_ = 0;
  bool truncated_ = false;
};

}  // namespace

bool SolveResult::hasTie() const {
  if (ranked.size() < 2) return false;
  return std::abs(ranked[0].value - ranked[1].value) < 1e-9;
}

std::vector<Action> SolveResult::bestActions(double tolerance) const {
  std::vector<Action> best;
  if (ranked.empty()) return best;
  const double top = ranked.front().value;
  for (const ActionValue& entry : ranked) {
    if (std::abs(top - entry.value) <= tolerance) best.push_back(entry.action);
  }
  return best;
}

std::string describeAssumptions(const RuleConfig& config, const SolveOptions& options) {
  std::ostringstream out;
  out << "Model: " << config.describe() << ". ";
  switch (options.opponent) {
    case OpponentModel::Optimal:
      out << "The other seat plays to minimise your chance of surviving the round";
      break;
    case OpponentModel::Paranoid:
      out << "Every other seat plays to minimise your chance of surviving the round";
      break;
  }
  if (!options.opponentUsesInfoItems) {
    out << ", spending no magnifying glasses or burner phones, so it is modelled "
           "slightly weaker than a player who tracks shells";
  }
  out << ", and choosing from what it has seen rather than from what you have seen, "
         "picking evenly between moves it cannot tell apart";
  out << ". The answer is given from what the advised seat has seen, so a shell "
         "only somebody else has looked at counts as unseen. Values are the probability "
         "of being the last player standing in this round, looking through "
      << options.reloadBudget << " reload" << (options.reloadBudget == 1 ? "" : "s") << ".";
  return out.str();
}

SolveResult solve(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  SolveResult result;
  result.assumptions = describeAssumptions(config, options);
  if (options.seat < 0 || options.seat >= state.playerCount) {
    // Nothing sensible can be said about a seat that is not at the table, and
    // the value would otherwise be read from past the end of the seats.
    result.assumptions = "No such seat in this position.";
    return result;
  }

  // Answer from the information state of the seat being advised. A shell it
  // has not seen goes back into the unresolved pool, so the search cannot tell
  // it what is in the chamber on the strength of somebody else having looked.
  GameState start = blindedTo(state, options.seat);
  while (rules::applyPendingSkip(&start)) {
    if (start.roundOver()) break;
  }

  result.mover = start.current;
  Search search(config, options);
  if (start.roundOver()) {
    result.value = start.soleSurvivor() == options.seat ? 1.0 : 0.0;
    return result;
  }
  if (start.needsReload()) {
    result.value = search.value(start, options.reloadBudget);
    result.nodes = search.nodes();
    result.truncated = search.truncated();
    return result;
  }

  // Values are always the solved seat's chance of surviving. The ordering
  // belongs to whoever is holding the gun, and when that is an opponent it has
  // to rank its moves by what it can see rather than by what we have seen.
  const bool mine = static_cast<int>(start.current) == options.seat;
  const bool hidden = !mine && hasHiddenShell(start, start.current);
  const GameState seen = hidden ? blindedTo(start, start.current) : start;

  struct Ranked {
    Action action;
    double value = 0.0;  ///< the solved seat's chance, in the position as it is
    double key = 0.0;    ///< what the seat holding the gun is choosing on
  };
  std::vector<Ranked> rows;
  for (const Action& action : search.legalFor(start)) {
    Ranked row;
    row.action = action;
    row.value = search.actionValue(start, action, options.reloadBudget);
    row.key = hidden ? search.actionValue(seen, action, options.reloadBudget) : row.value;
    rows.push_back(row);
  }
  std::stable_sort(rows.begin(), rows.end(), [mine](const Ranked& a, const Ranked& b) {
    return mine ? a.key > b.key : a.key < b.key;
  });
  for (const Ranked& row : rows) {
    ActionValue entry;
    entry.action = row.action;
    entry.value = row.value;
    result.ranked.push_back(entry);
  }
  // Read the position's worth from the search rather than from the front of the
  // list: an opponent that cannot tell its options apart makes the position
  // worth the average over them, not the worst of them.
  result.value =
      rows.empty() ? boundaryValue(start, options.seat) : search.value(start, options.reloadBudget);
  result.nodes = search.nodes();
  result.truncated = search.truncated();
  return result;
}

double solveValue(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  return solve(state, config, options).value;
}

}  // namespace bsr
