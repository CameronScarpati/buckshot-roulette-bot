#include "engine/Rules.h"

#include <algorithm>
#include <cassert>
#include <tuple>

namespace bsr {
namespace rules {
namespace {

std::uint8_t maskFor(int seat) {
  return static_cast<std::uint8_t>(1u << seat);
}

std::uint8_t maskAll(int playerCount) {
  return static_cast<std::uint8_t>((1u << playerCount) - 1u);
}

/// Plain subtraction, for a failed Expired Medicine. Shots go through
/// `shotDamage`, which knows the third story stage.
void damage(PlayerState* player, int amount) {
  player->hp = static_cast<std::uint8_t>(player->hp > amount ? player->hp - amount : 0);
}

/// Healing a seat below the floor does nothing at all, which is the third story
/// stage's faded band: a seat past its last normal charge cannot be healed.
void heal(PlayerState* player, int amount, int floor) {
  if (player->hp < floor) return;
  const int healed = player->hp + amount;
  player->hp = static_cast<std::uint8_t>(std::min<int>(healed, player->maxHp));
}

/// Hand the turn to the next living seat, consuming one pending skip on the way.
/// A skipped seat loses its cuffs and is marked as owed a turn, which is what
/// stops a second pair of handcuffs before it has played.
void advanceTurn(GameState* state) {
  if (state->roundOver()) return;
  int seat = state->nextSeat(state->current);
  for (int guard = 0; guard < kMaxPlayers + 1; ++guard) {
    PlayerState& player = state->players[seat];
    if (player.cuffed) {
      player.cuffed = false;
      player.skipConsumed = true;
      seat = state->nextSeat(seat);
      continue;
    }
    break;
  }
  state->current = static_cast<std::uint8_t>(seat);
  state->players[seat].skipConsumed = false;
  state->cuffUsedThisTurn = false;
}

/// Branches for the objective type of the chamber. A position the model has
/// already pinned down returns a single branch; otherwise the split follows the
/// shells nobody has placed yet. `observers` receive the answer.
std::vector<Outcome> resolveChamber(const GameState& state, std::uint8_t observers) {
  std::vector<Outcome> out;
  if (state.tube.empty()) return out;
  if (state.tube.truth[0] != Shell::Unknown) {
    Outcome only;
    only.probability = 1.0;
    only.state = state;
    only.state.tube.knownBy[0] = static_cast<std::uint8_t>(only.state.tube.knownBy[0] | observers);
    out.push_back(only);
    return out;
  }
  const int unresolvedLive = state.tube.unresolvedLive();
  const int unresolvedBlank = state.tube.unresolvedBlank();
  const double total = unresolvedLive + unresolvedBlank;
  if (total <= 0) return out;
  // Branch on the shell drawn from the pool. A pending inversion is applied
  // inside resolveChamberDraw, which is why the weights use the pool and the
  // resulting type may be the opposite of the draw.
  if (unresolvedLive > 0) {
    Outcome branch;
    branch.probability = unresolvedLive / total;
    branch.state = state;
    branch.state.tube.resolveChamberDraw(Shell::Live, observers);
    out.push_back(branch);
  }
  if (unresolvedBlank > 0) {
    Outcome branch;
    branch.probability = unresolvedBlank / total;
    branch.state = state;
    branch.state.tube.resolveChamberDraw(Shell::Blank, observers);
    out.push_back(branch);
  }
  return out;
}

/// Seats that a cuff or a jammer may legally point at.
std::vector<int> restrainableSeats(const GameState& state) {
  std::vector<int> seats;
  for (int i = 0; i < state.playerCount; ++i) {
    if (i == state.current) continue;
    const PlayerState& player = state.players[i];
    if (!player.alive() || player.cuffed || player.skipConsumed) continue;
    seats.push_back(i);
  }
  return seats;
}

/// Apply an item that the acting seat has already paid for. Used directly and
/// again by Adrenaline, which pays for a stolen item and then resolves it here.
std::vector<Outcome> applyItemEffect(const GameState& state, const Action& action,
                                     const RuleConfig& config) {
  const int seat = state.current;
  std::vector<Outcome> out;

  switch (action.item) {
    case Item::MagnifyingGlass: {
      // Private: only the user learns the answer, so only the user's mask is set.
      return resolveChamber(state, maskFor(seat));
    }
    case Item::Beer: {
      std::vector<Outcome> branches = resolveChamber(state, maskAll(state.playerCount));
      for (Outcome& branch : branches) {
        branch.shellFired = true;
        branch.shellType = branch.state.tube.truth[0];
        branch.state.tube.popChamber();
      }
      return branches;
    }
    case Item::Cigarettes: {
      Outcome only;
      only.state = state;
      heal(&only.state.players[seat], 1, config.healFloor);
      out.push_back(only);
      return out;
    }
    case Item::Handcuffs:
    case Item::Jammer: {
      Outcome only;
      only.state = state;
      only.state.players[action.target].cuffed = true;
      // One restraint per turn, whichever kind it is.
      only.state.cuffUsedThisTurn = true;
      out.push_back(only);
      return out;
    }
    case Item::HandSaw: {
      Outcome only;
      only.state = state;
      only.state.tube.sawed = true;
      out.push_back(only);
      return out;
    }
    case Item::Inverter: {
      // Nobody learns anything: a chamber the user had already seen flips
      // outright, and an unseen one records the flip and keeps its odds
      // inverted without resolving.
      Outcome only;
      only.state = state;
      only.state.tube.invertChamber();
      out.push_back(only);
      return out;
    }
    case Item::BurnerPhone: {
      // The offset is a draw over the positions past the chamber, whether or
      // not anybody has seen them. A tube of one shell names nothing, and the
      // phone is spent all the same (BurnerPhone.gd 32).
      const std::array<double, kMaxShells> weights =
          phoneOffsetWeights(seat, state.playerCount, state.tube.size());
      for (int offset = 1; offset < state.tube.size(); ++offset) {
        const double share = weights[static_cast<std::size_t>(offset)];
        if (share <= 0.0) continue;
        if (state.tube.truth[offset] != Shell::Unknown) {
          Outcome branch;
          branch.probability = share;
          branch.state = state;
          branch.state.tube.knownBy[offset] =
              static_cast<std::uint8_t>(branch.state.tube.knownBy[offset] | maskFor(seat));
          out.push_back(branch);
          continue;
        }
        const int unresolvedLive = state.tube.unresolvedLive();
        const int unresolvedBlank = state.tube.unresolvedBlank();
        const double total = unresolvedLive + unresolvedBlank;
        if (unresolvedLive > 0) {
          Outcome branch;
          branch.probability = share * unresolvedLive / total;
          branch.state = state;
          branch.state.tube.resolve(offset, Shell::Live, maskFor(seat));
          out.push_back(branch);
        }
        if (unresolvedBlank > 0) {
          Outcome branch;
          branch.probability = share * unresolvedBlank / total;
          branch.state = state;
          branch.state.tube.resolve(offset, Shell::Blank, maskFor(seat));
          out.push_back(branch);
        }
      }
      if (out.empty()) {
        Outcome only;
        only.state = state;
        out.push_back(only);
      }
      return out;
    }
    case Item::ExpiredMedicine: {
      Outcome good;
      good.probability = config.medicineSuccess;
      good.state = state;
      heal(&good.state.players[seat], config.medicineHeal, config.healFloor);
      Outcome bad;
      bad.probability = 1.0 - config.medicineSuccess;
      bad.state = state;
      damage(&bad.state.players[seat], 1);
      if (good.probability > 0.0) out.push_back(good);
      if (bad.probability > 0.0) out.push_back(bad);
      return out;
    }
    case Item::Remote: {
      Outcome only;
      only.state = state;
      only.state.direction = static_cast<std::int8_t>(-only.state.direction);
      out.push_back(only);
      return out;
    }
    case Item::Adrenaline: {
      // Paid for by the caller; resolve the stolen item as if the thief owned
      // it, aimed wherever the action says. A stolen restraint names its own
      // victim, which is not necessarily the seat it was taken from.
      Action inner = Action::use(action.stolen);
      inner.target = action.target;
      return applyItemEffect(state, inner, config);
    }
  }
  return out;
}

}  // namespace

bool applyPendingSkip(GameState* state) {
  PlayerState& player = state->players[state->current];
  if (!player.cuffed) return false;
  player.cuffed = false;
  player.skipConsumed = true;
  const int seat = state->nextSeat(state->current);
  state->current = static_cast<std::uint8_t>(seat);
  state->players[seat].skipConsumed = false;
  state->cuffUsedThisTurn = false;
  return true;
}

std::vector<Action> legalActions(const GameState& state, const RuleConfig& config) {
  std::vector<Action> actions;
  if (state.roundOver() || state.needsReload()) return actions;

  const int seat = state.current;
  const PlayerState& me = state.players[seat];

  actions.push_back(Action::shoot(seat));
  for (int i = 0; i < state.playerCount; ++i) {
    if (i == seat || !state.players[i].alive()) continue;
    actions.push_back(Action::shoot(i));
  }

  const bool multiplayer = config.mode == Mode::Multiplayer;
  const std::vector<int> restrainable = restrainableSeats(state);

  // Whether the seat to move may use an item of this type, its own or stolen.
  // Everything else the game allows, even where it changes nothing.
  auto allowed = [&](Item item) {
    switch (item) {
      case Item::HandSaw:
        return !state.tube.sawed;
      case Item::Handcuffs:
        return !multiplayer && !state.cuffUsedThisTurn && !restrainable.empty();
      case Item::Jammer:
        return multiplayer && !state.cuffUsedThisTurn && !restrainable.empty();
      case Item::Remote:
        return multiplayer && state.aliveCount() > 2;
      case Item::Adrenaline:
        return false;  // listed on its own below, and never stolen
      case Item::MagnifyingGlass:
      case Item::Beer:
      case Item::Cigarettes:
      case Item::BurnerPhone:
      case Item::Inverter:
      case Item::ExpiredMedicine:
        return true;
    }
    return false;
  };

  // One action per run of adjacent copies in `hand`, by type, then by copy,
  // then by restraint victim. `make` builds the action for one copy.
  auto eachRun = [&](const Hand& hand, const auto& make) {
    for (int k = 0; k < kItemCount; ++k) {
      const Item item = itemAt(k);
      if (!allowed(item)) continue;
      const bool named = hand.runs(item) > 1;
      for (int i = 0; i < hand.len; ++i) {
        if (hand.at[static_cast<std::size_t>(i)] != item) continue;
        if (i > 0 && hand.at[static_cast<std::size_t>(i - 1)] == item) continue;
        const std::uint8_t copy = static_cast<std::uint8_t>(hand.ordinalAt(i));
        if (itemNeedsTarget(item)) {
          for (int victim : restrainable) {
            Action action = make(item, victim);
            action.copy = copy;
            action.named = named;
            actions.push_back(action);
          }
        } else {
          Action action = make(item, 0);
          action.copy = copy;
          action.named = named;
          actions.push_back(action);
        }
      }
    }
  };

  eachRun(me.hand, [](Item item, int victim) {
    return itemNeedsTarget(item) ? Action::useOn(item, victim) : Action::use(item);
  });

  if (me.hand.holds(Item::Adrenaline)) {
    actions.push_back(Action::adrenalineAlone(seat));
    for (int other = 0; other < state.playerCount; ++other) {
      if (other == seat || !state.players[other].alive()) continue;
      // A stolen restraint is aimed by the thief, so every legal victim is a
      // separate move.
      eachRun(state.players[other].hand,
              [other](Item item, int victim) { return Action::steal(other, item, victim); });
    }
  }

  return actions;
}

std::vector<Outcome> apply(const GameState& state, const Action& action, const RuleConfig& config) {
  std::vector<Outcome> out;
  const int seat = state.current;

  if (action.kind == Action::Kind::Shoot) {
    std::vector<Outcome> branches = resolveChamber(state, maskAll(state.playerCount));
    for (Outcome& branch : branches) {
      GameState& next = branch.state;
      const bool live = next.tube.truth[0] == Shell::Live;
      const int hit = next.tube.sawed ? 2 : 1;
      branch.shellFired = true;
      branch.shellType = next.tube.truth[0];
      if (live) shotDamage(&next.players[action.target], hit, config);
      next.tube.popChamber();
      const bool selfShot = static_cast<int>(action.target) == seat;
      // The dealer's seat firing a blank into itself goes on with its turn
      // without the end-of-turn reset that spends the saw
      // (DealerIntelligence.gd 305-324, 387). Every other shot spends it.
      const bool keepsSaw = config.dealerSeatBlankKeepsSaw && seat == 1 && state.playerCount == 2 &&
                            selfShot && !live && !next.tube.empty();
      next.tube.sawed = next.tube.sawed && keepsSaw;
      if (!next.players[seat].alive() || live || !selfShot) {
        advanceTurn(&next);
      }
      out.push_back(branch);
    }
    return out;
  }

  GameState paid = state;
  if (action.isAdrenalineAlone()) {
    // Spent with nothing taken. Any Adrenaline a seat spends is its first,
    // since nothing reads where an Adrenaline sits in a hand.
    paid.players[seat].hand.removeCopy(Item::Adrenaline, 0);
    Outcome only;
    only.state = paid;
    out.push_back(only);
    return out;
  }
  if (action.item == Item::Adrenaline) {
    paid.players[seat].hand.removeCopy(Item::Adrenaline, 0);
    paid.players[action.stealFrom].hand.removeCopy(action.stolen, action.copy);
  } else {
    paid.players[seat].hand.removeCopy(action.item, action.copy);
  }
  out = applyItemEffect(paid, action, config);
  if (out.empty()) {
    Outcome only;
    only.state = paid;
    out.push_back(only);
  }
  // Using an item never ends the turn, but it can end the round: medicine can
  // kill its user, and that seat must not keep the turn.
  for (Outcome& branch : out) {
    if (!branch.state.players[seat].alive() && !branch.state.roundOver()) {
      advanceTurn(&branch.state);
    }
  }
  return out;
}

std::vector<std::tuple<std::uint8_t, std::uint8_t, double>> loadDistribution(
    const RuleConfig& config) {
  std::vector<std::tuple<std::uint8_t, std::uint8_t, double>> table;
  if (!config.loadTable.empty()) {
    const double share = 1.0 / static_cast<double>(config.loadTable.size());
    for (const auto& entry : config.loadTable) {
      table.emplace_back(entry.first, entry.second, share);
    }
    return table;
  }
  // Default, documented in docs/RULES.md as unverified: a total of two to eight
  // shells, uniform, then a live count uniform in [1, total - 1] so that every
  // load holds at least one of each and blank-heavy loads really occur.
  const double totalShare = 1.0 / 7.0;
  for (int total = 2; total <= kMaxShells; ++total) {
    const double liveShare = totalShare / static_cast<double>(total - 1);
    for (int live = 1; live < total; ++live) {
      table.emplace_back(static_cast<std::uint8_t>(live), static_cast<std::uint8_t>(total - live),
                         liveShare);
    }
  }
  return table;
}

void reloadInto(GameState* state, std::uint8_t live, std::uint8_t blank, const RuleConfig& config) {
  const bool keepSaw = config.sawSurvivesReload && state->tube.sawed;
  state->tube = Tube{};
  state->tube.live = live;
  state->tube.blank = blank;
  state->tube.sawed = keepSaw;
  if (config.reloadClearsCuffs) {
    for (int i = 0; i < state->playerCount; ++i) {
      state->players[i].cuffed = false;
      state->players[i].skipConsumed = false;
    }
  }
  switch (config.reloadTurn) {
    case ReloadTurn::KeepCurrent:
      break;
    case ReloadTurn::PlayerFirst:
      state->current = 0;
      break;
    case ReloadTurn::DealerFirst:
      state->current = static_cast<std::uint8_t>(state->playerCount > 1 ? 1 : 0);
      break;
  }
  if (!state->players[state->current].alive()) {
    state->current = static_cast<std::uint8_t>(state->nextSeat(state->current));
  }
  state->cuffUsedThisTurn = false;
}

void shotDamage(PlayerState* player, int hit, const RuleConfig& config) {
  const int floor = config.healFloor > 1 && player->hp >= config.healFloor ? 1 : 0;
  player->hp = static_cast<std::uint8_t>(std::max(floor, player->hp - hit));
}

std::array<double, kMaxShells> phoneOffsetWeights(int seat, int playerCount, int size) {
  std::array<double, kMaxShells> weights{};
  if (size < 2) return weights;
  const double share = 1.0 / static_cast<double>(size - 1);
  for (int offset = 1; offset < size && offset < kMaxShells; ++offset) {
    weights[static_cast<std::size_t>(offset)] = share;
  }
  const bool dealersPhone = playerCount == 2 && seat == 1;
  if (!dealersPhone && size == kMaxShells) {
    // The player's phone moves a pick of 7 to 6 (BurnerPhone.gd 13-15).
    weights[6] += weights[7];
    weights[7] = 0.0;
  }
  return weights;
}

std::vector<Outcome> reloadOutcomes(const GameState& state, const RuleConfig& config,
                                    bool dealItems) {
  std::vector<Outcome> out;
  for (const auto& entry : loadDistribution(config)) {
    Outcome branch;
    branch.probability = std::get<2>(entry);
    branch.state = state;
    GameState& next = branch.state;
    reloadInto(&next, std::get<0>(entry), std::get<1>(entry), config);
    if (dealItems) {
      // The deal is not drawn: each living seat gains up to itemsDealtPerLoad()
      // items taken in turn from the pool, starting at its own seat index and
      // added after what it already holds, which is one deterministic spread
      // rather than a distribution over deals. Enumerating them would multiply
      // the state space by thousands without changing the ranking of the move
      // being asked about, and the same argument covers the count, which the
      // game draws at every load and this takes at the middle of its range
      // (2 to 5 in Double or Nothing, modelled at 4). docs/RULES.md records
      // both as approximations.
      const std::vector<Item>& pool = config.itemPool;
      if (!pool.empty()) {
        const int limit = std::min<int>(config.itemLimit, kMaxItemsPerSeat);
        for (int i = 0; i < next.playerCount; ++i) {
          PlayerState& player = next.players[i];
          if (!player.alive()) continue;
          const int room = limit - player.itemCount();
          const int toDeal = std::min<int>(config.itemsDealtPerLoad(), std::max(0, room));
          for (int d = 0; d < toDeal; ++d) {
            player.hand.append(pool[static_cast<std::size_t>(d + i) % pool.size()]);
          }
        }
      }
    }
    out.push_back(branch);
  }
  return out;
}

}  // namespace rules
}  // namespace bsr
