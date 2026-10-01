#include "solver/Solver.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_map>

#include "engine/Dealer.h"

namespace bsr {
namespace {

/// The scripted dealer's seat (p2), the only seat its memory and item list
/// describe.
constexpr int kDealerSeat = 1;

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

/// A position part of the way through a dealer turn. The memory is part of the
/// key because two passes that reach the same position with different memories
/// go on to play differently.
struct DealerKey {
  GameState state;
  dealer::Memory memory;
  int reloadsLeft;
  bool operator==(const DealerKey& other) const {
    return reloadsLeft == other.reloadsLeft && memory == other.memory && state == other.state;
  }
};

struct DealerHash {
  std::size_t operator()(const DealerKey& key) const noexcept {
    std::size_t h = MemoHash{}(MemoKey{key.state, key.reloadsLeft});
    const dealer::Memory& memory = key.memory;
    const std::size_t packed =
        (memory.knows ? 1u : 0u) | static_cast<std::size_t>(memory.known) << 1u |
        static_cast<std::size_t>(memory.target) << 3u | (memory.usedMedicine ? 1u : 0u) << 5u;
    h ^= packed + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
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
/// into the unresolved pool, where it is exchangeable again, and a chamber
/// flipped after somebody else pinned it down is a pending flip of a draw from
/// the counts before it, since the flip showed this seat nothing.
GameState blindedTo(const GameState& state, int seat) {
  GameState blind = state;
  blind.tube.unflipFor(seat);
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
///
/// `reads` adds the shells that phone reads this seat never saw the result of
/// named, one way they could have fallen: such a shell is drawn the same way,
/// with the reading seats among its observers. A shell this seat has seen
/// itself is not drawn again; the reading seats simply see it too.
///
/// `keepChamber` is set when the dealer is in the middle of its turn having
/// seen the chamber. The chamber is then always drawn, whatever the limit,
/// because forgetting it would contradict the memory the turn goes on with,
/// and only the other shells count towards the limit. `counted` gets how many
/// shells counted towards it.
std::vector<KnowledgeBranch> knowledgeBranches(const GameState& state, int seat, int limit,
                                               const ReadExpansion& reads, bool keepChamber,
                                               int* counted, bool* dropped) {
  GameState blind = blindedTo(state, seat);
  const int shells = std::min<int>(state.tube.size(), kMaxShells);
  std::vector<int> positions = foreignKnown(state, seat);
  for (int i = 0; i < shells; ++i) {
    const std::uint8_t readers = reads.extraObservers[static_cast<std::size_t>(i)];
    if (readers == 0) continue;
    if (state.tube.knows(seat, i)) {
      blind.tube.knownBy[i] = static_cast<std::uint8_t>(blind.tube.knownBy[i] | readers);
    } else {
      positions.push_back(i);
    }
  }
  if (keepChamber && shells > 0 && !state.tube.knows(seat, 0)) positions.push_back(0);
  std::sort(positions.begin(), positions.end());
  positions.erase(std::unique(positions.begin(), positions.end()), positions.end());

  const bool chamberKept = keepChamber && !positions.empty() && positions.front() == 0;
  *counted = static_cast<int>(positions.size()) - (chamberKept ? 1 : 0);
  *dropped = false;
  if (*counted > std::max(0, limit)) {
    *dropped = true;
    positions.clear();
    if (chamberKept) positions.push_back(0);
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
          static_cast<std::uint8_t>((state.tube.knownBy[offset] & ~(1u << seat)) |
                                    reads.extraObservers[static_cast<std::size_t>(offset)]);
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

/// Whether two actions differ at most in which copy of an item they spend.
bool sameMoveApartFromCopy(const Action& a, const Action& b) {
  return a.kind == b.kind && a.target == b.target && a.item == b.item && a.stolen == b.stolen &&
         a.stealFrom == b.stealFrom;
}

class Search {
 public:
  Search(const RuleConfig& config, const SolveOptions& options)
      : config_(config), options_(options) {
    // solve() refuses a rule set without a scripted dealer before a search is
    // built, so the default here is never the one that plays.
    if (!dealer::brainFor(config, &brain_)) brain_ = dealer::Brain::Endless;
    // The spread a reload deals the dealer's seat (rules::reloadOutcomes): the
    // pool in order from its seat index.
    const std::vector<Item>& pool = config.itemPool;
    for (int d = 0; d < config.itemsDealtPerLoad() && !pool.empty(); ++d) {
      if (pool[static_cast<std::size_t>(d + kDealerSeat) % pool.size()] == Item::Adrenaline) {
        reloadDealsDealerAdrenaline_ = true;
      }
    }
  }

  /// Put a position into the form the memo stores. Item order and the
  /// dealer's item list are read only by the scripted dealer, so under any
  /// other model every hand is sorted and the list forgotten, which merges
  /// positions that play the same. Under the dealer model the dealer's own
  /// order always matters, and p1's matters only while the dealer can hold
  /// Adrenaline, so p1's hand is sorted when it holds none and no reload the
  /// search still looks through deals it one.
  void canonicalise(GameState* state, int reloadsLeft) const {
    if (options_.opponent != OpponentModel::Dealer) {
      for (int i = 0; i < state->playerCount; ++i) state->players[i].hand.sortCanonical();
      state->dealerListCigs = false;
      return;
    }
    if (!options_.mergePlayerHandOrder) return;
    if (state->players[kDealerSeat].hand.holds(Item::Adrenaline)) return;
    if (reloadsLeft > 0 && reloadDealsDealerAdrenaline_) return;
    state->players[options_.seat].hand.sortCanonical();
  }

  double value(const GameState& start, int reloadsLeft) {
    GameState state = start;
    if (state.roundOver()) {
      return state.soleSurvivor() == options_.seat ? 1.0 : 0.0;
    }
    const bool reload = state.needsReload();
    if (reload && reloadsLeft <= 0) {
      budgetReached_ = true;
      return boundaryValue(state, options_.seat);
    }
    if (!reload) {
      // A seat that arrives cuffed is skipped before it can be asked for a move.
      while (rules::applyPendingSkip(&state)) {
        if (state.roundOver()) {
          return state.soleSurvivor() == options_.seat ? 1.0 : 0.0;
        }
      }
    }
    canonicalise(&state, reloadsLeft);

    const MemoKey key{state, reloadsLeft};
    auto found = memo_.find(key);
    if (found != memo_.end()) return found->second;
    // Only a position met for the first time counts, so stopping here never
    // changes the value of one the search has already solved.
    if (++nodes_ > options_.nodeLimit) {
      nodeLimitHit_ = true;
      return boundaryValue(state, options_.seat);
    }

    double worth = 0.0;
    if (reload) {
      for (const Outcome& branch : rules::reloadOutcomes(state, config_, true)) {
        worth += branch.probability * value(branch.state, reloadsLeft - 1);
      }
    } else if (options_.opponent == OpponentModel::Dealer && state.current != options_.seat) {
      // The scripted dealer does not choose between moves. Every turn it
      // starts, including the one after a blank into itself, starts with a
      // fresh memory.
      worth = dealerTurn(state, dealer::Memory{}, reloadsLeft);
    } else {
      worth = choiceValue(state, reloadsLeft);
    }
    memo_.emplace(key, worth);
    return worth;
  }

  /// The value of the seat to move choosing its move.
  double choiceValue(const GameState& state, int reloadsLeft) {
    const bool maximising = state.current == options_.seat;
    std::vector<Action> actions = legalFor(state);
    if (actions.empty()) {
      noMoveBoundary_ = true;
      return boundaryValue(state, options_.seat);
    }
    if (hasHiddenShell(state, state.current)) {
      return blindChoiceValue(state, actions, reloadsLeft, maximising);
    }
    double best = maximising ? -1.0 : 2.0;
    for (const Action& action : actions) {
      const double candidate = actionValue(state, action, reloadsLeft);
      best = maximising ? std::max(best, candidate) : std::min(best, candidate);
    }
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
  /// Moves that differ only in which copy of an item they spend count as one
  /// option, worth the best of its copies, so that holding two copies apart in
  /// the hand does not give a move two shares of the choice. A chosen option is
  /// played with that best copy, the first in move order among equals.
  ///
  /// This runs for whichever seat is to move. The advised seat needs it as much
  /// as an opponent does: once the answer averages over a shell somebody else
  /// looked at, the branches differ in a way the advised seat cannot see, and
  /// a policy that read them would be advice nobody can follow.
  double blindChoiceValue(const GameState& state, const std::vector<Action>& actions,
                          int reloadsLeft, bool maximising) {
    const GameState blind = blindedTo(state, state.current);
    const auto better = [maximising](double candidate, double than) {
      return maximising ? candidate > than + 1e-12 : candidate < than - 1e-12;
    };
    struct Option {
      Action action;
      double seen = 0.0;
    };
    std::vector<Option> options;
    for (const Action& action : actions) {
      const double candidate = actionValue(blind, action, reloadsLeft);
      auto same = std::find_if(options.begin(), options.end(), [&action](const Option& option) {
        return sameMoveApartFromCopy(option.action, action);
      });
      if (same == options.end()) {
        options.push_back(Option{action, candidate});
      } else if (better(candidate, same->seen)) {
        *same = Option{action, candidate};
      }
    }
    std::vector<Action> chosen;
    double bestSeen = maximising ? -1.0 : 2.0;
    for (const Option& option : options) {
      if (better(option.seen, bestSeen)) {
        bestSeen = option.seen;
        chosen.clear();
        chosen.push_back(option.action);
      } else if (std::abs(option.seen - bestSeen) <= 1e-12) {
        chosen.push_back(option.action);
      }
    }
    if (chosen.empty()) {
      noMoveBoundary_ = true;
      return boundaryValue(state, options_.seat);
    }
    double total = 0.0;
    for (const Action& action : chosen) total += actionValue(state, action, reloadsLeft);
    return total / static_cast<double>(chosen.size());
  }

  /// The value of the rest of a dealer turn: one pass, then either the next
  /// pass with the memory it left, or, once a shot has ended the turn, the
  /// position as the ordinary search sees it.
  double dealerTurn(const GameState& start, const dealer::Memory& memory, int reloadsLeft) {
    GameState state = start;
    canonicalise(&state, reloadsLeft);
    const DealerKey key{state, memory, reloadsLeft};
    auto found = dealerMemo_.find(key);
    if (found != dealerMemo_.end()) return found->second;
    if (++nodes_ > options_.nodeLimit) {
      nodeLimitHit_ = true;
      return boundaryValue(state, options_.seat);
    }
    const std::vector<dealer::Branch> branches = dealer::step(state, memory, config_, brain_);
    double total = 0.0;
    if (branches.empty()) {
      // Not reachable from a position the search generates; stated rather
      // than hidden if it ever is.
      noMoveBoundary_ = true;
      total = boundaryValue(state, options_.seat);
    }
    for (const dealer::Branch& branch : branches) {
      total += branch.probability * (branch.turnOver
                                         ? value(branch.state, reloadsLeft)
                                         : dealerTurn(branch.state, branch.memory, reloadsLeft));
    }
    dealerMemo_.emplace(key, total);
    return total;
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
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [&readsShells](const Action& action) {
                                   if (action.kind != Action::Kind::UseItem) return false;
                                   // Adrenaline can reach for one of these too.
                                   return readsShells(action.item) ||
                                          (action.isSteal() && readsShells(action.stolen));
                                 }),
                  actions.end());
    if (actions.empty()) actions.push_back(Action::shoot(state.current));
    return actions;
  }

  long long nodes() const { return nodes_; }
  bool budgetReached() const { return budgetReached_; }
  bool nodeLimitHit() const { return nodeLimitHit_; }
  bool truncated() const { return budgetReached_ || nodeLimitHit_ || noMoveBoundary_; }

 private:
  const RuleConfig& config_;
  const SolveOptions& options_;
  dealer::Brain brain_ = dealer::Brain::Endless;
  bool reloadDealsDealerAdrenaline_ = false;
  std::unordered_map<MemoKey, double, MemoHash> memo_;
  std::unordered_map<DealerKey, double, DealerHash> dealerMemo_;
  long long nodes_ = 0;
  bool budgetReached_ = false;
  bool nodeLimitHit_ = false;
  bool noMoveBoundary_ = false;
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

bool dealerSupported(const GameState& state, const RuleConfig& config, const SolveOptions& options,
                     std::string* reason) {
  dealer::Brain brain = dealer::Brain::Endless;
  if (state.playerCount != 2) {
    *reason = "the scripted dealer plays a table of two seats, and this position has " +
              std::to_string(static_cast<int>(state.playerCount));
    return false;
  }
  if (!dealer::brainFor(config, &brain)) {
    *reason = "the scripted dealer plays story mode and double or nothing, not multiplayer";
    return false;
  }
  if (options.seat != 0) {
    *reason = "the scripted dealer is p2, so the advised seat must be p1";
    return false;
  }
  return true;
}

std::string describeAssumptions(const RuleConfig& config, const SolveOptions& options) {
  std::ostringstream out;
  out << "Model: " << config.describe() << ". ";
  const bool scripted = options.opponent == OpponentModel::Dealer;
  if (scripted) {
    dealer::Brain brain = dealer::Brain::Endless;
    dealer::brainFor(config, &brain);
    out << "The other seat is the dealer, and it plays the game's own dealer script with its "
        << dealer::brainName(brain)
        << " rules: it uses items, aims and flips coins as the decompiled script does, rather "
           "than playing to minimise your chance of surviving the round. ";
  } else {
    out << (options.opponent == OpponentModel::Paranoid ? "Every other seat" : "The other seat")
        << " plays to minimise your chance of surviving the round";
    if (!options.opponentUsesInfoItems) {
      out << ", spending no magnifying glasses or burner phones, so it is modelled "
             "slightly weaker than a player who tracks shells";
    }
    out << ". ";
  }
  out << "The answer is given from what the advised seat has seen: a shell only somebody else "
         "has looked at is unknown to you and known to them, and the value averages over how it "
         "could have fallen. Approximations: the search looks through "
      << options.reloadBudget << " reload" << (options.reloadBudget == 1 ? "" : "s")
      << " and scores a round still going past that by each seat's share of the charges left; "
         "a reload deals each seat one fixed set of items, taken from the pool in order, "
         "rather than a random draw; every reload deals the same number of items, the one "
         "named above; a choice does not infer a shell's type from what another seat chose to "
         "do; at most "
      << options.opponentKnowledgeLimit
      << " shells that only another seat has looked at are averaged over, and past that they "
         "are treated as seen by nobody; and a seat that has not seen a shell picks evenly "
         "between moves it cannot tell apart, ranking them as though no other seat had seen "
         "that shell either. ";
  // The script's own guard keeps the dealer off medicine at one charge, so a
  // failed dose can only cross the heal floor when the floor is above two.
  if (scripted && config.healFloor > 2) {
    out << "For the dealer, a failed Expired Medicine always costs it a charge, even below the "
           "heal floor, where the script leaves its charges as they were. ";
  }
  out << "Where the game's scene data has not been extracted, such as the story loads and the "
         "item pool, the rules follow the assumptions listed in docs/RULES.md. Values are the "
         "probability of being the last player standing in this round.";
  return out.str();
}

namespace {

/// Said with every answer the search could not finish within the node limit.
constexpr const char* kStoppedEarly = " The search stopped early; these chances may be off.";

/// Said when the scripted dealer is to move at the root.
constexpr const char* kStartOfTurn =
    " A position with the dealer to move and no dealer= memory is taken as the start of its "
    "turn, so anything it decided earlier in that turn (the target a coin chose before it "
    "sawed the barrel, medicine already taken, what it worked out about the chamber) is not "
    "carried over.";

/// Why a position cannot be answered under this model, or empty when it can.
std::string refusalFor(const Position& position, const RuleConfig& config,
                       const SolveOptions& options) {
  if (!(position.dealerMemory == dealer::Memory{}) && options.opponent != OpponentModel::Dealer) {
    return "dealer describes the scripted dealer; use --opponent dealer";
  }
  if (options.opponent == OpponentModel::Dealer) {
    dealer::Brain brain = dealer::Brain::Endless;
    dealer::brainFor(config, &brain);
    std::string error;
    if (!dealer::validateMemory(position, brain, &error)) return error;
  }
  for (const UnseenRead& read : position.unseenReads) {
    if (read.seat != options.seat) continue;
    const std::string seat = "p" + std::to_string(options.seat + 1);
    std::string message = "phoned names ";
    message += seat;
    message += ", the seat being advised, which saw where its own phone looked; give known=";
    message += seat;
    message += " instead";
    return message;
  }
  return "";
}

/// Whether the dealer's memory says it has seen the chamber. The memory then
/// follows the chamber into every way it could have fallen.
bool dealerSawChamber(const Position& position) {
  return position.dealerMemory.knows && position.state.tube.knows(kDealerSeat, 0);
}

/// The memory the dealer's turn goes on with in one root start.
dealer::Memory memoryFor(const Position& position, const GameState& start) {
  dealer::Memory memory = position.dealerMemory;
  if (dealerSawChamber(position)) {
    memory.known = start.tube.truth[0];
    memory.target = memory.known == Shell::Live ? dealer::Target::Player : dealer::Target::Self;
  }
  return memory;
}

/// Sort every hand and forget the dealer's item list, under the models that
/// read neither.
void canonicalRoot(GameState* state, const SolveOptions& options) {
  if (options.opponent == OpponentModel::Dealer) return;
  for (int i = 0; i < state->playerCount; ++i) state->players[i].hand.sortCanonical();
  state->dealerListCigs = false;
}

/// What the answer did with shells another seat has looked at, appended to the
/// assumptions whenever there were any.
std::string knowledgeNote(const SolveResult& result, const RuleConfig& config,
                          const SolveOptions& options) {
  if (result.opponentKnownShells <= 0) return "";
  const bool scripted = options.opponent == OpponentModel::Dealer;
  dealer::Brain brain = dealer::Brain::Endless;
  dealer::brainFor(config, &brain);
  std::ostringstream extra;
  extra << (scripted ? " The dealer has looked at " : " Another seat has looked at ")
        << result.opponentKnownShells << " shell" << (result.opponentKnownShells == 1 ? "" : "s")
        << " that you have not";
  if (result.opponentKnowledgeDropped) {
    extra << ", which is more than this answer averages over, so they are treated as seen "
             "by nobody and the opponent is modelled weaker than it is";
  } else if (scripted && brain == dealer::Brain::Story) {
    // Only the endless rules deduce the chamber from shells seen before the
    // turn, so the story dealer plays the same whichever way they fell.
    extra << ", but the story rules do not use what it has seen, so this does not change the "
             "answer";
  } else if (scripted) {
    extra << ", and the answer is the average over how those could have fallen, against a "
             "dealer that remembers which";
  } else {
    extra << ", and the answer is the average over how those could have fallen, against a "
             "seat that knows which. Only the move being asked about gets that treatment: "
             "deeper in the search this seat picks as though nobody had looked, which "
             "understates the other seat in those lines";
  }
  extra << ".";
  return extra.str();
}

}  // namespace

SolveResult solve(const Position& position, const RuleConfig& config, const SolveOptions& options) {
  const GameState& state = position.state;
  SolveResult result;
  result.assumptions = describeAssumptions(config, options);
  if (options.seat < 0 || options.seat >= state.playerCount) {
    // Nothing sensible can be said about a seat that is not at the table, and
    // the value would otherwise be read from past the end of the seats.
    result.assumptions = "No such seat in this position.";
    return result;
  }
  if (options.opponent == OpponentModel::Dealer) {
    std::string reason;
    if (!dealerSupported(state, config, options, &reason)) {
      result.refused = true;
      result.assumptions = "Not solved: " + reason + ".";
      return result;
    }
  }
  const std::string refusal = refusalFor(position, config, options);
  if (!refusal.empty()) {
    result.refused = true;
    result.assumptions = "Not solved: " + refusal;
    return result;
  }

  // Answer from the information state of the seat being advised. A shell it has
  // not seen goes back into the unresolved pool, so the search cannot tell it
  // what is in the chamber on the strength of somebody else having looked. What
  // it does keep is that somebody looked: each such shell splits the position
  // into the ways it could have fallen, and every branch is solved against an
  // opponent that knows which one it is in. Phone reads it never saw the result
  // of split it first, into the shells each read could have named.
  const bool keepChamber = dealerSawChamber(position);
  // A dealer that has seen the chamber saws the barrel only when it is live
  // (DealerIntelligence.gd 181 and 203-215), so a sawed barrel in front of it
  // tells every seat what the chamber holds.
  GameState rooted = state;
  if (keepChamber && rooted.tube.sawed) {
    rooted.tube.knownBy[0] = static_cast<std::uint8_t>((1u << rooted.playerCount) - 1u);
  }
  std::vector<GameState> starts;
  std::vector<double> weights;
  std::vector<dealer::Memory> memories;
  for (const ReadExpansion& reads : expandReads(position, options.seat)) {
    int counted = 0;
    bool dropped = false;
    const std::vector<KnowledgeBranch> branches =
        knowledgeBranches(rooted, options.seat, options.opponentKnowledgeLimit, reads, keepChamber,
                          &counted, &dropped);
    if (reads.weight > 0.0) {
      result.opponentKnownShells = std::max(result.opponentKnownShells, counted);
      if (dropped) result.opponentKnowledgeDropped = true;
    }
    for (const KnowledgeBranch& branch : branches) {
      GameState start = branch.state;
      const dealer::Memory memory = memoryFor(position, start);
      while (rules::applyPendingSkip(&start)) {
        if (start.roundOver()) break;
      }
      canonicalRoot(&start, options);
      starts.push_back(start);
      weights.push_back(reads.weight * branch.probability);
      memories.push_back(memory);
    }
  }
  // Whether a seat is skipped, whether the round is over and whether the tube
  // needs filling are all settled by charges and counts, which every branch
  // shares, so the first branch answers them for all of them.
  const GameState& first = starts.front();

  result.mover = first.current;
  Search search(config, options);
  const auto finish = [&]() {
    result.nodes = search.nodes();
    result.budgetReached = search.budgetReached();
    result.nodeLimitHit = search.nodeLimitHit();
    result.truncated = search.truncated();
    result.assumptions += knowledgeNote(result, config, options);
    if (result.nodeLimitHit) result.assumptions += kStoppedEarly;
  };
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
    for (const GameState& start : over) {
      values.push_back(action == nullptr
                           ? search.value(start, options.reloadBudget)
                           : search.actionValue(start, *action, options.reloadBudget));
    }
    return average(values);
  };

  if (first.needsReload()) {
    result.value = averageOver(starts, nullptr);
    finish();
    return result;
  }

  // The scripted dealer to move has no moves to rank. Its turn goes on from
  // the memory the position gives, which is a fresh one unless it says
  // otherwise, and what it has already seen is in each branch.
  if (options.opponent == OpponentModel::Dealer &&
      static_cast<int>(first.current) != options.seat) {
    std::vector<double> values;
    values.reserve(starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
      values.push_back(search.dealerTurn(starts[i], memories[i], options.reloadBudget));
    }
    result.value = average(values);
    result.assumptions += kStartOfTurn;
    finish();
    return result;
  }

  // Values are always the solved seat's chance of surviving. The ordering
  // belongs to whoever is holding the gun, and when that is an opponent it has
  // to rank its moves by what it can see rather than by what we have seen.
  const bool mine = static_cast<int>(first.current) == options.seat;
  const bool hidden = !mine && hasHiddenShell(first, first.current);
  std::vector<GameState> seen;
  if (hidden) {
    for (const GameState& start : starts) seen.push_back(blindedTo(start, start.current));
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
  finish();
  return result;
}

SolveResult solve(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  Position position;
  position.state = state;
  return solve(position, config, options);
}

double solveValue(const GameState& state, const RuleConfig& config, const SolveOptions& options) {
  return solve(state, config, options).value;
}

}  // namespace bsr
