#include "engine/Dealer.h"

#include <initializer_list>
#include <utility>

#include "engine/Position.h"
#include "engine/Rules.h"

namespace bsr {
namespace dealer {
namespace {

constexpr int kPlayer = 0;
constexpr int kDealer = 1;
constexpr std::uint8_t kDealerMask = static_cast<std::uint8_t>(1u << kDealer);

/// A pass part of the way through, before it has used an item or shot.
struct Partial {
  double probability = 1.0;
  GameState state;
  Memory memory;
  /// What set the target during this pass, if anything did. A target left
  /// over from an earlier pass is a known target.
  Reason targetReason = Reason::KnownTarget;
};

struct Resolved {
  double probability = 1.0;
  GameState state;
};

/// Branches for the type at `offset`, with `observers` added to whoever has
/// seen it. A position already pinned down gives one branch; otherwise the
/// weights are the shells nobody has placed yet. A pending inversion has been
/// settled before this is reached, so the pool is the plain pool.
std::vector<Resolved> resolveOffset(const GameState& state, int offset, std::uint8_t observers) {
  std::vector<Resolved> out;
  if (state.tube.truth[offset] != Shell::Unknown) {
    Resolved only;
    only.state = state;
    only.state.tube.knownBy[offset] =
        static_cast<std::uint8_t>(only.state.tube.knownBy[offset] | observers);
    out.push_back(only);
    return out;
  }
  const int live = state.tube.unresolvedLive();
  const int blank = state.tube.unresolvedBlank();
  const double total = live + blank;
  if (total <= 0) return out;
  if (live > 0) {
    Resolved branch;
    branch.probability = live / total;
    branch.state = state;
    branch.state.tube.resolve(offset, Shell::Live, observers);
    out.push_back(branch);
  }
  if (blank > 0) {
    Resolved branch;
    branch.probability = blank / total;
    branch.state = state;
    branch.state.tube.resolve(offset, Shell::Blank, observers);
    out.push_back(branch);
  }
  return out;
}

/// Record what the dealer now knows about the chamber, which it has just
/// observed, and aim at whoever that shell is good for.
void learnChamber(const GameState& state, Memory* memory) {
  memory->knows = true;
  memory->known = state.tube.truth[0];
  memory->target = memory->known == Shell::Live ? Target::Player : Target::Self;
}

/// The script's coin, `CoinFlip` (DealerIntelligence.gd lines 421-431), as
/// (probability, face) pairs. The story rules flip a fair coin. The endless
/// rules count the whole tube, which every seat can: more live shells means 1,
/// more blanks means 0, and only a tie is a fair flip.
std::vector<std::pair<double, int>> coin(const Tube& tube, Brain brain) {
  if (brain == Brain::Endless) {
    if (tube.live > tube.blank) return {{1.0, 1}};
    if (tube.live < tube.blank) return {{1.0, 0}};
  }
  return {{0.5, 0}, {0.5, 1}};
}

/// `FigureOutShell` (DealerIntelligence.gd lines 282-303): the dealer knows the
/// chamber when it has seen it, when the tube holds only one type, or when the
/// shells it has seen account for every live or every blank one.
bool deduces(const GameState& state) {
  const Tube& tube = state.tube;
  if (tube.knows(kDealer, 0)) return true;
  if (tube.live == 0 || tube.blank == 0) return true;
  int live = tube.live;
  int blank = tube.blank;
  for (int i = 0; i < tube.size(); ++i) {
    if (!tube.knows(kDealer, i)) continue;
    if (tube.truth[i] == Shell::Live) --live;
    if (tube.truth[i] == Shell::Blank) --blank;
  }
  return live == 0 || blank == 0;
}

/// Whether the player could be handcuffed now. The script checks only that the
/// player is not already cuffed, and it counts a player still serving a skip as
/// cuffed (RoundManager.gd lines 309-323).
bool playerCanBeCuffed(const GameState& state) {
  const PlayerState& player = state.players[kPlayer];
  return player.alive() && !player.cuffed && !player.skipConsumed;
}

/// Whether an item's condition in the scan holds (DealerIntelligence.gd lines
/// 151-201). `hasCigs` is what the script believes about cigarettes this pass.
bool conditionHolds(Item item, const GameState& state, const Memory& memory, bool hasCigs) {
  const Tube& tube = state.tube;
  const PlayerState& me = state.players[kDealer];
  switch (item) {
    case Item::MagnifyingGlass:
      return !memory.knows && tube.size() != 1;
    case Item::Cigarettes:
      return me.hp < me.maxHp;
    case Item::ExpiredMedicine:
      return me.hp < me.maxHp && !hasCigs && !memory.usedMedicine && me.hp != 1;
    case Item::Beer:
      return memory.known != Shell::Live && tube.size() != 1;
    case Item::Handcuffs:
      return playerCanBeCuffed(state) && tube.size() != 1;
    case Item::HandSaw:
      return !tube.sawed && memory.known == Shell::Live;
    case Item::BurnerPhone:
      return tube.size() > 2;
    case Item::Inverter:
      return memory.knows && memory.known == Shell::Blank;
    case Item::Adrenaline:
    case Item::Jammer:
    case Item::Remote:
      return false;
  }
  return false;
}

/// Pay for an item: the dealer's first copy of it, or its first Adrenaline and
/// the player's first copy (DealerIntelligence.gd lines 243-261).
void pay(GameState* state, Item item, bool stolen) {
  if (stolen) {
    state->players[kDealer].hand.removeCopy(Item::Adrenaline, 0);
    state->players[kPlayer].hand.removeCopy(item, 0);
  } else {
    state->players[kDealer].hand.removeCopy(item, 0);
  }
}

Action actionFor(Item item, bool stolen) {
  if (stolen) return Action::steal(kPlayer, item, kPlayer);
  if (itemNeedsTarget(item)) return Action::useOn(item, kPlayer);
  return Action::use(item);
}

Branch itemBranch(double probability, const GameState& state, const Memory& memory, Item item,
                  bool stolen, Reason reason) {
  Branch branch;
  branch.probability = probability;
  branch.state = state;
  branch.memory = memory;
  branch.action = actionFor(item, stolen);
  branch.stolen = stolen;
  branch.reason = reason;
  // Medicine is the only item that can take a charge, and a charge lost can
  // end the round. Nothing follows in this turn then.
  if (state.roundOver() || state.current != kDealer) {
    branch.turnOver = true;
    branch.memory = Memory{};
  }
  return branch;
}

/// Use one item for the dealer and update the memory the way the script does.
void useItem(const Partial& pass, Item item, bool stolen, Reason reason, const RuleConfig& config,
             Brain brain, std::vector<Branch>* out) {
  if (item == Item::BurnerPhone) {
    // Any position past the chamber, whether or not somebody has already seen
    // it (DealerIntelligence.gd lines 187-194). The engine's own phone skips
    // positions its user has seen, which is not what the script does.
    GameState paid = pass.state;
    pay(&paid, item, stolen);
    const int positions = paid.tube.size() - 1;
    const double share = 1.0 / static_cast<double>(positions);
    for (int offset = 1; offset <= positions; ++offset) {
      for (const Resolved& seen : resolveOffset(paid, offset, kDealerMask)) {
        out->push_back(itemBranch(pass.probability * share * seen.probability, seen.state,
                                  pass.memory, item, stolen, reason));
      }
    }
    return;
  }
  if (item == Item::Inverter) {
    // The script writes a live shell into the chamber whatever was there
    // (DealerIntelligence.gd lines 195-201). A chamber the dealer believes is
    // blank but has not seen, which a story-rules Beer can leave behind, is a
    // draw from the pool first.
    GameState paid = pass.state;
    pay(&paid, item, stolen);
    for (const Resolved& seen : resolveOffset(paid, 0, kDealerMask)) {
      GameState next = seen.state;
      if (next.tube.truth[0] == Shell::Blank) next.tube.invertChamber();
      Memory memory = pass.memory;
      memory.known = Shell::Live;
      memory.target = Target::Player;
      out->push_back(
          itemBranch(pass.probability * seen.probability, next, memory, item, stolen, reason));
    }
    return;
  }

  // Everything else is the engine's own item, used by the dealer on its own
  // behalf, with the player as the one restraint target there is.
  for (const Outcome& outcome : rules::apply(pass.state, actionFor(item, stolen), config)) {
    Memory memory = pass.memory;
    switch (item) {
      case Item::MagnifyingGlass:
        learnChamber(outcome.state, &memory);
        break;
      case Item::ExpiredMedicine:
        memory.usedMedicine = true;
        break;
      case Item::Beer:
        // The endless rules forget the chamber but keep the target
        // (DealerIntelligence.gd lines 173-175); the story rules keep both.
        if (brain == Brain::Endless) {
          memory.knows = false;
          memory.known = Shell::Unknown;
        }
        break;
      default:
        break;
    }
    Branch branch = itemBranch(pass.probability * outcome.probability, outcome.state, memory, item,
                               stolen, reason);
    branch.shellFired = outcome.shellFired;
    branch.shellType = outcome.shellType;
    out->push_back(branch);
  }
}

/// Fire at the chosen seat. The shot is the engine's ordinary shot, and the
/// turn is over whatever it does.
void shoot(const Partial& pass, Target target, Reason reason, const RuleConfig& config,
           std::vector<Branch>* out) {
  const int seat = target == Target::Self ? kDealer : kPlayer;
  const Action action = Action::shoot(seat);
  for (const Outcome& outcome : rules::apply(pass.state, action, config)) {
    Branch branch;
    branch.probability = pass.probability * outcome.probability;
    branch.state = outcome.state;
    branch.turnOver = true;
    branch.action = action;
    branch.reason = reason;
    branch.shellFired = outcome.shellFired;
    branch.shellType = outcome.shellType;
    out->push_back(branch);
  }
}

/// Step 0: a chamber an Inverter flipped while nobody could see it becomes the
/// ordinary shell it would fire as, seen by nobody. Only a written position can
/// hold one: in the game the player's Inverter rewrites the shell directly.
std::vector<Partial> settleInversion(const GameState& state, const Memory& memory) {
  std::vector<Partial> out;
  const Tube& tube = state.tube;
  if (!tube.chamberInverted || tube.truth[0] != Shell::Unknown) {
    Partial only;
    only.state = state;
    only.memory = memory;
    out.push_back(only);
    return out;
  }
  const int live = tube.unresolvedLive();
  const int blank = tube.unresolvedBlank();
  const double total = live + blank;
  for (const Shell drawn : {Shell::Live, Shell::Blank}) {
    const int count = drawn == Shell::Live ? live : blank;
    if (count <= 0) continue;
    Partial branch;
    branch.probability = count / total;
    branch.state = state;
    branch.state.tube.resolveChamberDraw(drawn, 0);
    branch.memory = memory;
    out.push_back(branch);
  }
  return out;
}

/// Resolve the chamber with the dealer watching, and let it act on what it saw.
std::vector<Partial> learnFromChamber(const Partial& pass, Reason reason) {
  std::vector<Partial> out;
  for (const Resolved& seen : resolveOffset(pass.state, 0, kDealerMask)) {
    Partial next = pass;
    next.probability = pass.probability * seen.probability;
    next.state = seen.state;
    learnChamber(next.state, &next.memory);
    next.targetReason = reason;
    out.push_back(next);
  }
  return out;
}

}  // namespace

bool brainFor(const RuleConfig& config, Brain* brain) {
  switch (config.mode) {
    case Mode::Story:
      *brain = Brain::Story;
      return true;
    case Mode::DoubleOrNothing:
      *brain = Brain::Endless;
      return true;
    case Mode::Multiplayer:
      return false;
  }
  return false;
}

const char* brainName(Brain brain) {
  return brain == Brain::Story ? "story" : "endless";
}

bool Memory::operator==(const Memory& other) const {
  return knows == other.knows && known == other.known && target == other.target &&
         usedMedicine == other.usedMedicine;
}

bool listCigsAfterPass(const GameState& before, const GameState& after) {
  return before.players[kDealer].hand.holds(Item::Adrenaline) &&
         after.players[kPlayer].hand.holds(Item::Cigarettes);
}

bool checkMemory(const Position& position, std::string* error) {
  const Memory& memory = position.dealerMemory;
  if (memory == Memory{}) return true;
  const GameState& state = position.state;
  if (state.playerCount != 2) {
    *error = "listcigs and dealer need exactly two seats";
    return false;
  }
  if (state.tube.empty()) {
    *error = "dealer needs shells left in the tube";
    return false;
  }
  if (state.current != kDealer) {
    *error = "dealer needs p2 to move";
    return false;
  }
  if (state.players[kDealer].cuffed) {
    *error = "a cuffed seat cannot be in the middle of its turn";
    return false;
  }
  const bool sawLive = state.tube.knows(kDealer, 0) && state.tube.truth[0] == Shell::Live;
  if (memory.knows &&
      (memory.known == Shell::Unknown || (memory.known == Shell::Live && !sawLive))) {
    *error = "dealer=seen needs known=p2:0L or known=p2:0B";
    return false;
  }
  // The memories a turn can leave between passes: the chamber seen live or
  // seen (or believed) blank with the matching target, a target left by an
  // endless Beer or by the saw coin, or no target at all.
  const bool matchesACore =
      memory.knows
          ? (memory.target == (memory.known == Shell::Live ? Target::Player : Target::Self))
          : memory.known == Shell::Unknown;
  if (!matchesACore) {
    *error =
        "dealer must be seen, believes:B, aim:self or aim:p1, optionally followed by med, as in "
        "dealer=seen,med";
    return false;
  }
  if (!memory.knows && memory.target == Target::Player && !state.tube.sawed) {
    *error = "dealer=aim:p1 needs the barrel sawed";
    return false;
  }
  if (state.tube.sawed && memory.target == Target::Self) {
    *error =
        "the dealer saws only when it aims at p1, so a sawed barrel cannot go with this dealer "
        "memory";
    return false;
  }
  return true;
}

bool validateMemory(const Position& position, Brain brain, std::string* error) {
  if (!checkMemory(position, error)) return false;
  const Memory& memory = position.dealerMemory;
  const Tube& tube = position.state.tube;
  const bool sawBlank = tube.knows(kDealer, 0) && tube.truth[0] == Shell::Blank;
  if (brain == Brain::Endless && memory.knows && memory.known == Shell::Blank && !sawBlank) {
    *error = "dealer=believes:B happens only in story mode";
    return false;
  }
  if (brain == Brain::Story && !memory.knows && memory.target == Target::Self) {
    *error = "dealer=aim:self happens only in double or nothing";
    return false;
  }
  return true;
}

std::vector<Branch> step(const GameState& state, const Memory& memory, const RuleConfig& config,
                         Brain brain) {
  std::vector<Branch> out;
  if (state.playerCount != 2 || state.current != kDealer || state.tube.empty() ||
      state.roundOver()) {
    return out;
  }

  std::vector<Partial> passes = settleInversion(state, memory);

  // Step 1: the endless rules work the chamber out from what the dealer has
  // seen and the counts, whenever it does not already know it
  // (DealerIntelligence.gd lines 96-104).
  if (brain == Brain::Endless && !memory.knows) {
    std::vector<Partial> next;
    for (const Partial& pass : passes) {
      if (!deduces(pass.state)) {
        next.push_back(pass);
        continue;
      }
      for (Partial& learned : learnFromChamber(pass, Reason::Deduced)) next.push_back(learned);
    }
    passes = std::move(next);
  }

  // Step 2: with one shell left, both rule sets know what it is
  // (DealerIntelligence.gd lines 106-112).
  if (state.tube.size() == 1) {
    std::vector<Partial> next;
    for (const Partial& pass : passes) {
      // Nothing before this step changes the shell count.
      for (Partial& learned : learnFromChamber(pass, Reason::LastShell)) next.push_back(learned);
    }
    passes = std::move(next);
  }

  for (Partial& pass : passes) {
    const PlayerState& me = pass.state.players[kDealer];
    const PlayerState& player = pass.state.players[kPlayer];
    const bool adrenaline = me.hand.holds(Item::Adrenaline);

    // Step 3: the item scan (DealerIntelligence.gd lines 113-201). Whether the
    // dealer holds cigarettes is read from the list the previous pass built,
    // before this pass rebuilds it: the dealer's own Cigarettes, and the
    // player's when that pass began with the dealer holding Adrenaline. The
    // scan then walks the dealer's items in the order they sit on the table,
    // and the player's after them when the dealer holds Adrenaline, and takes
    // the first whose condition holds.
    const bool hasCigs = me.hand.holds(Item::Cigarettes) || pass.state.dealerListCigs;

    bool used = false;
    for (int side = 0; side < 2 && !used; ++side) {
      const bool stolen = side == 1;
      if (stolen && !adrenaline) break;
      const Hand& hand = stolen ? player.hand : me.hand;
      for (int index = 0; index < hand.size(); ++index) {
        const Item item = hand.at[static_cast<std::size_t>(index)];
        if (!conditionHolds(item, pass.state, pass.memory, hasCigs)) continue;
        useItem(pass, item, stolen, Reason::Item, config, brain, &out);
        used = true;
        break;
      }
    }
    if (used) continue;

    // Step 4: nothing else to use, so a saw within reach is a coin flip
    // between sawing and shooting itself (DealerIntelligence.gd lines 203-215).
    // The script looks for it in the same list, so a saw of the player's counts
    // when the dealer holds Adrenaline.
    const bool ownSaw = me.hand.holds(Item::HandSaw);
    const bool hasSaw = ownSaw || (adrenaline && player.hand.holds(Item::HandSaw));
    if (hasSaw && !pass.state.tube.sawed && pass.memory.known != Shell::Blank) {
      for (const auto& [chance, face] : coin(pass.state.tube, brain)) {
        Partial flipped = pass;
        flipped.probability = pass.probability * chance;
        if (face == 0) {
          flipped.memory.target = Target::Self;
          shoot(flipped, Target::Self, Reason::SawCoinSelf, config, &out);
        } else {
          flipped.memory.target = Target::Player;
          useItem(flipped, Item::HandSaw, !ownSaw, Reason::SawCoin, config, brain, &out);
        }
      }
      continue;
    }

    // Step 5: shoot the chosen target, or flip for one
    // (DealerIntelligence.gd lines 268-280 and 326-329).
    if (pass.memory.target != Target::None) {
      shoot(pass, pass.memory.target, pass.targetReason, config, &out);
      continue;
    }
    for (const auto& [chance, face] : coin(pass.state.tube, brain)) {
      Partial flipped = pass;
      flipped.probability = pass.probability * chance;
      shoot(flipped, face == 0 ? Target::Self : Target::Player, Reason::Coin, config, &out);
    }
  }

  // Every pass rebuilds the list, so every branch leaves the bit the next pass
  // reads (DealerIntelligence.gd 113-149).
  for (Branch& branch : out) branch.state.dealerListCigs = listCigsAfterPass(state, branch.state);
  return out;
}

}  // namespace dealer
}  // namespace bsr
