#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/Items.h"

namespace bsr {

/// Which set of rules a game runs under. Every mode is a configuration of the
/// same engine; nothing about the state type changes between them.
enum class Mode : std::uint8_t { Story, DoubleOrNothing, Multiplayer };

/// Who holds the turn after a mid-round reload. In the single-player modes the
/// player is handed the shotgun after every load (ShellLoader.gd 92-98), which
/// is the default here; multiplayer has no script to read, and the optimal move
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
/// The first two minimise within the limits the solver states: the other seats
/// choose from their own information state and spend no information items
/// unless the search is told to let them. The third is the single-player
/// dealer as the game scripts it, rule by rule, with the decompiled script's
/// lines cited in engine/Dealer.h and engine/Dealer.cpp. It plays its script
/// rather than minimising, so it is a model of the dealer and not of the
/// strongest possible opponent, and it exists only for a two-seat table in
/// story mode or Double or Nothing with the player's seat advised.
enum class OpponentModel : std::uint8_t {
  Optimal,   ///< the opponent minimises our win probability (two players)
  Paranoid,  ///< three or more players: everyone else plays to minimise us
  Dealer,    ///< seat 2 is the scripted single-player dealer (engine/Dealer.h)
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
  /// and the table limit. Double or Nothing draws a count of 2 to 5 at every
  /// load (RoundManager.gd 154) and both seats take that many, stopping at the
  /// limit of 8 (ItemManager.gd 331, 346-347); equal ends mean a fixed deal.
  /// See the items dealt per load row in docs/RULES.md.
  std::uint8_t itemsPerLoad = 2;
  std::uint8_t itemsPerLoadMax = 5;
  std::uint8_t itemLimit = 8;

  /// The count a reload deal actually hands out: the middle of the range,
  /// rounded up, so Double or Nothing's 2 to 5 is modelled at 4. The game draws
  /// the count at every load; the solver deals this one count every time, one
  /// of the approximations listed in docs/RULES.md. The range is what the flag
  /// sets and what `describe()` reports, next to the count it is modelled at.
  std::uint8_t itemsDealtPerLoad() const;

  /// Shell counts a reload may produce, as (live, blank) pairs drawn evenly.
  /// Empty means "use the default generator", which draws a total of 2 to 8
  /// with at least one of each type and no forced even split. Story and
  /// multiplayer use it, an assumption until the story loads are extracted.
  std::vector<std::pair<std::uint8_t, std::uint8_t>> loadTable;

  ReloadTurn reloadTurn = ReloadTurn::PlayerFirst;

  /// Whether a sawed barrel survives a reload. In story mode and Double or
  /// Nothing it does: a Beer that empties the tube starts the next load without
  /// touching the saw (ItemInteraction.gd 130-141, RoundManager.gd 196-234), and
  /// only the end of a shot turn resets it (RoundManager.gd 273). Every shot
  /// already spends the saw before an empty tube reloads, so keeping it at the
  /// reload is exact. Multiplayer has no sourced answer and keeps the old
  /// default. See the sawed barrel across a reload row in docs/RULES.md.
  bool sawSurvivesReload = false;

  /// Whether a blank that seat 2 fires into itself at a two-seat table leaves
  /// a sawed barrel sawed while shells remain. The dealer's turn goes on
  /// without the end-of-turn reset that spends the saw (DealerIntelligence.gd
  /// 305-324, 387), where the player's turn ends through it (RoundManager.gd
  /// 264-295). Story mode and Double or Nothing; not multiplayer.
  bool dealerSeatBlankKeepsSaw = true;

  /// Whether handcuffs come off every player at a load, a mid-round reload
  /// included. The single-player scripts release every restraint before a load
  /// is dealt (RoundManager.gd 200).
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
