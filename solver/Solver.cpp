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
    }
    for (const Action& action : actions) {
      const double candidate = actionValue(state, action, reloadsLeft);
      best = maximising ? std::max(best, candidate) : std::min(best, candidate);
    }
    memo_.emplace(key, best);
    return best;
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
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [](const Action& action) {
                                   return action.kind == Action::Kind::UseItem &&
                                          (action.item == Item::MagnifyingGlass ||
                                           action.item == Item::BurnerPhone);
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
  out << ". Values are the probability of being the last player standing in this "
         "round, looking through "
      << options.reloadBudget << " reload" << (options.reloadBudget == 1 ? "" : "s") << ".";
  return out.str();
}

SolveResult solve(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  SolveResult result;
  result.assumptions = describeAssumptions(config, options);

  GameState start = state;
  while (rules::applyPendingSkip(&start)) {
    if (start.roundOver()) break;
  }

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

  for (const Action& action : search.legalFor(start)) {
    ActionValue entry;
    entry.action = action;
    entry.value = search.actionValue(start, action, options.reloadBudget);
    result.ranked.push_back(entry);
  }
  // Values are always the solved seat's chance of surviving, but the ordering
  // belongs to whoever is holding the gun: best first for that seat, which for
  // an opponent means the move that leaves us worst off.
  const bool mine = static_cast<int>(start.current) == options.seat;
  std::stable_sort(result.ranked.begin(), result.ranked.end(),
                   [mine](const ActionValue& a, const ActionValue& b) {
                     return mine ? a.value > b.value : a.value < b.value;
                   });
  result.value =
      result.ranked.empty() ? boundaryValue(start, options.seat) : result.ranked.front().value;
  result.nodes = search.nodes();
  result.truncated = search.truncated();
  return result;
}

double solveValue(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  return solve(state, config, options).value;
}

}  // namespace bsr
