#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/Items.h"

namespace bsr {

/// Which set of rules a game runs under. Every mode is a configuration of the
/// same engine; nothing about the state type changes between them.
enum class Mode : std::uint8_t { Story, DoubleOrNothing, Multiplayer };

/// Who holds the turn after a mid-round reload. The single-player answer is
/// sourced and is the default here; multiplayer is not, and the optimal move
/// depends on the answer, so it stays a setting with an explicit default
/// rather than a hard-coded rule. See the turn owner after a mid-round reload
/// row in docs/RULES.md.
enum class ReloadTurn : std::uint8_t {
  KeepCurrent,  ///< whoever was to move keeps the turn
  PlayerFirst,  ///< seat 0 acts first after every reload
  DealerFirst,  ///< seat 1 acts first after every reload
};

/// How the solver treats the other seats. Stated in every advisor answer,
/// because "optimal" has no meaning without it.
/// The scripted single-player dealer is deliberately absent: its policy would
/// have to be re-derived rule by rule from the game before it could be called
/// a model of the dealer, and a guess presented as one would be worse than
/// naming the assumption honestly. Both models below are exact.
enum class OpponentModel : std::uint8_t {
  Optimal,   ///< the opponent minimises our win probability (two players)
  Paranoid,  ///< three or more players: everyone else plays to minimise us
};

/// Everything a rule set needs to decide. Defaults describe Double or Nothing
/// with two seats, which is the mode the advisor is most often asked about.
struct RuleConfig {
  Mode mode = Mode::DoubleOrNothing;

  /// Charges each player starts a round with. Story mode uses 2, then 4, then
  /// 5, where the fifth is the faded band described under `healFloor`.
  std::uint8_t charges = 4;

  /// Healing does nothing to a seat holding fewer charges than this. One means
  /// healing works on any living seat, which is every mode but the third story
  /// stage. That stage gives four normal charges and two faded ones: once a
  /// seat loses its last normal charge the machine cuts its life support, any
  /// hit from then on is fatal, and healing items stop working. The faded pair
  /// is therefore worth exactly one more hit that cannot be healed, so the
  /// stage is modelled as five charges with a floor of two, and a seat showing
  /// one charge here is a seat showing no normal charges in the game. See the
  /// faded charges row in docs/RULES.md.
  std::uint8_t healFloor = 1;

  /// Item pool for this mode, used by reload deals.
  std::vector<Item> itemPool;

  /// Items dealt to each player per load, as the range the game draws from,
  /// and the table limit. Double or Nothing redraws the count at every load, so
  /// the two ends differ there; equal ends mean a fixed deal. See the items
  /// dealt per load row in docs/RULES.md.
  std::uint8_t itemsPerLoad = 1;
  std::uint8_t itemsPerLoadMax = 5;
  std::uint8_t itemLimit = 8;

  /// The count a reload deal actually hands out: the middle of the range,
  /// rounded up. The deal is already modelled at its average over which items
  /// come out of the box, so a chance node over how many come out would
  /// multiply the branching without making the answer any truer. The range is
  /// what the flag sets and what `describe()` reports, so nothing is hidden.
  std::uint8_t itemsDealtPerLoad() const;

  /// Shell counts a reload may produce, as (live, blank) pairs with weights.
  /// Empty means "use the default generator", which draws a total of 2 to 8
  /// with at least one of each type and no forced even split.
  std::vector<std::pair<std::uint8_t, std::uint8_t>> loadTable;

  ReloadTurn reloadTurn = ReloadTurn::PlayerFirst;

  /// Whether a sawed barrel survives a reload. The game says it does not, and
  /// that is the default; it stays a setting because the answer changes what
  /// emptying a tube with Beer is worth. See the sawed barrel across a reload
  /// row in docs/RULES.md.
  bool sawSurvivesReload = false;

  /// Whether handcuffs come off every player when items are dealt, including a
  /// mid-round reload. The decompiled single-player scripts say yes.
  bool reloadClearsCuffs = true;

  /// Expired Medicine heals by this much on success and costs one charge on
  /// failure, with this probability of success.
  double medicineSuccess = 0.5;
  std::uint8_t medicineHeal = 2;

  static RuleConfig storyRound(int round);
  static RuleConfig doubleOrNothing(std::uint8_t charges = 4);
  static RuleConfig multiplayer(std::uint8_t players, std::uint8_t charges = 4);

  /// Human-readable one-liner naming every assumption that changes an answer.
  std::string describe() const;
};

}  // namespace bsr
