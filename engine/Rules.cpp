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

/// Offsets past the chamber whose type `seat` has not seen.
std::vector<int> unseenFutureOffsets(const GameState& state, int seat) {
  std::vector<int> offsets;
  for (int i = 1; i < state.tube.size(); ++i) {
    if (!state.tube.knows(seat, i)) offsets.push_back(i);
  }
  return offsets;
}

bool holds(const PlayerState& player, Item item) {
  return player.items[itemIndex(item)] > 0;
}

void consume(PlayerState* player, Item item) {
  std::uint8_t& count = player->items[itemIndex(item)];
  if (count > 0) --count;
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
      const std::vector<int> offsets = unseenFutureOffsets(state, seat);
      if (offsets.empty()) {
        Outcome only;
        only.state = state;
        out.push_back(only);
        return out;
      }
      const double share = 1.0 / static_cast<double>(offsets.size());
      for (int offset : offsets) {
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

  auto itemIsUseful = [&](Item item) {
    switch (item) {
      case Item::MagnifyingGlass: {
        if (state.tube.empty()) return false;
        const double p = state.tube.liveProbability(seat, 0);
        return p > 0.0 && p < 1.0;
      }
      case Item::Beer:
        return !state.tube.empty();
      case Item::Cigarettes:
        // Nothing to heal, or nothing healing can reach: the faded band.
        return me.hp < me.maxHp && me.hp >= config.healFloor;
      case Item::Handcuffs:
        return !multiplayer && !state.cuffUsedThisTurn && !restrainable.empty();
      case Item::HandSaw:
        return !state.tube.sawed && !state.tube.empty();
      case Item::BurnerPhone:
        return state.tube.size() >= 2 && !unseenFutureOffsets(state, seat).empty();
      case Item::Inverter:
        return !state.tube.empty();
      case Item::ExpiredMedicine:
        return true;
      case Item::Jammer:
        return multiplayer && !state.cuffUsedThisTurn && !restrainable.empty();
      case Item::Remote:
        return multiplayer && state.aliveCount() > 2;
      case Item::Adrenaline:
        return false;  // handled separately, it needs a victim and a payload
    }
    return false;
  };

  for (int k = 0; k < kItemCount; ++k) {
    const Item item = itemAt(k);
    if (item == Item::Adrenaline || !holds(me, item)) continue;
    if (!itemIsUseful(item)) continue;
    if (item == Item::Handcuffs || item == Item::Jammer) {
      for (int victim : restrainable) actions.push_back(Action::useOn(item, victim));
    } else {
      actions.push_back(Action::use(item));
    }
  }

  if (holds(me, Item::Adrenaline)) {
    for (int other = 0; other < state.playerCount; ++other) {
      if (other == seat || !state.players[other].alive()) continue;
      for (int k = 0; k < kItemCount; ++k) {
        const Item item = itemAt(k);
        if (item == Item::Adrenaline) continue;  // cannot steal adrenaline
        if (state.players[other].items[k] == 0) continue;
        if (!itemIsUseful(item)) continue;
        if (itemNeedsTarget(item)) {
          // A stolen restraint is aimed by the thief, so every legal victim is
          // a separate move.
          for (int victim : restrainable) actions.push_back(Action::steal(other, item, victim));
        } else {
          actions.push_back(Action::steal(other, item));
        }
      }
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
      if (live) damage(&next.players[action.target], hit);
      next.tube.popChamber();
      next.tube.sawed = false;
      const bool selfShot = static_cast<int>(action.target) == seat;
      if (!next.players[seat].alive() || live || !selfShot) {
        advanceTurn(&next);
      }
      out.push_back(branch);
    }
    return out;
  }

  GameState paid = state;
  consume(&paid.players[seat], action.item);
  if (action.item == Item::Adrenaline) {
    consume(&paid.players[action.stealFrom], action.stolen);
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

std::vector<Outcome> reloadOutcomes(const GameState& state, const RuleConfig& config,
                                    bool dealItems) {
  std::vector<Outcome> out;
  for (const auto& entry : loadDistribution(config)) {
    Outcome branch;
    branch.probability = std::get<2>(entry);
    branch.state = state;
    GameState& next = branch.state;
    next.tube = Tube{};
    next.tube.live = std::get<0>(entry);
    next.tube.blank = std::get<1>(entry);
    next.tube.sawed = config.sawSurvivesReload ? state.tube.sawed : false;
    if (config.reloadClearsCuffs) {
      for (int i = 0; i < next.playerCount; ++i) {
        next.players[i].cuffed = false;
        next.players[i].skipConsumed = false;
      }
    }
    if (dealItems) {
      // The deal is not drawn: each living seat gains up to itemsDealtPerLoad()
      // items taken in turn from the pool, starting at its own seat index,
      // which is one deterministic spread rather than a distribution over
      // multisets. Enumerating them would multiply the state space by thousands
      // without changing the ranking of the move being asked about, and the
      // same argument covers the count, which the game redraws at every load
      // and this takes at the middle of its range. docs/RULES.md records both
      // as approximations.
      const std::vector<Item>& pool = config.itemPool;
      if (!pool.empty()) {
        for (int i = 0; i < next.playerCount; ++i) {
          PlayerState& player = next.players[i];
          if (!player.alive()) continue;
          int room = config.itemLimit - player.itemCount();
          int toDeal = std::min<int>(config.itemsDealtPerLoad(), std::max(0, room));
          for (int d = 0; d < toDeal; ++d) {
            const Item item = pool[static_cast<std::size_t>(d + i) % pool.size()];
            ++player.items[itemIndex(item)];
          }
        }
      }
    }
    switch (config.reloadTurn) {
      case ReloadTurn::KeepCurrent:
        break;
      case ReloadTurn::PlayerFirst:
        next.current = 0;
        break;
      case ReloadTurn::DealerFirst:
        next.current = static_cast<std::uint8_t>(next.playerCount > 1 ? 1 : 0);
        break;
    }
    if (!next.players[next.current].alive()) {
      next.current = static_cast<std::uint8_t>(next.nextSeat(next.current));
    }
    next.cuffUsedThisTurn = false;
    out.push_back(branch);
  }
  return out;
}

}  // namespace rules
}  // namespace bsr
