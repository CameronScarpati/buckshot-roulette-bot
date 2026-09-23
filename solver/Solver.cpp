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

/// Positions that somebody other than `seat` has pinned down and `seat` has
/// not. These are the shells an opponent has looked at. The advised seat cannot
/// read them, and forgetting that they were read is a different mistake from
/// reading them: an opponent that has looked plays better than one that has
/// not, whichever way the shell fell.
std::vector<int> foreignKnown(const GameState& state, int seat) {
  std::vector<int> positions;
  const int shells = std::min<int>(state.tube.size(), kMaxShells);
  for (int i = 0; i < shells; ++i) {
    if (state.tube.truth[i] == Shell::Unknown) continue;
    if (state.tube.knows(seat, i)) continue;
    // A position nobody can name is a modelling artefact, not knowledge, and
    // blinding already puts it back in the pool.
    const std::uint8_t others = static_cast<std::uint8_t>(state.tube.knownBy[i] & ~(1u << seat));
    if (others == 0) continue;
    positions.push_back(i);
  }
  return positions;
}

struct KnowledgeBranch {
  double probability = 1.0;
  GameState state;
};

/// The position as `seat` sees it, split into the ways the shells an opponent
/// has looked at could have fallen.
///
/// Every such position goes back into the unresolved pool as far as this seat
/// is concerned, so the branch weights are draws without replacement from that
/// pool; within a branch the position is pinned again and still carries the
/// observers who saw it, so the seats that looked go on playing as though they
/// know, because they do. One branch with probability one comes back when
/// nobody else has looked at anything, which is the ordinary case.
std::vector<KnowledgeBranch> knowledgeBranches(const GameState& state, int seat, int limit,
                                               int* shells, bool* dropped) {
  const GameState blind = blindedTo(state, seat);
  std::vector<int> positions = foreignKnown(state, seat);
  *shells = static_cast<int>(positions.size());
  *dropped = false;
  if (static_cast<int>(positions.size()) > std::max(0, limit)) {
    *dropped = true;
    positions.clear();
  }
  std::vector<KnowledgeBranch> branches;
  if (positions.empty()) {
    branches.push_back(KnowledgeBranch{1.0, blind});
    return branches;
  }

  const int n = static_cast<int>(positions.size());
  const int poolLive = blind.tube.unresolvedLive();
  const int poolBlank = blind.tube.unresolvedBlank();
  for (unsigned int assignment = 0; assignment < (1u << n); ++assignment) {
    double probability = 1.0;
    int live = poolLive;
    int blanks = poolBlank;
    for (int j = 0; j < n; ++j) {
      const int left = live + blanks;
      if (left <= 0) {
        probability = 0.0;
        break;
      }
      if (((assignment >> j) & 1u) != 0u) {
        probability *= static_cast<double>(live) / static_cast<double>(left);
        --live;
      } else {
        probability *= static_cast<double>(blanks) / static_cast<double>(left);
        --blanks;
      }
    }
    if (probability <= 0.0) continue;

    KnowledgeBranch branch;
    branch.probability = probability;
    branch.state = blind;
    bool chamberDrawn = false;
    Shell chamberShell = Shell::Unknown;
    std::uint8_t chamberObservers = 0;
    for (std::size_t j = 0; j < positions.size(); ++j) {
      const int offset = positions[j];
      const Shell drawn = ((assignment >> j) & 1u) != 0u ? Shell::Live : Shell::Blank;
      const std::uint8_t observers =
          static_cast<std::uint8_t>(state.tube.knownBy[offset] & ~(1u << seat));
      if (offset == 0) {
        // The chamber goes last: pinning it down can move the public counts
        // when an inversion is pending, and the other offsets were weighted off
        // the counts as they stand.
        chamberDrawn = true;
        chamberShell = drawn;
        chamberObservers = observers;
        continue;
      }
      branch.state.tube.resolve(offset, drawn, observers);
    }
    if (chamberDrawn) branch.state.tube.resolveChamberDraw(chamberShell, chamberObservers);
    branches.push_back(branch);
  }
  if (branches.empty()) branches.push_back(KnowledgeBranch{1.0, blind});
  return branches;
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
    } else if (hasHiddenShell(state, state.current)) {
      best = blindChoiceValue(state, actions, reloadsLeft, maximising);
    } else {
      for (const Action& action : actions) {
        const double candidate = actionValue(state, action, reloadsLeft);
        best = maximising ? std::max(best, candidate) : std::min(best, candidate);
      }
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
  ///
  /// This runs for whichever seat is to move. The advised seat needs it as much
  /// as an opponent does: once the answer averages over a shell somebody else
  /// looked at, the branches differ in a way the advised seat cannot see, and
  /// a policy that read them would be advice nobody can follow.
  double blindChoiceValue(const GameState& state, const std::vector<Action>& actions,
                          int reloadsLeft, bool maximising) {
    const GameState blind = blindedTo(state, state.current);
    std::vector<Action> chosen;
    double bestSeen = maximising ? -1.0 : 2.0;
    for (const Action& action : actions) {
      const double candidate = actionValue(blind, action, reloadsLeft);
      const bool better = maximising ? candidate > bestSeen + 1e-12 : candidate < bestSeen - 1e-12;
      if (better) {
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
  out << ". The answer is given from what the advised seat has seen: a shell only "
         "somebody else has looked at is unknown to you and known to them, and the value "
         "averages over how it could have fallen. Values are the probability "
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

  // Answer from the information state of the seat being advised. A shell it has
  // not seen goes back into the unresolved pool, so the search cannot tell it
  // what is in the chamber on the strength of somebody else having looked. What
  // it does keep is that somebody looked: each such shell splits the position
  // into the ways it could have fallen, and every branch is solved against an
  // opponent that knows which one it is in.
  std::vector<KnowledgeBranch> branches =
      knowledgeBranches(state, options.seat, options.opponentKnowledgeLimit,
                        &result.opponentKnownShells, &result.opponentKnowledgeDropped);
  std::vector<GameState> starts;
  std::vector<double> weights;
  for (KnowledgeBranch& branch : branches) {
    GameState start = branch.state;
    while (rules::applyPendingSkip(&start)) {
      if (start.roundOver()) break;
    }
    starts.push_back(start);
    weights.push_back(branch.probability);
  }
  // Whether a seat is skipped, whether the round is over and whether the tube
  // needs filling are all settled by charges and counts, which every branch
  // shares, so the first branch answers them for all of them.
  const GameState& first = starts.front();

  result.mover = first.current;
  Search search(config, options);
  if (first.roundOver()) {
    result.value = first.soleSurvivor() == options.seat ? 1.0 : 0.0;
    return result;
  }
  const auto average = [&](const std::vector<double>& values) {
    double total = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) total += weights[i] * values[i];
    return total;
  };
  const auto averageOver = [&](const std::vector<GameState>& over, const Action* action) {
    std::vector<double> values;
    values.reserve(over.size());
    for (const GameState& position : over) {
      values.push_back(action == nullptr
                           ? search.value(position, options.reloadBudget)
                           : search.actionValue(position, *action, options.reloadBudget));
    }
    return average(values);
  };

  if (first.needsReload()) {
    result.value = averageOver(starts, nullptr);
    result.nodes = search.nodes();
    result.truncated = search.truncated();
    return result;
  }

  // Values are always the solved seat's chance of surviving. The ordering
  // belongs to whoever is holding the gun, and when that is an opponent it has
  // to rank its moves by what it can see rather than by what we have seen.
  const bool mine = static_cast<int>(first.current) == options.seat;
  const bool hidden = !mine && hasHiddenShell(first, first.current);
  std::vector<GameState> seen;
  if (hidden) {
    for (const GameState& position : starts) seen.push_back(blindedTo(position, position.current));
  }

  struct Ranked {
    Action action;
    double value = 0.0;  ///< the solved seat's chance, in the position as it is
    double key = 0.0;    ///< what the seat holding the gun is choosing on
  };
  std::vector<Ranked> rows;
  for (const Action& action : search.legalFor(first)) {
    Ranked row;
    row.action = action;
    row.value = averageOver(starts, &action);
    row.key = hidden ? averageOver(seen, &action) : row.value;
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
  // worth the average over them, not the worst of them. The advised seat is the
  // exception. Its own move is chosen on the averaged rows, which already
  // account for an opponent that has looked, while the search picks each
  // branch's move without sight of the other branches. Taking the front row
  // keeps the value and the recommendation the same answer.
  if (rows.empty()) {
    result.value = boundaryValue(first, options.seat);
  } else if (mine) {
    result.value = rows.front().value;
  } else {
    result.value = averageOver(starts, nullptr);
  }
  result.nodes = search.nodes();
  result.truncated = search.truncated();
  if (result.opponentKnownShells > 0) {
    std::ostringstream extra;
    extra << " Another seat has looked at " << result.opponentKnownShells << " shell"
          << (result.opponentKnownShells == 1 ? "" : "s") << " that you have not";
    if (result.opponentKnowledgeDropped) {
      extra << ", which is more than this answer averages over, so they are treated as seen "
               "by nobody and the opponent is modelled weaker than it is";
    } else {
      extra << ", and the answer is the average over how those could have fallen, against a "
               "seat that knows which. Only the move being asked about gets that treatment: "
               "deeper in the search this seat picks as though nobody had looked, which "
               "understates the other seat in those lines";
    }
    extra << ".";
    result.assumptions += extra.str();
  }
  return result;
}

double solveValue(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  return solve(state, config, options).value;
}

}  // namespace bsr
