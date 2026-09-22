#include "engine/Items.h"

#include <algorithm>
#include <cctype>

namespace bsr {
namespace {

struct Entry {
  const char* name;
  const char* token;
};

constexpr Entry kEntries[kItemCount] = {
    {"Magnifying Glass", "mg"}, {"Beer", "beer"},    {"Cigarettes", "cig"},
    {"Handcuffs", "cuff"},      {"Hand Saw", "saw"}, {"Burner Phone", "phone"},
    {"Adrenaline", "adr"},      {"Inverter", "inv"}, {"Expired Medicine", "med"},
    {"Jammer", "jam"},          {"Remote", "rem"},
};

std::string lowered(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c == ' ' || c == '_' || c == '-') continue;
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

const char* itemName(Item item) {
  return kEntries[itemIndex(item)].name;
}

const char* itemToken(Item item) {
  return kEntries[itemIndex(item)].token;
}

bool itemFromToken(const std::string& text, Item* out) {
  const std::string needle = lowered(text);
  if (needle.empty()) return false;
  for (int i = 0; i < kItemCount; ++i) {
    if (needle == kEntries[i].token || needle == lowered(kEntries[i].name)) {
      *out = itemAt(i);
      return true;
    }
  }
  // A few spellings people actually type.
  struct Alias {
    const char* text;
    Item item;
  };
  static constexpr Alias kAliases[] = {
      {"glass", Item::MagnifyingGlass},
      {"magnifier", Item::MagnifyingGlass},
      {"cigarette", Item::Cigarettes},
      {"smoke", Item::Cigarettes},
      {"handcuffs", Item::Handcuffs},
      {"cuffs", Item::Handcuffs},
      {"handsaw", Item::HandSaw},
      {"saw", Item::HandSaw},
      {"burner", Item::BurnerPhone},
      {"adrenaline", Item::Adrenaline},
      {"inverter", Item::Inverter},
      {"medicine", Item::ExpiredMedicine},
      {"expiredmedicine", Item::ExpiredMedicine},
      {"jammer", Item::Jammer},
      {"remote", Item::Remote},
  };
  for (const Alias& alias : kAliases) {
    if (needle == alias.text) {
      *out = alias.item;
      return true;
    }
  }
  return false;
}

}  // namespace bsr
