#include "engine/State.h"

#include <sstream>

namespace bsr {

int PlayerState::itemCount() const {
  int total = 0;
  for (std::uint8_t count : items) total += count;
  return total;
}

bool PlayerState::operator==(const PlayerState& other) const {
  return hp == other.hp && maxHp == other.maxHp && cuffed == other.cuffed &&
         skipConsumed == other.skipConsumed && items == other.items;
}

int GameState::aliveCount() const {
  int count = 0;
  for (int i = 0; i < playerCount; ++i) {
    if (players[i].alive()) ++count;
  }
  return count;
}

int GameState::soleSurvivor() const {
  int found = -1;
  for (int i = 0; i < playerCount; ++i) {
    if (!players[i].alive()) continue;
    if (found >= 0) return -1;
    found = i;
  }
  return found;
}

int GameState::nextSeat(int from) const {
  int seat = from;
  for (int step = 0; step < playerCount; ++step) {
    seat += direction;
    while (seat < 0) seat += playerCount;
    seat %= playerCount;
    if (players[seat].alive()) return seat;
  }
  return from;
}

bool GameState::operator==(const GameState& other) const {
  if (playerCount != other.playerCount || current != other.current ||
      direction != other.direction || cuffUsedThisTurn != other.cuffUsedThisTurn) {
    return false;
  }
  if (!(tube == other.tube)) return false;
  for (int i = 0; i < playerCount; ++i) {
    if (!(players[i] == other.players[i])) return false;
  }
  return true;
}

std::string GameState::debugString() const {
  std::ostringstream out;
  out << "seat " << static_cast<int>(current) << " to move, tube " << static_cast<int>(tube.live)
      << "L" << static_cast<int>(tube.blank) << "B";
  if (tube.sawed) out << " sawed";
  for (int i = 0; i < playerCount; ++i) {
    out << " | p" << (i + 1) << " hp=" << static_cast<int>(players[i].hp);
    if (players[i].cuffed) out << " cuffed";
    for (int k = 0; k < kItemCount; ++k) {
      if (players[i].items[k] > 0) {
        out << " " << itemToken(itemAt(k)) << "x" << static_cast<int>(players[i].items[k]);
      }
    }
  }
  return out.str();
}

bool Action::operator==(const Action& other) const {
  if (kind != other.kind) return false;
  if (kind == Kind::Shoot) return target == other.target;
  if (item != other.item) return false;
  if (item == Item::Adrenaline) {
    if (stolen != other.stolen || stealFrom != other.stealFrom) return false;
    return !itemNeedsTarget(stolen) || target == other.target;
  }
  if (itemNeedsTarget(item) && target != other.target) return false;
  return true;
}

std::string Action::describe(int actingSeat) const {
  std::ostringstream out;
  if (kind == Kind::Shoot) {
    if (static_cast<int>(target) == actingSeat) {
      out << "shoot self";
    } else {
      out << "shoot p" << (static_cast<int>(target) + 1);
    }
    return out.str();
  }
  if (item == Item::Adrenaline) {
    out << "steal " << itemName(stolen) << " from p" << (static_cast<int>(stealFrom) + 1)
        << " and use it";
    if (itemNeedsTarget(stolen)) out << " on p" << (static_cast<int>(target) + 1);
    return out.str();
  }
  out << "use " << itemName(item);
  if (itemNeedsTarget(item)) out << " on p" << (static_cast<int>(target) + 1);
  return out.str();
}

}  // namespace bsr

namespace std {

std::size_t hash<bsr::GameState>::operator()(const bsr::GameState& state) const noexcept {
  std::size_t h = 1469598103934665603ULL;
  auto mix = [&h](std::size_t value) { h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
  mix(state.playerCount);
  mix(state.current);
  mix(static_cast<std::size_t>(state.direction + 1));
  mix(state.cuffUsedThisTurn ? 1u : 0u);
  mix(state.tube.live);
  mix(state.tube.blank);
  mix(state.tube.sawed ? 1u : 0u);
  mix(state.tube.chamberInverted ? 1u : 0u);
  for (int i = 0; i < bsr::kMaxShells; ++i) {
    mix(static_cast<std::size_t>(state.tube.truth[i]) * 31u + state.tube.knownBy[i]);
  }
  for (int i = 0; i < state.playerCount; ++i) {
    const bsr::PlayerState& player = state.players[i];
    mix(player.hp * 131u + player.maxHp);
    mix((player.cuffed ? 2u : 0u) + (player.skipConsumed ? 1u : 0u));
    for (int k = 0; k < bsr::kItemCount; ++k) {
      mix(static_cast<std::size_t>(player.items[k]) * 17u + static_cast<std::size_t>(k));
    }
  }
  return h;
}

}  // namespace std
