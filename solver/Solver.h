#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/Config.h"
#include "engine/Position.h"
#include "engine/Rules.h"
#include "engine/State.h"

namespace bsr {

struct SolveOptions {
  /// The seat whose chance of surviving the round is being maximised.
  int seat = 0;

  /// With `OpponentModel::Dealer`, seat 2 plays the game's dealer script
  /// (engine/Dealer.h) instead of minimising, which needs a table that
  /// `dealerSupported` accepts.
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

  /// How many shells that only another seat has looked at the answer averages
  /// over. Each one splits the position in two, and no seat can realistically
  /// look at more than a few, so the guard rail is low. Past it the extra
  /// shells are treated as seen by nobody, which is what this solver did with
  /// all of them before. Under `OpponentModel::Dealer` it does not apply: the
  /// dealer works out the chamber from every shell it has seen
  /// (DealerIntelligence.gd 282-303), so all of them are averaged over.
  int opponentKnowledgeLimit = 4;

  /// Guard rail. The search counts the positions it has not met before, and
  /// once that count passes this limit it stops recursing and reports
  /// `nodeLimitHit` rather than running forever. Positions it has already
  /// solved keep their values. Each position met for the first time after
  /// that is scored by charges in hand and counted once, so the count can end
  /// above the limit.
  long long nodeLimit = 40000000;

  /// Under `OpponentModel::Dealer`, merge positions that differ only in the
  /// order of p1's hand when nothing can read that order: the dealer reads it
  /// only while it holds Adrenaline (DealerIntelligence.gd 127-129, 146-149,
  /// 243-257), and here it holds none and no deal the search looks through
  /// gives it one. This changes how much the search does and never a value.
  /// No command line flag sets it; it exists so that tests can compare the
  /// two.
  bool mergePlayerHandOrder = true;
};

struct ActionValue {
  Action action;
  double value = 0.0;  ///< probability that the solved seat survives the round
  /// The move spends an item and changes nothing else: Cigarettes at full
  /// charges, a Magnifying Glass on a chamber the seat has seen, a Burner Phone
  /// with one shell left, an Adrenaline used on its own. The game allows these
  /// (docs/RULES.md, "What a seat may do"). Such a move ranks after every move
  /// it ties with and is not counted among the best of them, since past the
  /// reloads the search looks through the item may still be worth keeping.
  bool spendsOnly = false;
};

/// Why spending an item for nothing can rank above every move that does
/// something. The search finds that it does; this names the rule behind it.
enum class SpendReason : std::uint8_t {
  None,        ///< neither rule below applies
  Adrenaline,  ///< another seat holds Adrenaline and could take the item
  Room,        ///< the hand is full, and a reload the search looks through deals into it
};

/// The reason for `action`, a move that only spends an item, played by
/// `mover` in `state`, looking through `reloadBudget` reloads.
SpendReason spendReason(const GameState& state, int mover, const Action& action,
                        const RuleConfig& config, int reloadBudget);

struct SolveResult {
  double value = 0.0;
  /// The seat the ranking belongs to. It is not always `options.seat`: a seat
  /// that arrives handcuffed is skipped before anything is asked of it, so the
  /// moves listed can be the opponent's.
  int mover = 0;
  /// Best first for `mover`. Empty when the scripted dealer is to move, since
  /// the dealer does not choose between moves: `value` is then the worth of the
  /// dealer's turn to the advised seat.
  std::vector<ActionValue> ranked;
  /// Set when the question could not be answered under the model asked for,
  /// such as the scripted dealer at a table it does not play. Nothing is
  /// searched, and `assumptions` says why.
  bool refused = false;
  /// Positions the search met for the first time, counting a dealer pass
  /// separately from the position it starts from. Past the node limit this
  /// takes in the positions scored by charges in hand without a search.
  long long nodes = 0;
  /// Some line ran past the reload budget and was scored by charges in hand.
  bool budgetReached = false;
  /// The search passed `SolveOptions::nodeLimit` and stopped early, so the
  /// values may be wrong.
  bool nodeLimitHit = false;
  /// Set when any value was not searched to the end of the round: the reload
  /// budget, the node limit, or a position with no move to make.
  bool truncated = false;
  /// Shells another seat has looked at and the advised seat has not. The value
  /// is the average over the ways those shells could have fallen, weighted by
  /// what the advised seat can work out about them.
  int opponentKnownShells = 0;
  /// Set when there were more such shells than `opponentKnowledgeLimit`, so
  /// they were treated as seen by nobody instead of averaged over.
  bool opponentKnowledgeDropped = false;
  std::string assumptions;

  bool hasTie() const;
  /// The actions that tie at the front of the ranking, which is the best play
  /// for whichever seat is holding the gun. A move that only spends an item
  /// is left out when a move that does something ties with it.
  std::vector<Action> bestActions(double tolerance = 1e-9) const;
};

/// Solve a position exactly under the stated model. The value is the
/// probability that `options.seat` is the last player standing in this round.
///
/// Phone reads whose result the advised seat never saw are spread over the
/// shells they could have named, and a dealer memory is taken as the memory
/// the dealer's current turn has reached. A position the model cannot answer
/// comes back `refused`, with the reason in `assumptions` after "Not solved: ":
/// a dealer memory under another opponent model, a memory the dealer's rules
/// in this mode cannot leave, or phone reads by the advised seat itself.
SolveResult solve(const Position& position, const RuleConfig& config, const SolveOptions& options);

/// `solve` for a state with no unseen phone reads, at the start of a turn.
SolveResult solve(const GameState& state, const RuleConfig& config, const SolveOptions& options);

/// The value of a position without ranking the moves, for tests and the oracle
/// comparison.
double solveValue(const GameState& state, const RuleConfig& config, const SolveOptions& options);

/// Whether the scripted dealer can be the opponent here: two seats, the
/// player's seat (p1) advised, and story mode or Double or Nothing. When it
/// cannot, `reason` gets one line saying why. This does not look at
/// `options.opponent`, so a caller can ask before switching the model on.
bool dealerSupported(const GameState& state, const RuleConfig& config, const SolveOptions& options,
                     std::string* reason);

/// One line naming every assumption that could change the answer. Its
/// approximations are entries 1 to 7 of the list in docs/RULES.md, in the same
/// order; the answer adds entries 8 and 9 when they apply.
std::string describeAssumptions(const RuleConfig& config, const SolveOptions& options);

}  // namespace bsr
