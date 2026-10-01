/// Play a round against the solver or the game's scripted dealer, watch the
/// solver play the dealer, or run a batch of rounds. Every shell and every coin
/// the dealer flips is drawn from a seeded generator, so a whole game replays
/// exactly from its seed.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cli/Args.h"
#include "cli/RuleFlags.h"
#include "engine/Dealer.h"
#include "engine/Notation.h"
#include "engine/Rules.h"
#include "solver/Solver.h"

namespace {

using namespace bsr;

constexpr int kPlayerSeat = 0;
constexpr int kDealerSeat = 1;

struct Options {
  unsigned seed = 1;
  int charges = 4;
  bool chargesGiven = false;
  int players = 2;
  int you = 0;
  int reloadBudget = 2;
  bool quiet = false;
  std::string mode = "don";
  /// Empty when the command line did not name one, so that a flag which needs
  /// the dealer can tell a conflicting choice from no choice.
  std::string opponent;
  bool watch = false;
  int paceMs = 0;
  bool paceGiven = false;
};

/// How stage 3's charges are shown, said once when such a round starts.
constexpr const char* kStoryThreeScale =
    "story3 is shown as five charges: four normal ones and a last, faded one.\n"
    "A seat on 1/5 has no normal charges left and cannot heal.";

/// How the board and the narration count shells, said once at the start of
/// a round on screen.
constexpr const char* kShellNumbers =
    "Shells are numbered from the chamber, which is shell 1. Each time a shell\n"
    "leaves the tube, every shell left moves one place closer to the chamber.";

/// Who seat 2 is in a batch.
enum class BatchOpponent : std::uint8_t { Solver, Heuristic, Dealer };

/// Who is reading a narrated dealer move. The player is told what the dealer
/// does and nothing it learns privately; a spectator is told both.
enum class Audience : std::uint8_t { Nobody, Player, Spectator };

/// One branch, drawn with its probability. Works for the engine's outcomes and
/// for the dealer's passes alike.
template <typename Branch>
const Branch& sample(const std::vector<Branch>& outcomes, std::mt19937* rng) {
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  double roll = uniform(*rng);
  for (const Branch& outcome : outcomes) {
    roll -= outcome.probability;
    if (roll <= 0.0) return outcome;
  }
  return outcomes.back();
}

void pause(int milliseconds) {
  if (milliseconds > 0) std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/// The rule set a mode names, with the settings from the command line on top.
/// The mode has been checked by the caller.
bool buildConfig(const Options& options,
                 const std::vector<std::pair<std::string, std::string>>& ruleSettings,
                 RuleConfig* config) {
  if (options.mode.rfind("story", 0) == 0) {
    *config = RuleConfig::storyRound(options.mode.back() - '0');
  } else if (options.players > 2) {
    *config = RuleConfig::multiplayer(static_cast<std::uint8_t>(options.players),
                                      static_cast<std::uint8_t>(options.charges));
  } else {
    *config = RuleConfig::doubleOrNothing(static_cast<std::uint8_t>(options.charges));
  }
  std::string settingError;
  if (!cli::applyRuleSettings(ruleSettings, config, &settingError)) {
    std::cerr << settingError << "\n";
    return false;
  }
  return true;
}

GameState freshTable(int players, int charges) {
  GameState state;
  state.playerCount = static_cast<std::uint8_t>(players);
  for (int seat = 0; seat < players; ++seat) {
    state.players[seat].hp = static_cast<std::uint8_t>(charges);
    state.players[seat].maxHp = static_cast<std::uint8_t>(charges);
  }
  state.current = 0;
  return state;
}

/// How a live game draws a load. By default it draws from the distribution the
/// solver conditions on (`rules::loadDistribution`) and deals the item count
/// the rule settings give. Against the scripted dealer in Double or Nothing it
/// draws as the pinned script does instead (RoundManager.gd 146-154): a total
/// of 2 to 8 shells with the live count fixed at half of it, rounded down and
/// at least 1, and 2 to 5 items, unless --items-per-load names another count.
struct LiveLoads {
  bool scriptShells = false;
  bool scriptItems = false;
};

/// Deal a load's items and return how many were placed, over every seat. A
/// seat already at the item limit takes none of the ones it is offered.
int dealItems(GameState* state, const RuleConfig& config, const LiveLoads& loads,
              std::mt19937* rng) {
  if (config.itemPool.empty()) return 0;
  // A live game draws the count fresh at every load and hands every seat the
  // same number. Only the solver has to take the count at the middle of its
  // range, because there it would branch.
  int low = config.itemsPerLoad;
  int high = std::max<int>(low, config.itemsPerLoadMax);
  if (loads.scriptItems) {
    low = 2;
    high = 5;
  }
  std::uniform_int_distribution<int> howMany(low, high);
  const int offered = howMany(*rng);
  if (offered == 0) return 0;
  std::uniform_int_distribution<std::size_t> pick(0, config.itemPool.size() - 1);
  int placed = 0;
  for (int seat = 0; seat < state->playerCount; ++seat) {
    PlayerState& player = state->players[seat];
    if (!player.alive()) continue;
    for (int i = 0; i < offered; ++i) {
      if (player.itemCount() >= config.itemLimit) break;
      ++player.items[itemIndex(config.itemPool[pick(*rng)])];
      ++placed;
    }
  }
  return placed;
}

/// Load the gun and deal, and return how many items were placed.
int reload(GameState* state, const RuleConfig& config, const LiveLoads& loads, std::mt19937* rng,
           bool announce) {
  std::uint8_t live = 1;
  std::uint8_t blank = 1;
  if (loads.scriptShells) {
    std::uniform_int_distribution<int> total(2, kMaxShells);
    const int shells = total(*rng);
    live = static_cast<std::uint8_t>(std::max(1, shells / 2));
    blank = static_cast<std::uint8_t>(shells - live);
  } else {
    const auto table = rules::loadDistribution(config);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    double roll = uniform(*rng);
    for (const auto& entry : table) {
      roll -= std::get<2>(entry);
      live = std::get<0>(entry);
      blank = std::get<1>(entry);
      if (roll <= 0.0) break;
    }
  }
  const bool keptSaw = config.sawSurvivesReload && state->tube.sawed;
  state->tube = Tube{};
  state->tube.live = live;
  state->tube.blank = blank;
  state->tube.sawed = keptSaw;
  if (config.reloadClearsCuffs) {
    for (int seat = 0; seat < state->playerCount; ++seat) {
      state->players[seat].cuffed = false;
      state->players[seat].skipConsumed = false;
    }
  }
  const int dealt = dealItems(state, config, loads, rng);
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
  if (announce) {
    std::cout << "\nThe gun is loaded with " << static_cast<int>(live) << " live and "
              << static_cast<int>(blank) << " blank, in an order nobody sees.\n";
  }
  return dealt;
}

/// The position as p1 can see it. The board prints what every seat has seen,
/// and in a round you play that would show you what the other seats learned
/// from their glasses and phones, whoever plays them. Every shell p1 has not
/// seen goes back into the pool, and the other seats' marks come off the ones
/// p1 has, for display only.
GameState playerView(const GameState& state) {
  GameState view = state;
  const std::uint8_t mine = static_cast<std::uint8_t>(1u << kPlayerSeat);
  const int shells = std::min<int>(view.tube.size(), kMaxShells);
  for (int i = 0; i < shells; ++i) {
    if (view.tube.knows(kPlayerSeat, i)) {
      view.tube.knownBy[i] = mine;
    } else {
      view.tube.truth[i] = Shell::Unknown;
      view.tube.knownBy[i] = 0;
    }
  }
  return view;
}

/// A plain heuristic opponent, written out so that the solver has something
/// honest to be measured against. It knows the odds and the obvious tactics and
/// nothing else: no search, no lookahead past the current shell.
Action baselineAction(const GameState& state, const RuleConfig& config) {
  const int seat = state.current;
  const std::vector<Action> actions = rules::legalActions(state, config);
  auto available = [&](const Action& wanted) {
    for (const Action& action : actions) {
      if (action == wanted) return true;
    }
    return false;
  };
  const double live = state.tube.liveProbability(seat, 0);
  const int opponent = state.nextSeat(seat);

  // Finish the job when the shell is certain and the damage is enough.
  const int damage = state.tube.sawed ? 2 : 1;
  if (live >= 1.0 && state.players[opponent].hp <= damage && available(Action::shoot(opponent))) {
    return Action::shoot(opponent);
  }
  // Saw a certain live shell that would otherwise leave the opponent standing.
  if (live >= 1.0 && !state.tube.sawed && state.players[opponent].hp > 1 &&
      available(Action::use(Item::HandSaw))) {
    return Action::use(Item::HandSaw);
  }
  if (available(Action::use(Item::Cigarettes))) return Action::use(Item::Cigarettes);
  if (live > 0.0 && live < 1.0 && available(Action::use(Item::MagnifyingGlass))) {
    return Action::use(Item::MagnifyingGlass);
  }
  if (available(Action::useOn(Item::Handcuffs, opponent))) {
    return Action::useOn(Item::Handcuffs, opponent);
  }
  if (live > 0.5 && !state.tube.sawed && available(Action::use(Item::HandSaw))) {
    return Action::use(Item::HandSaw);
  }
  if (live < 0.5 && available(Action::shoot(seat))) return Action::shoot(seat);
  if (available(Action::shoot(opponent))) return Action::shoot(opponent);
  return actions.front();
}

const char* shellWord(Shell shell) {
  return shell == Shell::Live ? "live" : "blank";
}

/// Print `text` after `lead`, wrapped to 80 columns, with every further line
/// indented to start under the text, or by two spaces when there is no lead.
void printWrapped(const std::string& lead, const std::string& text) {
  constexpr std::size_t kWidth = 80;
  const std::string indent(lead.empty() ? 2 : lead.size(), ' ');
  std::istringstream words(text);
  std::string word;
  std::string line = lead;
  bool first = true;
  while (words >> word) {
    if (!first && line.size() + 1 + word.size() > kWidth) {
      std::cout << line << "\n";
      line = indent + word;
    } else {
      line += (first ? "" : " ") + word;
    }
    first = false;
  }
  std::cout << line << "\n";
}

/// Whether `seat` can tell what the chamber holds from what it has seen and
/// the public counts. For the dealer this is the endless rules' deduction
/// (`FigureOutShell`, engine/Dealer.cpp `deduces`).
bool canTellChamber(const Tube& tube, int seat) {
  if (tube.knows(seat, 0)) return true;
  if (tube.live == 0 || tube.blank == 0) return true;
  int live = tube.live;
  int blank = tube.blank;
  for (int i = 0; i < tube.size(); ++i) {
    if (!tube.knows(seat, i)) continue;
    if (tube.truth[i] == Shell::Live) --live;
    if (tube.truth[i] == Shell::Blank) --blank;
  }
  return live == 0 || blank == 0;
}

/// How the dealer knows the chamber without an item showing it, as a clause
/// with no full stop that names the dealer as `who`. It is called private only
/// when p1 could not tell the same thing from what p1 has seen.
std::string chamberKnowledge(const Tube& tube, Shell known, const std::string& who) {
  const std::string type = shellWord(known);
  if (tube.size() == 1) return "one shell is left, so " + who + " knows the chamber is " + type;
  if (tube.live == 0 || tube.blank == 0) {
    return "every shell left is " + type + ", so " + who + " knows the chamber is " + type;
  }
  if (canTellChamber(tube, kPlayerSeat)) {
    return "both seats can tell from what they have seen that the chamber is " + type;
  }
  // A glass shows the chamber only for a shot or a Beer in the same turn, so a
  // chamber the dealer knew before this pass, and every shell past it that it
  // knows, was heard on a Burner Phone.
  if (tube.knows(kDealerSeat, 0)) {
    return who + " heard this shell on a Burner Phone earlier and p1 did not, so only " + who +
           " knows it is " + type;
  }
  return "shells only " + who + " has heard on a Burner Phone, with the counts, show the " +
         "chamber is " + type;
}

/// Whether the story rules leave the dealer blind to a chamber the counts
/// settle: they do not work the chamber out from the counts, so a tube of one
/// type with more than one shell in it still reads as unknown to them.
bool storyIgnoresCounts(const Tube& tube, dealer::Brain brain) {
  return brain == dealer::Brain::Story && tube.size() > 1 && (tube.live == 0 || tube.blank == 0);
}

/// The clause that goes before a story-rules reason when the counts settle
/// the chamber, so that the reason does not read as contradicting the board.
std::string storyCountsLead(const Tube& tube, dealer::Brain brain) {
  return storyIgnoresCounts(tube, brain)
             ? "the story rules do not work the chamber out from the counts, so "
             : "";
}

/// What the script's coin decides: a shot with no target chosen, or, with a
/// saw in reach and no other item to use, whether to saw or shoot itself.
enum class CoinUse : std::uint8_t { Shot, SawSelf, Saw };

/// Why the coin came out as it did, as a clause with no full stop. The rule
/// is engine/Dealer.cpp's `coin`. The endless rules count the tube, which
/// every seat can: more live than blank shells always means p1 and the saw,
/// fewer always means itself, and only even counts toss a fair coin. The story
/// rules always toss a fair coin.
std::string coinWhy(CoinUse use, const Tube& tube, dealer::Brain brain, bool self) {
  const bool shot = use == CoinUse::Shot;
  std::string always;
  std::string chose;
  switch (use) {
    case CoinUse::Shot:
      always = self ? "it always shoots itself" : "it always shoots p1";
      chose = self ? "shoot itself" : "shoot p1";
      break;
    case CoinUse::SawSelf:
      always = "it always shoots itself rather than saw";
      chose = "shoot itself rather than saw";
      break;
    case CoinUse::Saw:
      always = "it always saws the barrel and shoots p1";
      chose = "saw the barrel and shoot p1";
      break;
  }
  if (brain == dealer::Brain::Story) {
    std::string lead = storyCountsLead(tube, brain);
    if (lead.empty()) lead = "under the story rules, ";
    return lead + (shot ? "with no target" : "with no other item to use and a saw in reach") +
           ", a fair coin chose to " + chose;
  }
  const std::string lead =
      shot ? "with no target and " : "with no other item to use, a saw in reach and ";
  if (tube.live > tube.blank) return lead + "more live than blank shells, " + always;
  if (tube.live < tube.blank) return lead + "fewer live than blank shells, " + always;
  return lead + "as many live as blank shells, a fair coin chose to " + chose;
}

/// What made an item's rule fire, in the order the script checks them.
std::string itemWhy(Item item, const Tube& tube, dealer::Brain brain) {
  switch (item) {
    case Item::MagnifyingGlass:
      return storyCountsLead(tube, brain) +
             "it does not know the chamber and more than one shell is left";
    case Item::Cigarettes:
      return "it is below its full charges";
    case Item::ExpiredMedicine:
      return "it is below its full charges and not on its last one, no cigarettes are within "
             "its reach, and it has not taken medicine this turn";
    case Item::Beer:
      return storyCountsLead(tube, brain) +
             "it does not know the chamber to be live and more than one shell is left";
    case Item::Handcuffs:
      return "p1 is free to be handcuffed and more than one shell is left";
    case Item::HandSaw:
      return "it knows the chamber is live and the barrel is not sawed";
    case Item::BurnerPhone:
      return "more than two shells are left";
    case Item::Inverter:
      return "it knows the chamber is blank";
    case Item::Adrenaline:
    case Item::Jammer:
    case Item::Remote:
      break;
  }
  return "its item rules chose it";
}

/// The dealer's side of a game being played out: the memory it carries from
/// one pass to the next within a turn, and, for a spectator, what set the
/// target it is carrying.
struct DealerSeat {
  dealer::Brain brain = dealer::Brain::Endless;
  dealer::Memory memory;
  bool midTurn = false;
  /// When the target was chosen, as a clause that follows "when ". Empty when
  /// nothing narrated has set a target this turn.
  std::string targetWhen;
  /// Whether the target was chosen from the type of the chamber at the time,
  /// and whether a Beer has racked that shell out since. Both rule sets keep
  /// the target after a Beer (engine/Dealer.cpp, `useItem`).
  bool targetFromChamber = false;
  bool chamberRacked = false;
};

/// Record what set the target the dealer now carries.
void aim(DealerSeat* seat, const std::string& when, bool fromChamber) {
  seat->targetWhen = when;
  seat->targetFromChamber = fromChamber;
  seat->chamberRacked = false;
}

/// Why the dealer shoots a target it chose on an earlier pass of this turn,
/// as a clause that follows "Why: ". The rule applied is that it keeps the
/// target, so the clause leads with that, then says what chose it and whether
/// that shell is still in the chamber.
std::string keptTargetWhy(const DealerSeat& seat) {
  std::string why = "it keeps the target it chose earlier this turn";
  if (seat.targetWhen.empty()) return why;
  why += ", when " + seat.targetWhen;
  if (seat.chamberRacked) {
    why += ". A Beer has since racked that shell out, and the ";
    why += seat.brain == dealer::Brain::Story
               ? "story rules keep both the target and what it learned after a Beer"
               : "endless rules keep the target after a Beer";
  }
  return why;
}

/// Tell the audience what one pass did. `before` and `memory` are the
/// position and the memory the pass started from.
void narrateDealer(const GameState& before, const dealer::Memory& memory,
                   const dealer::Branch& pass, Audience audience, DealerSeat* seat) {
  const bool spectator = audience == Audience::Spectator;
  const std::string player = spectator ? "p1" : "you";
  const std::string players = spectator ? "p1's" : "your";
  const GameState& after = pass.state;
  const PlayerState& dealerAfter = after.players[kDealerSeat];
  const Action& action = pass.action;

  if (action.kind == Action::Kind::Shoot) {
    const bool self = static_cast<int>(action.target) == kDealerSeat;
    std::cout << "The dealer shoots " << (self ? "itself" : player) << ".\n";
    if (spectator) {
      std::string why;
      switch (pass.reason) {
        case dealer::Reason::Deduced:
        case dealer::Reason::LastShell:
          why = chamberKnowledge(before.tube, pass.shellType, "it");
          break;
        case dealer::Reason::KnownTarget:
          why = keptTargetWhy(*seat);
          break;
        case dealer::Reason::SawCoinSelf:
          why = coinWhy(CoinUse::SawSelf, before.tube, seat->brain, true);
          break;
        case dealer::Reason::Coin:
        case dealer::Reason::Item:
        case dealer::Reason::SawCoin:
          why = coinWhy(CoinUse::Shot, before.tube, seat->brain, self);
          break;
      }
      printWrapped("  Why: ", why + ".");
    }
    std::cout << "  The shell was " << (pass.shellType == Shell::Live ? "LIVE" : "blank") << ".\n";
    // A blank fired at oneself hands the turn to nobody else, unless the tube
    // is now empty and the reload decides who moves.
    if (self && pass.shellType == Shell::Blank && !after.roundOver() && !after.needsReload() &&
        after.current == kDealerSeat) {
      std::cout << "  A blank at itself lets the dealer move again.\n";
    }
    return;
  }

  const Item item = pass.stolen ? action.stolen : action.item;
  // Whether the pass started by working out the chamber, which the endless
  // rules do whenever they can and both rule sets do with one shell left
  // (engine/Dealer.cpp, steps 1 and 2). An item rule then acted, and what the
  // dealer worked out is what the chamber held when the pass began. A
  // spectator is told it first, in a sentence of its own.
  const bool deduced =
      !memory.knows && pass.reason == dealer::Reason::Item &&
      ((seat->brain == dealer::Brain::Endless && canTellChamber(before.tube, kDealerSeat)) ||
       before.tube.size() == 1);
  Shell chamber = after.tube.truth[0];
  if (item == Item::Beer) chamber = pass.shellType;
  if (item == Item::Inverter) chamber = Shell::Blank;
  if (deduced && spectator) {
    std::string sentence = chamberKnowledge(before.tube, chamber, "the dealer") + ".";
    sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
    printWrapped("", sentence);
  }

  if (pass.stolen) {
    std::cout << "The dealer uses Adrenaline to take " << players << " " << itemName(item)
              << ", and uses it.\n";
  } else {
    std::cout << "The dealer uses its " << itemName(item) << ".\n";
  }

  switch (item) {
    case Item::MagnifyingGlass:
      if (spectator) {
        std::cout << "  Seen only by the dealer: the chamber is " << shellWord(after.tube.truth[0])
                  << ".\n";
      } else {
        std::cout << "  It looks into the chamber.\n";
      }
      aim(seat, std::string("the glass showed the chamber was ") + shellWord(after.tube.truth[0]),
          true);
      break;
    case Item::Cigarettes:
    case Item::ExpiredMedicine:
      std::cout << "  ";
      if (item == Item::ExpiredMedicine) {
        std::cout << (dealerAfter.hp < before.players[kDealerSeat].hp ? "It fails. "
                                                                      : "It works. ");
      }
      std::cout << "The dealer is now on " << static_cast<int>(dealerAfter.hp) << "/"
                << static_cast<int>(dealerAfter.maxHp) << " charges.\n";
      // The script smokes only below full charges, so a smoke that changes
      // nothing means the dealer is in the band below the heal floor.
      if (item == Item::Cigarettes && dealerAfter.hp == before.players[kDealerSeat].hp) {
        std::cout << "  Healing does nothing on its last charge in this stage.\n";
      }
      break;
    case Item::Beer:
      std::cout << "  The shell it racked out was "
                << (pass.shellType == Shell::Live ? "LIVE" : "blank") << ".\n";
      break;
    case Item::Handcuffs:
      std::cout << "  " << (spectator ? "p1 is" : "You are")
                << " handcuffed and will lose the next turn.\n";
      break;
    case Item::HandSaw:
      std::cout << "  The barrel is sawed: a live shell on the next shot deals two charges.\n";
      break;
    case Item::BurnerPhone:
      if (spectator) {
        int heard = -1;
        for (int i = 1; i < after.tube.size(); ++i) {
          if (after.tube.knows(kDealerSeat, i) && !before.tube.knows(kDealerSeat, i)) heard = i;
        }
        if (heard > 0) {
          std::cout << "  Heard only by the dealer: shell " << (heard + 1) << " is "
                    << shellWord(after.tube.truth[heard]) << ".\n";
        } else {
          std::cout << "  Heard only by the dealer: a shell it already knew.\n";
        }
      } else {
        std::cout << "  It listens to the type of one shell past the chamber.\n";
      }
      break;
    case Item::Inverter:
      if (spectator) {
        std::cout << "  Seen only by the dealer: the chamber "
                  << (after.tube.live > before.tube.live ? "was blank and is now live"
                                                         : "was already live")
                  << ".\n";
      } else {
        std::cout << "  It flips the chamber.\n";
      }
      aim(seat, "the Inverter left the chamber live", true);
      break;
    case Item::Adrenaline:
    case Item::Jammer:
    case Item::Remote:
      break;
  }

  if (pass.reason == dealer::Reason::SawCoin) {
    aim(seat, "it chose to saw the barrel and shoot p1", false);
  } else if (deduced && item != Item::Inverter && pass.memory.target != dealer::Target::None) {
    aim(seat, std::string("it worked out that the chamber was ") + shellWord(chamber), true);
  }
  // A Beer moves the next shell into the chamber and leaves the target alone.
  if (item == Item::Beer && seat->targetFromChamber && pass.memory.target != dealer::Target::None) {
    seat->chamberRacked = true;
  }
  if (!spectator) return;
  if (pass.reason == dealer::Reason::SawCoin) {
    printWrapped("  Why: ", coinWhy(CoinUse::Saw, before.tube, seat->brain, false) + ".");
    return;
  }
  printWrapped("  Why: ", itemWhy(item, before.tube, seat->brain) + ".");
}

/// One pass of the dealer's turn, drawn from the script's branches with the
/// game's generator. Returns false when the position is not the dealer's.
bool dealerPass(GameState* state, DealerSeat* seat, const RuleConfig& config, std::mt19937* rng,
                Audience audience) {
  if (!seat->midTurn) {
    seat->memory = dealer::Memory{};
    aim(seat, "", false);
  }
  const std::vector<dealer::Branch> branches =
      dealer::step(*state, seat->memory, config, seat->brain);
  if (branches.empty()) return false;
  const dealer::Branch& chosen = sample(branches, rng);
  if (audience != Audience::Nobody) narrateDealer(*state, seat->memory, chosen, audience, seat);
  *state = chosen.state;
  seat->memory = chosen.memory;
  seat->midTurn = !chosen.turnOver;
  return true;
}

/// The 95 percent Wilson score interval for a survival rate, which unlike the
/// plain normal interval stays inside 0 to 1 and behaves near either end.
void printWilson(int wins, int rounds) {
  const double z = 1.959963984540054;
  const double n = static_cast<double>(rounds);
  const double rate = static_cast<double>(wins) / n;
  const double scale = 1.0 + z * z / n;
  const double centre = (rate + z * z / (2.0 * n)) / scale;
  const double half = z * std::sqrt(rate * (1.0 - rate) / n + z * z / (4.0 * n * n)) / scale;
  std::cout << "95 percent Wilson score interval for the survival rate: " << std::fixed
            << std::setprecision(1) << 100.0 * std::max(0.0, centre - half) << " to "
            << 100.0 * std::min(1.0, centre + half) << " percent\n";
}

/// Run rounds with nobody watching and report how often seat 1 survives.
int runBatch(int rounds, const Options& options, BatchOpponent opponent, const RuleConfig& config,
             const LiveLoads& loads) {
  const int players = options.players;
  const int charges = config.charges;
  const int reloadBudget = options.reloadBudget;
  dealer::Brain brain = dealer::Brain::Endless;
  if (opponent == BatchOpponent::Dealer) dealer::brainFor(config, &brain);
  int wins = 0;
  int unfinished = 0;
  long long moves = 0;
  long long nodes = 0;
  // A round of this game can run a long way when both seats keep healing, so a
  // cap keeps a batch bounded. Rounds that hit it are reported rather than
  // counted as a result either way.
  constexpr int kMoveCap = 400;
  for (int round = 0; round < rounds; ++round) {
    std::mt19937 rng(options.seed + static_cast<unsigned>(round));
    GameState state = freshTable(players, charges);
    DealerSeat dealerSeat;
    dealerSeat.brain = brain;
    reload(&state, config, loads, &rng, false);
    int guard = 0;
    while (!state.roundOver() && guard++ < kMoveCap) {
      while (rules::applyPendingSkip(&state)) {
        if (state.roundOver()) break;
      }
      if (state.roundOver()) break;
      if (state.needsReload()) {
        reload(&state, config, loads, &rng, false);
        continue;
      }
      if (opponent == BatchOpponent::Dealer && state.current == kDealerSeat) {
        if (!dealerPass(&state, &dealerSeat, config, &rng, Audience::Nobody)) break;
        ++moves;
        continue;
      }
      Action action;
      if (state.current == 0 || opponent == BatchOpponent::Solver) {
        SolveOptions solveOptions;
        solveOptions.seat = state.current;
        solveOptions.reloadBudget = reloadBudget;
        solveOptions.opponent = players > 2 ? OpponentModel::Paranoid : OpponentModel::Optimal;
        if (opponent == BatchOpponent::Dealer) solveOptions.opponent = OpponentModel::Dealer;
        const SolveResult result = solve(state, config, solveOptions);
        nodes += result.nodes;
        if (result.ranked.empty()) break;
        action = result.ranked.front().action;
      } else {
        action = baselineAction(state, config);
      }
      const std::vector<Outcome> outcomes = rules::apply(state, action, config);
      if (outcomes.empty()) break;
      state = sample(outcomes, &rng).state;
      ++moves;
    }
    if (state.soleSurvivor() == 0) ++wins;
    if (!state.roundOver()) ++unfinished;
  }
  std::cout << "rounds " << rounds << ", seed " << options.seed << ", ";
  if (options.mode != "don") std::cout << "story stage " << options.mode.back() << ", ";
  std::cout << charges << " charges, " << players << " seats, reload budget " << reloadBudget
            << "\n";
  const char* against = "(solver, against the solver)";
  if (opponent == BatchOpponent::Heuristic) against = "(solver, against the heuristic)";
  if (opponent == BatchOpponent::Dealer) against = "(solver, against the scripted dealer)";
  std::cout << "seat 1 " << against << " survived " << wins << " of " << rounds << " rounds, "
            << std::fixed << std::setprecision(1)
            << (100.0 * static_cast<double>(wins) / static_cast<double>(rounds)) << " percent\n";
  if (opponent == BatchOpponent::Dealer) printWilson(wins, rounds);
  std::cout << moves << " moves played, " << nodes << " states examined\n";
  if (unfinished > 0) {
    std::cout << unfinished << " rounds reached the " << kMoveCap
              << " move cap without a winner and are counted as losses\n";
  }
  return 0;
}

/// The line for a seat that loses its turn to a restraint, worded for whoever
/// is reading the round.
std::string lostTurn(int seat, const Options& options, bool againstDealer) {
  const std::string name = "p" + std::to_string(seat + 1);
  if (options.players > 2) return name + " is jammed and loses this turn.";
  if (againstDealer && seat == kDealerSeat) return "The dealer is handcuffed and loses this turn.";
  if (againstDealer && !options.watch && seat == options.you) {
    return "You are handcuffed and lose this turn.";
  }
  return name + " is handcuffed and loses this turn.";
}

/// What narration calls a seat: "you" for the seat being played, "the dealer"
/// for the scripted dealer, and p1, p2 and so on otherwise.
std::string seatName(int seat, const Options& options, bool againstDealer) {
  if (againstDealer && seat == kDealerSeat) return "the dealer";
  if (!options.watch && seat == options.you) return "you";
  return "p" + std::to_string(seat + 1);
}

/// The line after a load: whether items were dealt and who moves, by the
/// configured rule for the turn after a reload. The first load of a round has
/// no turn to keep, so it always names who moves first.
void announceDeal(const GameState& state, const RuleConfig& config, int dealt, bool firstLoad,
                  const Options& options, bool againstDealer) {
  const std::string mover = seatName(state.current, options, againstDealer);
  std::cout << (dealt > 0 ? "New items are dealt" : "No items are dealt") << ", and ";
  if (firstLoad || config.reloadTurn != ReloadTurn::KeepCurrent) {
    std::cout << mover << (mover == "you" ? " move" : " moves") << " first.\n";
  } else {
    std::cout << "the turn stays with " << mover << ".\n";
  }
}

/// Name every seat a move has just passed over. The engine hands the turn past
/// a cuffed seat inside the move itself (`advanceTurn`) and sets that seat's
/// `skipConsumed`, which the board shows as "lost a turn", so the mark
/// appearing is the skip. A reload can take the cuffs
/// off without a skip, which is why cuffs coming off is not the test. When the
/// move emptied the tube and the reload hands the turn to a set seat, the skip
/// cost nobody anything, so it goes unsaid here. Returns those seats, one bit
/// each, for the reload to name.
unsigned narrateSkips(const GameState& before, const GameState& after, const RuleConfig& config,
                      const Options& options, bool againstDealer) {
  const bool reloadDecides = after.needsReload() && config.reloadTurn != ReloadTurn::KeepCurrent;
  unsigned unsaid = 0;
  for (int seat = 0; seat < after.playerCount; ++seat) {
    if (!before.players[seat].skipConsumed && after.players[seat].skipConsumed) {
      if (reloadDecides) {
        unsaid |= 1u << seat;
      } else {
        std::cout << lostTurn(seat, options, againstDealer) << "\n";
      }
    }
  }
  return unsaid;
}

/// Name every seat a reload has just freed: one still cuffed or jammed when
/// the tube ran out, or one whose skip the last move took and `unsaid` holds.
/// Either way that seat loses no turn, because the reload clears the
/// restraint or hands the turn to a set seat.
void narrateRestraintsOff(const GameState& before, const GameState& after, unsigned unsaid,
                          const Options& options, bool againstDealer) {
  for (int seat = 0; seat < after.playerCount; ++seat) {
    const PlayerState& now = after.players[seat];
    const bool wasCuffed = before.players[seat].cuffed && !now.cuffed;
    const bool skipVoided = ((unsaid >> seat) & 1u) != 0u && !now.skipConsumed;
    if (!(wasCuffed || skipVoided) || !now.alive()) continue;
    const std::string name = seatName(seat, options, againstDealer);
    if (options.players > 2) {
      std::cout << "The reload frees " << name << " from the Jammer.\n";
    } else {
      std::cout << "The reload takes the handcuffs off " << name << ".\n";
    }
  }
}

/// A seat's name to start a sentence: "You" and "The dealer" take a capital,
/// and p1, p2 and so on stay as they are.
std::string sentenceName(std::string name) {
  if (name == "you" || name == "the dealer") name[0] = static_cast<char>(name[0] - 'a' + 'A');
  return name;
}

int chooseFromMenu(const std::vector<Action>& actions, const GameState& state) {
  std::cout << "\nYour move:\n";
  for (std::size_t i = 0; i < actions.size(); ++i) {
    std::cout << "  " << (i + 1) << ") " << actions[i].describe(state.current) << "\n";
  }
  while (true) {
    std::cout << "> " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return -1;
    if (line == "q" || line == "quit") return -1;
    long choice = 0;
    if (cli::parseWholeNumber(line, 1, static_cast<long>(actions.size()), &choice)) {
      return static_cast<int>(choice) - 1;
    }
    std::cout << "Pick a number from 1 to " << actions.size() << ", or q to quit.\n";
  }
}

/// The solver's move for p1 when it plays the dealer with a spectator: the
/// board, the moves it ranks highest with their values, and the one it plays.
/// Moves that tie at the top are starred as the advisor stars them, and every
/// starred row prints the same value. The values carry enough decimals that an
/// unstarred row never prints the same number as a starred one. A weighing
/// whose values stop at the reload budget says it is estimated, and the first
/// such weighing in a round sets `budgetNoted` and adds a note saying what
/// that means.
Action watchedSolverMove(const GameState& state, const RuleConfig& config, int reloadBudget,
                         bool* budgetNoted, bool* found) {
  constexpr double kTie = 1e-9;  // the tolerance of SolveResult::bestActions
  SolveOptions solveOptions;
  solveOptions.seat = kPlayerSeat;
  solveOptions.reloadBudget = reloadBudget;
  solveOptions.opponent = OpponentModel::Dealer;
  const SolveResult result = solve(state, config, solveOptions);
  *found = !result.ranked.empty();
  if (!*found) return Action{};
  std::cout << "\n" << notation::board(state);
  std::cout << "p1 (solver) weighs, by its " << (result.truncated ? "estimated " : "")
            << "chance of surviving the round:\n";
  const std::size_t shown = std::min<std::size_t>(3, result.ranked.size());
  const double top = result.ranked.front().value;
  auto fixed = [](double value, int decimals) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
  };
  int decimals = 4;
  for (bool clash = true; clash && decimals < 10;) {
    clash = false;
    for (std::size_t i = 0; i < shown; ++i) {
      const double value = result.ranked[i].value;
      if (std::abs(top - value) > kTie && fixed(value, decimals) == fixed(top, decimals)) {
        clash = true;
      }
    }
    if (clash) ++decimals;
  }
  for (std::size_t i = 0; i < shown; ++i) {
    const bool starred = std::abs(top - result.ranked[i].value) <= kTie;
    std::cout << (starred ? "  * " : "    ") << std::left << std::setw(44)
              << result.ranked[i].action.describe(kPlayerSeat) << std::right
              << fixed(starred ? top : result.ranked[i].value, decimals) << "\n";
  }
  const std::size_t tied = result.bestActions(kTie).size();
  if (tied > 1) {
    std::cout << "  " << tied << " moves tie at the top";
    if (tied > shown) std::cout << " (" << shown << " shown)";
    std::cout << ", and p1 plays the first one listed.\n";
  }
  if (result.truncated && !*budgetNoted) {
    printWrapped("  Note: ",
                 "some lines hit the reload budget and were valued by each seat's share of the "
                 "charges in hand. Every weighing marked estimated holds values like these.");
    *budgetNoted = true;
  }
  const Action action = result.ranked.front().action;
  std::cout << "p1 plays: " << action.describe(kPlayerSeat) << "\n";
  return action;
}

void printHelp() {
  std::cout << "play [--seed N] [--charges N] [--players N] [--reloads N] [--mode MODE]\n"
               "     [--opponent solver|dealer] [--watch] [--pace MS]\n"
               "     [--selfplay ROUNDS | --baseline ROUNDS | --dealer ROUNDS] [--quiet]\n"
               "     [rule settings, listed below]\n\n"
               "With no batch flag and no --watch, play one round yourself as p1. The same\n"
               "seed and flags replay the same round, in any mode and against either\n"
               "opponent.\n\n"
               "  --seed N            seed for the shells, the items and the dealer's coins\n"
               "                      (default 1). A batch seeds its rounds N, N plus 1,\n"
               "                      and so on.\n"
               "  --charges N         charges each seat starts with, 1 to 8 (default 4).\n"
               "  --players N         seats at the table, 2 to 4 (default 2). With more than\n"
               "                      two, the multiplayer rules apply and the solver plays\n"
               "                      every seat but yours, each for its own survival.\n"
               "  --reloads N         how many reloads the solver looks through, 0 to 6\n"
               "                      (default 2).\n"
               "  --opponent solver   p2 is the solver, playing to minimise your chance of\n"
               "                      surviving the round. The default.\n"
               "  --opponent dealer   p2 is the game's scripted dealer, its coins and the\n"
               "                      shells it cannot see drawn from the seed. Two seats\n"
               "                      only. You are told what it does, not what it sees.\n"
               "  --watch             the solver plays p1 against the scripted dealer while\n"
               "                      you watch. Before each of p1's moves it prints the\n"
               "                      board and up to three of the moves it ranks highest,\n"
               "                      with their values; every dealer move comes with the\n"
               "                      rule that made it, including what the dealer learned\n"
               "                      privately.\n"
               "  --pace MS           wait MS milliseconds (0 to 5000, default 0) before\n"
               "                      each move the program makes in a round on screen.\n"
               "  --mode MODE         don for double or nothing (the default), or story1,\n"
               "                      story2 or story3. A story stage sets its own charges,\n"
               "                      so --charges cannot be given with one. story3 is\n"
               "                      shown as five charges: four normal ones and a last,\n"
               "                      faded one. A seat on 1/5 has no normal charges left\n"
               "                      and cannot heal.\n"
               "  --quiet             in a round you play against the solver, leave out the\n"
               "                      chance it gives itself with each of its moves. It\n"
               "                      changes nothing else, and a batch prints the same\n"
               "                      lines with or without it.\n\n"
               "--selfplay runs the solver against itself, --baseline runs it against a\n"
               "heuristic opponent, and --dealer runs it against the scripted dealer. Each\n"
               "reports how often seat 1 survives, and --dealer adds a 95 percent Wilson\n"
               "score interval for that rate.\n\n"
               "Against the dealer, with --opponent dealer, --watch or --dealer, a double\n"
               "or nothing load takes its counts the way the game's script does: 2 to 8\n"
               "shells with the live count half the total, rounded down, and 2 to 5 items\n"
               "a seat unless --items-per-load is given, so live shells never outnumber\n"
               "blanks when a load is dealt. A story stage takes fixed loads in the game,\n"
               "and here draws them at random from the solver's distribution instead,\n"
               "which can deal loads the stage never does. So a survival rate from\n"
               "--dealer in a story stage is not measured on the game's own loads.\n\n"
               "A batch at the default reload budget takes minutes a round. Pass\n"
               "--reloads 0 or 1 for a batch you intend to wait for.\n\n"
            << cli::ruleSettingsHelp();
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  int batchRounds = 0;
  BatchOpponent batchOpponent = BatchOpponent::Solver;
  std::string batchFlag;
  std::vector<std::pair<std::string, std::string>> ruleSettings;
  // The opponent as typed, so that an error names the word the user gave.
  std::string opponentTyped;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    long number = 0;
    if (arg == "--seed") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 4294967295L, &number)) return 2;
      options.seed = static_cast<unsigned>(number);
    } else if (arg == "--charges") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1, 8, &number)) return 2;
      options.charges = static_cast<int>(number);
      options.chargesGiven = true;
    } else if (arg == "--players") {
      if (!cli::nextNumber(argc, argv, &i, arg, 2, kMaxPlayers, &number)) return 2;
      options.players = static_cast<int>(number);
    } else if (arg == "--reloads") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 6, &number)) return 2;
      options.reloadBudget = static_cast<int>(number);
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else if (arg == "--mode") {
      if (!cli::nextValue(argc, argv, &i, arg, &options.mode)) return 2;
      if (options.mode != "don" && options.mode != "story1" && options.mode != "story2" &&
          options.mode != "story3") {
        std::cerr << "--mode takes don, story1, story2 or story3, not " << options.mode << "\n";
        return 2;
      }
    } else if (arg == "--opponent") {
      if (!cli::nextValue(argc, argv, &i, arg, &options.opponent)) return 2;
      opponentTyped = options.opponent;
      // An older name for the minimising opponent is still taken.
      if (options.opponent == "optimal") options.opponent = "solver";
      if (options.opponent != "solver" && options.opponent != "dealer") {
        std::cerr << "--opponent takes solver or dealer, not " << options.opponent << "\n";
        return 2;
      }
    } else if (arg == "--watch") {
      options.watch = true;
    } else if (arg == "--pace") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 5000, &number)) return 2;
      options.paceMs = static_cast<int>(number);
      options.paceGiven = true;
    } else if (cli::isRuleSetting(arg)) {
      std::string value;
      if (!cli::nextValue(argc, argv, &i, arg, &value)) return 2;
      std::string settingError;
      RuleConfig probe;
      if (!cli::applyRuleSetting(arg, value, &probe, &settingError)) {
        std::cerr << settingError << "\n";
        return 2;
      }
      ruleSettings.emplace_back(arg, value);
    } else if (arg == "--selfplay" || arg == "--baseline" || arg == "--dealer") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1, 100000, &number)) return 2;
      if (!batchFlag.empty() && batchFlag != arg) {
        std::cerr << arg << " and " << batchFlag << " are two different batches; pick one\n";
        return 2;
      }
      batchFlag = arg;
      batchRounds = static_cast<int>(number);
      batchOpponent = arg == "--selfplay"   ? BatchOpponent::Solver
                      : arg == "--baseline" ? BatchOpponent::Heuristic
                                            : BatchOpponent::Dealer;
    } else if (arg == "--help" || arg == "-h") {
      printHelp();
      return 0;
    } else {
      std::cerr << "unrecognised option " << arg << "\n";
      return 2;
    }
  }
  options.players = std::max(2, std::min(options.players, static_cast<int>(kMaxPlayers)));
  options.charges = std::max(1, std::min(options.charges, 8));

  // Every way of putting the scripted dealer in seat 2, and whether anything
  // on the line contradicts it.
  const bool againstDealer = options.opponent == "dealer" || options.watch ||
                             (batchRounds > 0 && batchOpponent == BatchOpponent::Dealer);
  const bool story = options.mode != "don";
  if (story && options.chargesGiven) {
    std::cerr << "--mode " << options.mode
              << " sets its own charges, so --charges cannot be given with it\n";
    return 2;
  }
  if (story && options.players > 2) {
    std::cerr << "--mode " << options.mode << " is a two-seat mode, so --players must be 2\n";
    return 2;
  }
  if (againstDealer && options.players > 2) {
    std::cerr << "the scripted dealer plays only at a two-seat table, so --players must be 2\n";
    return 2;
  }
  if (againstDealer && options.opponent == "solver") {
    std::cerr << (options.watch ? "--watch" : "--dealer")
              << " plays the scripted dealer, so it cannot be given --opponent " << opponentTyped
              << "\n";
    return 2;
  }
  if (batchRounds > 0 && (options.watch || options.paceGiven)) {
    std::cerr << (options.watch ? "--watch plays one round on screen"
                                : "--pace waits before each move in a round on screen")
              << ", so it cannot be given with " << batchFlag << "\n";
    return 2;
  }
  if (batchRounds > 0 && batchOpponent != BatchOpponent::Dealer && againstDealer) {
    std::cerr << batchFlag << " does not play the scripted dealer; use --dealer ROUNDS\n";
    return 2;
  }

  RuleConfig config;
  if (!buildConfig(options, ruleSettings, &config)) return 2;

  // Against the scripted dealer in Double or Nothing a live game draws its
  // loads as the script does. The story stages take fixed loads in the game,
  // which are not in the pinned scripts, so they draw from the solver's
  // distribution like every other game.
  LiveLoads loads;
  if (againstDealer && !story) {
    loads.scriptShells = true;
    loads.scriptItems = true;
    for (const auto& setting : ruleSettings) {
      if (setting.first == "--items-per-load") loads.scriptItems = false;
    }
  }

  if (batchRounds > 0) return runBatch(batchRounds, options, batchOpponent, config, loads);

  GameState state = freshTable(options.players, config.charges);
  DealerSeat dealerSeat;
  dealer::brainFor(config, &dealerSeat.brain);
  const Audience audience = options.watch ? Audience::Spectator : Audience::Player;

  std::mt19937 rng(options.seed);
  if (options.watch) {
    std::cout << "Buckshot Roulette, seed " << options.seed
              << ". The solver is p1 and the scripted dealer is p2.\n";
  } else if (againstDealer) {
    std::cout << "Buckshot Roulette, seed " << options.seed << ". You are p1. The dealer is p2.\n";
  } else {
    std::cout << "Buckshot Roulette, seed " << options.seed << ". You are p1.\n";
  }
  std::cout << kShellNumbers << "\n";
  announceDeal(state, config, reload(&state, config, loads, &rng, true), true, options,
               againstDealer);
  if (options.mode == "story3") std::cout << kStoryThreeScale << "\n";
  bool budgetNoted = false;
  unsigned unsaidSkips = 0;  // skips a move took that the next reload voids

  while (!state.roundOver()) {
    for (int seat = state.current; rules::applyPendingSkip(&state); seat = state.current) {
      std::cout << lostTurn(seat, options, againstDealer) << "\n";
      if (state.roundOver()) break;
    }
    if (state.roundOver()) break;
    if (state.needsReload()) {
      const GameState emptied = state;
      announceDeal(state, config, reload(&state, config, loads, &rng, true), false, options,
                   againstDealer);
      narrateRestraintsOff(emptied, state, unsaidSkips, options, againstDealer);
      unsaidSkips = 0;
      continue;
    }

    if (againstDealer && state.current == kDealerSeat) {
      if (!dealerSeat.midTurn) {
        std::cout << "\n" << notation::board(options.watch ? state : playerView(state));
      }
      pause(options.paceMs);
      const GameState before = state;
      if (!dealerPass(&state, &dealerSeat, config, &rng, audience)) break;
      unsaidSkips |= narrateSkips(before, state, config, options, againstDealer);
      continue;
    }

    Action action;
    if (options.watch) {
      pause(options.paceMs);
      bool found = false;
      action = watchedSolverMove(state, config, options.reloadBudget, &budgetNoted, &found);
      if (!found) break;
    } else {
      // Whoever p2 is, the board you see leaves out what it has looked at.
      std::cout << "\n" << notation::board(playerView(state));
      const std::vector<Action> actions = rules::legalActions(state, config);
      if (actions.empty()) break;

      action = actions.front();
      if (static_cast<int>(state.current) == options.you) {
        const int choice = chooseFromMenu(actions, state);
        if (choice < 0) {
          std::cout << "Leaving the table.\n";
          return 0;
        }
        action = actions[static_cast<std::size_t>(choice)];
      } else {
        pause(options.paceMs);
        SolveOptions solveOptions;
        solveOptions.seat = state.current;
        solveOptions.reloadBudget = options.reloadBudget;
        solveOptions.opponent =
            options.players > 2 ? OpponentModel::Paranoid : OpponentModel::Optimal;
        const SolveResult result = solve(state, config, solveOptions);
        if (!result.ranked.empty()) action = result.ranked.front().action;
        std::cout << "p" << (static_cast<int>(state.current) + 1) << " "
                  << action.describe(state.current);
        if (!options.quiet && !result.ranked.empty()) {
          std::cout << "  (it rates its chances at " << std::fixed << std::setprecision(3)
                    << result.ranked.front().value << ")";
        }
        std::cout << "\n";
      }
    }

    const std::vector<Outcome> outcomes = rules::apply(state, action, config);
    const Outcome& chosen = sample(outcomes, &rng);
    const bool usesItem = action.kind == Action::Kind::UseItem;
    const Item used = action.item == Item::Adrenaline ? action.stolen : action.item;
    if (chosen.shellFired) {
      const char* shell = chosen.shellType == Shell::Live ? "LIVE" : "blank";
      if (usesItem && used == Item::Beer) {
        std::cout << "  The shell " << seatName(state.current, options, againstDealer)
                  << " racked out was " << shell << ".\n";
      } else {
        std::cout << "  The shell was " << shell << ".\n";
      }
    }
    // A blank fired at oneself keeps the turn, unless the tube is now empty
    // and the reload decides who moves.
    const bool atSelf =
        action.kind == Action::Kind::Shoot && static_cast<int>(action.target) == state.current;
    if (atSelf && chosen.shellFired && chosen.shellType == Shell::Blank &&
        !chosen.state.roundOver() && !chosen.state.needsReload() &&
        chosen.state.current == state.current) {
      const std::string name = seatName(state.current, options, againstDealer);
      std::cout << "  A blank at " << (name == "you" ? "yourself" : "itself") << " lets " << name
                << " move again.\n";
    }
    const GameState before = state;
    state = chosen.state;
    if (usesItem && used == Item::ExpiredMedicine) {
      const PlayerState& was = before.players[before.current];
      const PlayerState& now = state.players[before.current];
      const std::string name = sentenceName(seatName(before.current, options, againstDealer));
      std::cout << "  " << (now.hp < was.hp ? "It fails. " : "It works. ") << name
                << (name == "You" ? " are" : " is") << " now on " << static_cast<int>(now.hp) << "/"
                << static_cast<int>(now.maxHp) << " charges.\n";
    }
    // A spectator sees what p1's own glass or phone told it.
    if (options.watch && usesItem && used == Item::MagnifyingGlass) {
      std::cout << "  p1 sees: the chamber is " << shellWord(state.tube.truth[0]) << ".\n";
    }
    if (options.watch && usesItem && used == Item::BurnerPhone) {
      for (int i = 1; i < state.tube.size(); ++i) {
        if (state.tube.knows(kPlayerSeat, i) && !before.tube.knows(kPlayerSeat, i)) {
          std::cout << "  p1 hears: shell " << (i + 1) << " is " << shellWord(state.tube.truth[i])
                    << ".\n";
        }
      }
    }
    if (options.watch && !before.players[kDealerSeat].cuffed && state.players[kDealerSeat].cuffed) {
      std::cout << "  The dealer is handcuffed and will lose the next turn.\n";
    }
    unsaidSkips |= narrateSkips(before, state, config, options, againstDealer);
  }

  const int winner = state.soleSurvivor();
  std::cout << "\n" << notation::board(options.watch ? state : playerView(state));
  if (winner < 0) {
    std::cout << "Nobody is left standing.\n";
  } else if (againstDealer && winner == kDealerSeat) {
    std::cout << "The dealer wins the round.\n";
  } else if (options.watch) {
    std::cout << "p1 (solver) wins the round.\n";
  } else if (winner == options.you) {
    std::cout << "You win the round.\n";
  } else {
    std::cout << "p" << (winner + 1) << " wins the round.\n";
  }
  return 0;
}
