#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "engine/IndexedArray.h"

namespace bsr {

/// Every item in the game. The first five are the base set, the next four are
/// added by Double or Nothing, and the last two only exist in multiplayer.
enum class Item : std::uint8_t {
  MagnifyingGlass = 0,
  Beer,
  Cigarettes,
  Handcuffs,
  HandSaw,
  BurnerPhone,
  Adrenaline,
  Inverter,
  ExpiredMedicine,
  Jammer,
  Remote,
};

constexpr int kItemCount = 11;
constexpr int kBaseItemCount = 5;

using ItemCounts = IndexedArray<std::uint8_t, kItemCount>;

constexpr int itemIndex(Item item) {
  return static_cast<int>(item);
}
constexpr Item itemAt(int index) {
  return static_cast<Item>(index);
}

/// Display name, for transcripts and the advisor.
const char* itemName(Item item);

/// Short token used by the position notation: mg, beer, cig, cuff, saw, phone,
/// adr, inv, med, jam, rem.
const char* itemToken(Item item);

/// Parse a display name or short token, case-insensitively. Returns false when
/// the text names no item.
bool itemFromToken(const std::string& text, Item* out);

/// True when the item points at a seat. Handcuffs name their victim even with
/// two seats at the table, so that a move reads the same in every mode.
constexpr bool itemNeedsTarget(Item item) {
  return item == Item::Handcuffs || item == Item::Jammer || item == Item::Adrenaline;
}

}  // namespace bsr
