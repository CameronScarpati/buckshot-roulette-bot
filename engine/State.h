#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "engine/Config.h"
#include "engine/Items.h"
#include "engine/Tube.h"

namespace bsr {

struct PlayerState {
  std::uint8_t hp = 0;
  std::uint8_t maxHp = 0;
  ItemCounts items{};
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
  std::uint8_t current = 0;    ///< seat to move
  std::int8_t direction = 1;   ///< +1 clockwise, -1 after a Remote
  Tube tube;
  bool cuffUsedThisTurn = false;  ///< no stacking within one turn

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
  std::uint8_t target = 0;   ///< seat being shot, or the seat an item points at
  Item item = Item::Beer;    ///< meaningful when kind == UseItem
  Item stolen = Item::Beer;  ///< the item Adrenaline takes

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
  static Action steal(int seat, Item what) {
    Action a = useOn(Item::Adrenaline, seat);
    a.stolen = what;
    return a;
  }

  bool operator==(const Action& other) const;
  /// "shoot self", "shoot p2", "use saw", "steal saw from p3".
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
