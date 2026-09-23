#include "engine/Config.h"

#include <sstream>

namespace bsr {
namespace {

std::vector<Item> basePool() {
  return {Item::MagnifyingGlass, Item::Beer, Item::Cigarettes, Item::Handcuffs, Item::HandSaw};
}

std::vector<Item> doubleOrNothingPool() {
  std::vector<Item> pool = basePool();
  pool.push_back(Item::BurnerPhone);
  pool.push_back(Item::Adrenaline);
  pool.push_back(Item::Inverter);
  pool.push_back(Item::ExpiredMedicine);
  return pool;
}

std::vector<Item> multiplayerPool() {
  // The nine minus Handcuffs, plus Jammer and Remote.
  std::vector<Item> pool;
  for (Item item : doubleOrNothingPool()) {
    if (item == Item::Handcuffs) continue;
    pool.push_back(item);
  }
  pool.push_back(Item::Jammer);
  pool.push_back(Item::Remote);
  return pool;
}

const char* reloadTurnName(ReloadTurn turn) {
  switch (turn) {
    case ReloadTurn::KeepCurrent:
      return "the seat to move keeps the turn";
    case ReloadTurn::PlayerFirst:
      return "seat 1 acts first";
    case ReloadTurn::DealerFirst:
      return "seat 2 acts first";
  }
  return "";
}

}  // namespace

std::uint8_t RuleConfig::itemsDealtPerLoad() const {
  const int low = itemsPerLoad;
  const int high = itemsPerLoadMax < itemsPerLoad ? itemsPerLoad : itemsPerLoadMax;
  return static_cast<std::uint8_t>((low + high + 1) / 2);
}

RuleConfig RuleConfig::storyRound(int round) {
  RuleConfig config;
  config.mode = Mode::Story;
  // Two charges in stage 1 and four in each of stages 2 and 3. Stage 3 also
  // shows two faded charges, which heal back to nothing and do not stop a
  // fatal shot; the engine models the normal charges only. See the faded
  // charges paragraph in docs/RULES.md.
  config.charges = round <= 1 ? 2 : 4;
  config.itemPool = round <= 1 ? std::vector<Item>{} : basePool();
  // Stage 1 deals nothing, stage 2 deals two per load and stage 3 deals four.
  config.itemsPerLoad = round <= 1 ? 0 : (round == 2 ? 2 : 4);
  config.itemsPerLoadMax = config.itemsPerLoad;
  config.reloadTurn = ReloadTurn::PlayerFirst;
  return config;
}

RuleConfig RuleConfig::doubleOrNothing(std::uint8_t charges) {
  RuleConfig config;
  config.mode = Mode::DoubleOrNothing;
  config.charges = charges;
  config.itemPool = doubleOrNothingPool();
  // The count is redrawn at every load rather than fixed.
  config.itemsPerLoad = 1;
  config.itemsPerLoadMax = 5;
  config.reloadTurn = ReloadTurn::PlayerFirst;
  return config;
}

RuleConfig RuleConfig::multiplayer(std::uint8_t /*players*/, std::uint8_t charges) {
  RuleConfig config;
  config.mode = Mode::Multiplayer;
  config.charges = charges;
  config.itemPool = multiplayerPool();
  // Nothing sourced covers the multiplayer deal, so it stays at the story size.
  config.itemsPerLoad = 2;
  config.itemsPerLoadMax = 2;
  config.reloadTurn = ReloadTurn::KeepCurrent;
  return config;
}

std::string RuleConfig::describe() const {
  std::ostringstream out;
  switch (mode) {
    case Mode::Story:
      out << "story mode";
      break;
    case Mode::DoubleOrNothing:
      out << "double or nothing";
      break;
    case Mode::Multiplayer:
      out << "multiplayer";
      break;
  }
  out << ", " << static_cast<int>(charges) << " charges, ";
  if (itemsPerLoadMax > itemsPerLoad) {
    out << static_cast<int>(itemsPerLoad) << " to " << static_cast<int>(itemsPerLoadMax)
        << " items dealt per load, modelled at " << static_cast<int>(itemsDealtPerLoad());
  } else {
    out << static_cast<int>(itemsPerLoad) << " items dealt per load";
  }
  out << ", after a reload " << reloadTurnName(reloadTurn);
  out << ", a sawed barrel " << (sawSurvivesReload ? "survives" : "does not survive")
      << " a reload";
  if (reloadClearsCuffs) out << ", a reload clears handcuffs";
  return out.str();
}

}  // namespace bsr
