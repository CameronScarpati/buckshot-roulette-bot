#include "engine/State.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sstream>

namespace bsr {

int Hand::count(Item item) const {
  int total = 0;
  for (int i = 0; i < len; ++i) {
    if (at[static_cast<std::size_t>(i)] == item) ++total;
  }
  return total;
}

void Hand::append(Item item) {
  assert(len < kMaxItemsPerSeat);
  if (len >= kMaxItemsPerSeat) return;
  at[len] = item;
  ++len;
}

void Hand::removeAt(int index) {
  if (index < 0 || index >= len) return;
  for (int i = index; i + 1 < len; ++i) {
    at[static_cast<std::size_t>(i)] = at[static_cast<std::size_t>(i) + 1];
  }
  --len;
  at[len] = Item::MagnifyingGlass;
}

int Hand::indexOfCopy(Item item, int ordinal) const {
  int seen = 0;
  for (int i = 0; i < len; ++i) {
    if (at[static_cast<std::size_t>(i)] != item) continue;
    if (seen == ordinal) return i;
    ++seen;
  }
  return -1;
}

bool Hand::removeCopy(Item item, int ordinal) {
  const int index = indexOfCopy(item, ordinal);
  if (index < 0) return false;
  removeAt(index);
  return true;
}

void Hand::sortCanonical() {
  std::stable_sort(at.begin(), at.begin() + len,
                   [](Item a, Item b) { return itemIndex(a) < itemIndex(b); });
}

int Hand::runs(Item item) const {
  int total = 0;
  for (int i = 0; i < len; ++i) {
    if (at[static_cast<std::size_t>(i)] != item) continue;
    if (i == 0 || at[static_cast<std::size_t>(i - 1)] != item) ++total;
  }
  return total;
}

int Hand::ordinalAt(int index) const {
  const Item item = at[static_cast<std::size_t>(index)];
  int ordinal = 0;
  for (int i = 0; i < index; ++i) {
    if (at[static_cast<std::size_t>(i)] == item) ++ordinal;
  }
  return ordinal;
}

bool Hand::operator==(const Hand& other) const {
  return len == other.len && at == other.at;
}

int PlayerState::itemCount() const {
  return hand.len;
}

bool PlayerState::operator==(const PlayerState& other) const {
  return hp == other.hp && maxHp == other.maxHp && cuffed == other.cuffed &&
         skipConsumed == other.skipConsumed && hand == other.hand;
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
      direction != other.direction || cuffUsedThisTurn != other.cuffUsedThisTurn ||
      dealerListCigs != other.dealerListCigs) {
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
    const Hand& hand = players[i].hand;
    for (int k = 0; k < hand.len; ++k) {
      const Item item = hand.at[static_cast<std::size_t>(k)];
      out << " " << itemToken(item);
      if (hand.runs(item) > 1) out << "#" << hand.ordinalAt(k) + 1;
    }
  }
  if (dealerListCigs) out << " | listcigs";
  return out.str();
}

bool Action::operator==(const Action& other) const {
  return kind == other.kind && target == other.target && item == other.item &&
         stolen == other.stolen && stealFrom == other.stealFrom && copy == other.copy;
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
  if (isAdrenalineAlone()) return "use Adrenaline";
  if (item == Item::Adrenaline) {
    out << "steal " << itemName(stolen);
    if (named) out << " #" << (static_cast<int>(copy) + 1);
    out << " from p" << (static_cast<int>(stealFrom) + 1) << " and use it";
    if (itemNeedsTarget(stolen)) out << " on p" << (static_cast<int>(target) + 1);
    return out.str();
  }
  out << "use " << itemName(item);
  if (named) out << " #" << (static_cast<int>(copy) + 1);
  if (itemNeedsTarget(item)) out << " on p" << (static_cast<int>(target) + 1);
  return out.str();
}

}  // namespace bsr

namespace std {

std::size_t hash<bsr::GameState>::operator()(const bsr::GameState& state) const noexcept {
  // Mixed in 64 bits on every platform, so a 32-bit size_t (WebAssembly)
  // takes the same steps and only the final value is narrowed.
  std::uint64_t h = 1469598103934665603ULL;
  auto mix = [&h](std::uint64_t value) {
    h ^= value + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  };
  mix(state.playerCount);
  mix(state.current);
  mix(static_cast<std::size_t>(state.direction + 1));
  mix(state.cuffUsedThisTurn ? 1u : 0u);
  mix(state.tube.live);
  mix(state.tube.blank);
  mix(state.tube.sawed ? 1u : 0u);
  mix((state.tube.chamberInverted ? 1u : 0u) + (state.tube.pinnedFlip ? 2u : 0u));
  for (int i = 0; i < bsr::kMaxShells; ++i) {
    mix(static_cast<std::size_t>(state.tube.truth[i]) * 31u + state.tube.knownBy[i]);
  }
  for (int i = 0; i < state.playerCount; ++i) {
    const bsr::PlayerState& player = state.players[i];
    mix(player.hp * 131u + player.maxHp);
    mix((player.cuffed ? 2u : 0u) + (player.skipConsumed ? 1u : 0u));
    mix(player.hand.len);
    for (const bsr::Item item : player.hand.at) {
      mix(static_cast<std::uint64_t>(bsr::itemIndex(item)));
    }
  }
  mix(state.dealerListCigs ? 1u : 0u);
  return static_cast<std::size_t>(h);
}

}  // namespace std
