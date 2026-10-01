#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/Config.h"
#include "engine/Items.h"
#include "engine/Tube.h"

namespace bsr {

/// A seat never holds more items than this: the game stops placing items on a
/// seat's side of the table at eight (ItemManager.gd 258, 279, 347).
constexpr int kMaxItemsPerSeat = 8;

/// The items one seat holds, in the order it received them, oldest first. The
/// order matters because the game's dealer reads it: its item scan walks the
/// items in the order they sit on the table, and its own use and its steals
/// take the first copy of a type (DealerIntelligence.gd 118-149, 243-261).
///
/// Entries at `len` and above are always Magnifying Glass, so that two equal
/// hands compare and hash equal whatever they held before.
struct Hand {
  std::array<Item, kMaxItemsPerSeat> at{};
  std::uint8_t len = 0;

  int size() const { return len; }
  int count(Item item) const;
  bool holds(Item item) const { return indexOfCopy(item, 0) >= 0; }
  /// Add an item at the end. The hand must have room.
  void append(Item item);
  /// Remove the entry at `index` and close the gap.
  void removeAt(int index);
  /// The index of the `ordinal`-th copy of `item`, counted from the front and
  /// from zero, or -1 when there is no such copy.
  int indexOfCopy(Item item, int ordinal) const;
  /// Remove the `ordinal`-th copy of `item`. Returns false when there is none.
  bool removeCopy(Item item, int ordinal);
  /// Sort by item type, keeping the order of equal entries.
  void sortCanonical();
  /// How many separate runs of adjacent copies of `item` the hand holds.
  int runs(Item item) const;
  /// Which copy of its type the entry at `index` is, counted from zero.
  int ordinalAt(int index) const;
  bool operator==(const Hand& other) const;
};

struct PlayerState {
  std::uint8_t hp = 0;
  std::uint8_t maxHp = 0;
  Hand hand{};
  bool cuffed = false;  ///< skips the next turn that would come to this seat
  /// Set when a skip was consumed and cleared when this seat next acts. The
  /// real game gives a cuffed seat one turn before it can be cuffed again, so
  /// this is what blocks the chain the previous engine allowed.
  bool skipConsumed = false;

  bool alive() const { return hp > 0; }
  int itemCount() const;
  bool operator==(const PlayerState& other) const;
};

/// One position, from nobody's point of view in particular: it carries the
/// objective tube and every seat's knowledge. A solver reads it through the
/// information set of the seat it is advising.
struct GameState {
  IndexedArray<PlayerState, kMaxPlayers> players{};
  std::uint8_t playerCount = 2;
  std::uint8_t current = 0;   ///< seat to move
  std::int8_t direction = 1;  ///< +1 clockwise, -1 after a Remote
  Tube tube;
  bool cuffUsedThisTurn = false;  ///< no stacking within one turn
  /// The dealer's item list from its last pass still holds Cigarettes that
  /// belong to p1. The script reads whether it holds cigarettes from the list
  /// its previous pass built (DealerIntelligence.gd 113-116), and that list
  /// holds p1's items when the dealer then held Adrenaline (lines 127-129,
  /// 146-149). Only a two-seat table against the dealer reads it.
  bool dealerListCigs = false;

  int aliveCount() const;
  /// The only seat left alive, or -1 when more than one remains.
  int soleSurvivor() const;
  bool roundOver() const { return aliveCount() <= 1; }
  /// True when the tube is empty and the caller must reload before acting.
  bool needsReload() const { return tube.empty(); }

  /// The next seat after `from` in the current direction, skipping the dead.
  int nextSeat(int from) const;

  bool operator==(const GameState& other) const;
  std::string debugString() const;
};

/// What a player can do. Shooting carries a target so multiplayer needs no
/// separate action kind, and items that take a target carry one too.
struct Action {
  enum class Kind : std::uint8_t { Shoot, UseItem } kind = Kind::Shoot;
  std::uint8_t target = 0;     ///< seat being shot, or the seat an item points at
  Item item = Item::Beer;      ///< meaningful when kind == UseItem
  Item stolen = Item::Beer;    ///< the item Adrenaline takes
  std::uint8_t stealFrom = 0;  ///< the seat Adrenaline takes it from
  /// Which copy of the item leaves the hand it is taken from: the seat's own
  /// hand for a use, the victim's for a steal. Counted from the front of that
  /// hand among copies of the same type, from zero.
  std::uint8_t copy = 0;
  /// Set when the hand holds this type in more than one place, so that the
  /// move has to say which copy it means. Display only: it is not compared.
  bool named = false;

  static Action shoot(int seat) {
    Action a;
    a.kind = Kind::Shoot;
    a.target = static_cast<std::uint8_t>(seat);
    return a;
  }
  static Action use(Item item) {
    Action a;
    a.kind = Kind::UseItem;
    a.item = item;
    return a;
  }
  static Action useOn(Item item, int seat) {
    Action a = use(item);
    a.target = static_cast<std::uint8_t>(seat);
    return a;
  }
  /// Take `what` from `from` and use it at once. A stolen restraint still needs
  /// a victim of its own, which is what `victim` names; for everything else it
  /// is ignored.
  static Action steal(int from, Item what, int victim = 0) {
    Action a = use(Item::Adrenaline);
    a.stealFrom = static_cast<std::uint8_t>(from);
    a.stolen = what;
    a.target = static_cast<std::uint8_t>(victim);
    return a;
  }
  /// Use an Adrenaline and take nothing. The game allows it, and the timer then
  /// spends the Adrenaline with nothing to show for it.
  static Action adrenalineAlone(int mover) {
    Action a = use(Item::Adrenaline);
    a.stolen = Item::Adrenaline;
    a.stealFrom = static_cast<std::uint8_t>(mover);
    a.target = static_cast<std::uint8_t>(mover);
    return a;
  }

  /// True for an Adrenaline used on its own.
  bool isAdrenalineAlone() const {
    return kind == Kind::UseItem && item == Item::Adrenaline && stolen == Item::Adrenaline;
  }
  /// True for an Adrenaline that takes another seat's item.
  bool isSteal() const {
    return kind == Kind::UseItem && item == Item::Adrenaline && stolen != Item::Adrenaline;
  }

  bool operator==(const Action& other) const;
  /// "shoot self", "shoot p2", "use Beer #2", "steal Hand Saw from p2 and use
  /// it".
  std::string describe(int actingSeat) const;
};

/// One branch of a transition: a probability and the state it leads to.
struct Outcome {
  double probability = 1.0;
  GameState state;
  /// Set when this branch fired a shell, so callers can narrate it.
  bool shellFired = false;
  Shell shellType = Shell::Unknown;
};

}  // namespace bsr

namespace std {
template <>
struct hash<bsr::GameState> {
  std::size_t operator()(const bsr::GameState& state) const noexcept;
};
}  // namespace std
