#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/Config.h"
#include "engine/State.h"

namespace bsr {

struct Position;

/// The single-player dealer as the game scripts it.
///
/// The rules are read from a third-party decompilation of Buckshot Roulette
/// v2.2.0 hotfix 6, pinned at commit 34531a4c5e26ec44320c5197e2f678ce1a7b8d00
/// of https://github.com/thecatontheceiling/buckshotroulette, and every rule
/// here cites a file there by line, as in
/// https://github.com/thecatontheceiling/buckshotroulette/blob/34531a4c5e26ec44320c5197e2f678ce1a7b8d00/DealerIntelligence.gd#L85
/// for the start of the dealer's decision (`DealerChoice`). The dealer is
/// always seat index 1 (p2) and the player is seat index 0 (p1), because the
/// script only exists for a two-seat table.
///
/// A dealer turn is a sequence of passes. Each pass either uses one item, after
/// which another pass follows in the same turn with the turn memory kept, or
/// fires one shot, after which the turn is over and the memory is discarded.
/// `step` is one pass. Like everything in the engine it reads no input, writes
/// no output and holds no random number generator: every coin the script flips
/// and every shell it cannot see is a branch with a probability.
///
/// The scan walks the dealer's items in the order they sit on the table, then
/// the player's when the dealer holds Adrenaline, and whether it holds
/// cigarettes is read from the list its previous pass built, which
/// `GameState::dealerListCigs` carries between passes (DealerIntelligence.gd
/// 113-149). Two things differ from the script on purpose, and docs/RULES.md
/// lists both with the solver's approximations. The list is taken to be empty
/// at the start of every round, which only `play` meets, since a solved
/// position carries its list in `listcigs`. And a failed Expired Medicine
/// always costs a charge, where the script leaves a dealer below the heal
/// floor where it was; its guard against taking medicine on one charge means
/// this can only arise with a floor above two, and the solver says so with
/// every answer it can change.
namespace dealer {

/// Which of the script's two sets of decision rules is in force. Double or
/// Nothing runs the script's endless rules, which add deduction from counts
/// and weight the coin by the tube; story mode runs the plain ones.
enum class Brain : std::uint8_t { Story, Endless };

/// The brain a rule set calls for. Returns false for multiplayer, which has no
/// scripted dealer.
bool brainFor(const RuleConfig& config, Brain* brain);

/// Printable name: "story" or "endless".
const char* brainName(Brain brain);

/// Who the dealer has decided to shoot.
enum class Target : std::uint8_t { None, Self, Player };

/// What the dealer carries from one pass to the next within a turn. The script
/// keeps these as dealerKnowsShell, knownShell, dealerTarget and usingMedicine
/// (DealerIntelligence.gd lines 65-77). It clears the first three after every
/// shot (lines 277-279) and usingMedicine at every turn start (line 68). A
/// default-constructed memory is the memory a turn starts with.
struct Memory {
  bool knows = false;
  Shell known = Shell::Unknown;  ///< Unknown when it knows nothing
  Target target = Target::None;
  bool usedMedicine = false;

  bool operator==(const Memory& other) const;
};

/// Why a pass did what it did, for narration.
enum class Reason : std::uint8_t {
  Item,         ///< the item scan found an item whose condition held
  SawCoin,      ///< nothing else to use, and the coin said saw the barrel
  SawCoinSelf,  ///< a saw was available, and the coin said shoot itself instead
  Deduced,      ///< it worked out the chamber from what it has seen and the counts
  LastShell,    ///< one shell left, so it knows what it is
  KnownTarget,  ///< a target chosen earlier in the turn
  Coin,         ///< no target, so a coin chose one
};

/// One outcome of a pass.
struct Branch {
  double probability = 1.0;
  GameState state;
  /// The memory the next pass starts from. After a shot the turn is over and
  /// this is a fresh memory, because the next dealer turn starts from one.
  Memory memory;
  /// True when the pass fired a shot, or when the round ended during it. The
  /// position then continues through the ordinary rules: the round may be
  /// over, the tube may need a reload, and the next turn may be the dealer's
  /// again, which starts with a fresh memory.
  bool turnOver = false;
  /// What the dealer did: an item, a stolen item, or a shot at a seat.
  Action action;
  bool stolen = false;  ///< the item came from p1 through Adrenaline
  Reason reason = Reason::Coin;
  /// Set when a shell left the tube in this branch, by a shot or a Beer.
  bool shellFired = false;
  Shell shellType = Shell::Unknown;
};

/// One pass of the dealer's turn from `state`, which must have p2 to move, a
/// shell in the tube and the round still going. Returns every branch with its
/// probability, summing to one, and nothing when the position is not a dealer
/// turn. The pass reads the type of a shell only where the dealer has seen it
/// or where the pass itself resolves it, so the same call works on a live game
/// whose state pins down shells the dealer has not seen.
std::vector<Branch> step(const GameState& state, const Memory& memory, const RuleConfig& config,
                         Brain brain);

/// Whether the list a pass leaves behind holds Cigarettes that belong to the
/// player, given the state the pass started from and the state it left. The
/// list holds the player's items when the dealer held Adrenaline as the pass
/// began (DealerIntelligence.gd 127-129, 146-149), and only its count of
/// Cigarettes is ever read (lines 113-116). `step` writes this into every
/// branch as `GameState::dealerListCigs`.
bool listCigsAfterPass(const GameState& before, const GameState& after);

/// Whether the endless rules let the dealer work out the chamber at the start of
/// a pass (`FigureOutShell`, DealerIntelligence.gd 282-303): it has seen the
/// chamber, the tube holds only one type, or the shells it has seen account for
/// every live or every blank one. Only shells pinned with the dealer's bit count
/// as seen.
bool deduces(const GameState& state);

/// The checks on a dealer memory that hold whatever the rules: a memory other
/// than the fresh one needs a two-seat table, shells in the tube, the dealer to
/// move and not cuffed; a chamber the memory says was seen must be one the
/// dealer saw; aiming at the player without having seen the chamber happens
/// only after the coin saws the barrel; and a memory that aims at the dealer
/// itself cannot go with a sawed barrel, because the dealer saws only when it
/// aims at the player (DealerIntelligence.gd 181-186, 203-215). Returns false
/// and fills `error` otherwise.
bool checkMemory(const Position& position, std::string* error);

/// `checkMemory`, then the checks that depend on the brain: believing a blank
/// the dealer has not seen is left only by the story rules' Beer, and aiming
/// at itself without knowing the chamber only by the endless rules' Beer
/// (DealerIntelligence.gd 170-176).
bool validateMemory(const Position& position, Brain brain, std::string* error);

}  // namespace dealer
}  // namespace bsr
