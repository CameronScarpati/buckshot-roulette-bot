/// Play a round against the solver or the game's scripted dealer, watch the
/// solver play the dealer, or run a batch of rounds. Every load, deal, shell
/// and coin the dealer flips is drawn from a seeded generator (engine/Table.h),
/// so a whole game replays exactly from its seed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cli/Args.h"
#include "cli/BatchReport.h"
#include "cli/RuleFlags.h"
#include "engine/Dealer.h"
#include "engine/Notation.h"
#include "engine/Rules.h"
#include "engine/Table.h"
#include "solver/Solver.h"

namespace {

using namespace bsr;

constexpr int kPlayerSeat = 0;

struct Options {
  unsigned seed = 1;
  /// 0 draws the charges for each round in double or nothing and means 4 with
  /// more than two seats.
  int charges = 0;
  bool chargesGiven = false;
  int players = 2;
  int you = 0;
  int reloadBudget = 2;
  /// The node limit every solve the program makes stops at.
  long long nodeLimit = SolveOptions{}.nodeLimit;
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

/// Who is reading a round on screen. The player is told what every seat does
/// and only what p1 learns; a spectator is told everything, with the rule
/// behind each dealer move.
enum class Audience : std::uint8_t { Player, Spectator };

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

TableOptions tableOptions(const Options& options, const RuleConfig& config, unsigned seed,
                          bool scriptedDealer) {
  TableOptions table;
  table.config = config;
  table.seed = seed;
  table.charges = options.charges;
  table.players = options.players;
  table.scriptedDealer = scriptedDealer;
  return table;
}

/// The charges a batch's rounds start with, as its header says them.
std::string chargesText(const Options& options, const RuleConfig& config) {
  if (config.mode == Mode::Story) return std::to_string(static_cast<int>(config.charges));
  if (options.charges > 0) return std::to_string(options.charges);
  if (config.mode == Mode::Multiplayer) return "4";
  return "2 to 4";
}

/// What a solve for `seat` is asked: the command line's reload budget and node
/// limit, with `opponent` playing the other seats.
SolveOptions solveOptionsFor(const Options& options, int seat, OpponentModel opponent) {
  SolveOptions solveOptions;
  solveOptions.seat = seat;
  solveOptions.reloadBudget = options.reloadBudget;
  solveOptions.nodeLimit = options.nodeLimit;
  solveOptions.opponent = opponent;
  return solveOptions;
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

/// The move in `legal` that `wanted` names. A solver that merges hands which
/// differ only in order names the first copy of a type, and the first run of
/// that type in the hand at the table is the same move.
bool matchLegal(const std::vector<Action>& legal, const Action& wanted, Action* out) {
  for (const Action& action : legal) {
    if (action == wanted) {
      *out = action;
      return true;
    }
  }
  for (const Action& action : legal) {
    Action same = wanted;
    same.copy = action.copy;
    if (action == same) {
      *out = action;
      return true;
    }
  }
  return false;
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

/// Print one event for `audience`. The first line of a move starts the line
/// and what follows it is indented; a load is all on the left. A shell worked
/// out as a move began comes before that move and starts the line as well.
/// The end of the round is printed by the caller, after the final board.
void printEvent(const Event& event, Audience audience) {
  const bool spectator = audience == Audience::Spectator;
  std::vector<std::string> lines;
  std::istringstream text(event.text);
  for (std::string line; std::getline(text, line);) lines.push_back(line);
  switch (event.kind) {
    case Event::Kind::Over:
      return;
    case Event::Kind::Rule:
      if (spectator) printWrapped("  Why: ", event.text);
      return;
    case Event::Kind::Learned:
      if (!spectator && event.privateTo != -1 && event.privateTo != kPlayerSeat) return;
      for (const std::string& line : lines) printWrapped(event.deduced ? "" : "  ", line);
      return;
    case Event::Kind::Load:
      std::cout << "\n";
      for (const std::string& line : lines) printWrapped("", line);
      return;
    case Event::Kind::Skip:
      for (const std::string& line : lines) printWrapped("", line);
      return;
    case Event::Kind::Shot:
    case Event::Kind::Item: {
      // The item a steal takes follows the line that says what was taken.
      const bool detailOnly =
          event.kind == Event::Kind::Item && event.steal && event.item != Item::Adrenaline;
      for (std::size_t i = 0; i < lines.size(); ++i) {
        printWrapped(i == 0 && !detailOnly ? "" : "  ", lines[i]);
      }
      return;
    }
  }
}

/// Print the events the table has logged since `*printed`.
void printNew(const Table& table, std::size_t* printed, Audience audience) {
  const std::vector<Event>& log = table.log();
  for (; *printed < log.size(); ++*printed) printEvent(log[*printed], audience);
}

/// The solver's move for p1 when it plays the dealer with a spectator: the
/// board, the moves it ranks highest with their values, and the one it plays.
/// Moves that tie at the top are starred as the advisor stars them, and every
/// starred row prints the same value. The values carry enough decimals that an
/// unstarred row never prints the same number as a starred one, except a move
/// that only spends an item, which is never starred while a move that does
/// something ties with it. Such a move says so beside its value, and why it
/// leads when it does. A weighing
/// whose values were not all searched to the end of the round says it is
/// estimated. The first weighing in a round that stops at the reload budget
/// sets `budgetNoted` and adds a note saying what that means, and every
/// weighing that stops at the node limit says so. The solver weighs the
/// position p1 can see, not the table's.
Action watchedSolverMove(const Table& table, const RuleConfig& config, const Options& options,
                         bool* budgetNoted, bool* found) {
  constexpr double kTie = 1e-9;  // the tolerance of SolveResult::bestActions
  const SolveResult result = solve(table.view(kPlayerSeat), config,
                                   solveOptionsFor(options, kPlayerSeat, OpponentModel::Dealer));
  Action action;
  *found =
      !result.ranked.empty() && matchLegal(table.legal(), result.ranked.front().action, &action);
  if (!*found) return Action{};
  std::cout << "\n" << notation::board(table.state());
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
  const bool frontSpends = result.ranked.front().spendsOnly;
  for (std::size_t i = 0; i < shown; ++i) {
    const ActionValue& entry = result.ranked[i];
    const bool level = std::abs(top - entry.value) <= kTie;
    const bool starred = level && (frontSpends || !entry.spendsOnly);
    std::cout << (starred ? "  * " : "    ") << std::left << std::setw(44)
              << entry.action.describe(kPlayerSeat) << std::right
              << fixed(level ? top : entry.value, decimals);
    if (entry.spendsOnly) {
      std::cout << "   (only spends the item";
      const SpendReason reason = i == 0 ? spendReason(table.state(), kPlayerSeat, entry.action,
                                                      config, options.reloadBudget)
                                        : SpendReason::None;
      if (reason == SpendReason::Adrenaline) {
        std::cout << ", which keeps it from the dealer's Adrenaline";
      } else if (reason == SpendReason::Room) {
        std::cout << ", which makes room in the hand for the next deal";
      }
      std::cout << ")";
    }
    std::cout << "\n";
  }
  const std::size_t tied = result.bestActions(kTie).size();
  if (tied > 1) {
    std::cout << "  " << tied << " moves tie at the top";
    if (tied > shown) std::cout << " (" << shown << " shown)";
    std::cout << ", and p1 plays the first one listed.\n";
  }
  if (result.budgetReached && !*budgetNoted) {
    printWrapped("  Note: ",
                 "some lines hit the reload budget and were valued by each seat's share of the "
                 "charges in hand. Every weighing marked estimated holds values like these.");
    *budgetNoted = true;
  }
  if (result.nodeLimitHit) {
    printWrapped("  Note: ", "the search stopped at the node limit of " +
                                 std::to_string(options.nodeLimit) +
                                 " positions, and valued every line it had not finished by each "
                                 "seat's share of the charges in hand, so these values may be "
                                 "wrong.");
  }
  return action;
}

/// Whether the chance `seat` gives itself is worked out only from what p1 can
/// see that it knows, so that the number tells p1 nothing the game keeps from
/// it. The game shows what a Burner Phone names and what a Magnifying Glass
/// shows only to the seat that used it (BurnerPhone.gd 6-35,
/// DealerIntelligence.gd 151-158 and 187-194). So the chance is kept back once
/// `seat` has heard a shell on a phone in this load, since p1 cannot tell
/// which shell the phone named or when that shell left the tube, and while
/// `seat` knows a shell that p1 cannot see it know.
bool chanceIsShared(const Table& table, int seat) {
  const Position mine = table.view(kPlayerSeat);
  for (const UnseenRead& read : mine.unseenReads) {
    if (read.seat == seat) return false;
  }
  const Tube theirs = table.view(seat).state.tube;
  for (int offset = 0; offset < theirs.size(); ++offset) {
    if (theirs.knows(seat, offset) && !mine.state.tube.knows(seat, offset)) return false;
  }
  return true;
}

/// Run rounds with nobody watching and report how often seat 1 survives.
/// Every seat the solver plays chooses from what that seat can see.
int runBatch(int rounds, const Options& options, BatchOpponent opponent, const RuleConfig& config) {
  const int players = options.players;
  const int reloadBudget = options.reloadBudget;
  const bool scripted = opponent == BatchOpponent::Dealer;
  int wins = 0;
  int capped = 0;
  long long moves = 0;
  long long nodes = 0;
  long long searches = 0;
  long long stopped = 0;
  for (int round = 0; round < rounds; ++round) {
    Table table(
        tableOptions(options, config, options.seed + static_cast<unsigned>(round), scripted));
    int played = 0;
    while (table.next() != Step::Over && played < cli::kBatchMoveCap) {
      const Step step = table.next();
      if (step == Step::Load) {
        table.load();
        continue;
      }
      ++played;
      ++moves;
      if (step == Step::Dealer) {
        table.dealerPass();
        continue;
      }
      const int seat = table.state().current;
      Action wanted;
      if (seat == kPlayerSeat || opponent == BatchOpponent::Solver) {
        OpponentModel model = players > 2 ? OpponentModel::Paranoid : OpponentModel::Optimal;
        if (scripted) model = OpponentModel::Dealer;
        const SolveResult result =
            solve(table.view(seat), config, solveOptionsFor(options, seat, model));
        nodes += result.nodes;
        ++searches;
        if (result.nodeLimitHit) ++stopped;
        if (result.ranked.empty()) {
          std::cerr << "the solver gave no move for p" << (seat + 1) << " in round " << (round + 1)
                    << ": " << result.assumptions << "\n";
          return 1;
        }
        wanted = result.ranked.front().action;
      } else {
        wanted = baselineAction(table.view(seat).state, config);
      }
      Action action;
      if (!matchLegal(table.legal(), wanted, &action)) {
        std::cerr << "the move " << wanted.describe(seat) << " is not legal at the table\n";
        return 1;
      }
      table.play(action);
    }
    if (!table.state().roundOver()) {
      ++capped;
    } else if (table.state().soleSurvivor() == kPlayerSeat) {
      ++wins;
    }
  }
  std::cout << "rounds " << rounds << ", seed " << options.seed << ", ";
  if (options.mode != "don") std::cout << "story stage " << options.mode.back() << ", ";
  std::cout << chargesText(options, config) << " charges, ";
  if (config.mode == Mode::DoubleOrNothing && options.charges == 0) {
    std::cout << "drawn per round, ";
  }
  std::cout << players << " seats, reload budget " << reloadBudget << "\n";
  const char* against = "(solver, against the solver)";
  if (opponent == BatchOpponent::Heuristic) against = "(solver, against the heuristic)";
  if (opponent == BatchOpponent::Dealer) against = "(solver, against the scripted dealer)";
  std::cout << cli::survivalLines(against, wins, rounds, capped, scripted);
  std::cout << moves << " moves played, " << nodes << " states examined\n";
  if (stopped > 0) {
    std::cout << stopped << " of the solver's " << searches
              << " searches stopped at the node limit of " << options.nodeLimit
              << " positions, so the moves they chose may not be its best\n";
  }
  return 0;
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

void printHelp() {
  std::cout << "play [--seed N] [--charges N] [--players N] [--reloads N] [--mode MODE]\n"
               "     [--opponent solver|dealer] [--watch] [--pace MS] [--node-limit N]\n"
               "     [--selfplay ROUNDS | --baseline ROUNDS | --dealer ROUNDS] [--quiet]\n"
               "     [rule settings, listed below]\n\n"
               "With no batch flag and no --watch, play one round yourself as p1. The same\n"
               "seed and flags replay the same round, in any mode and against either\n"
               "opponent.\n\n"
               "  --seed N            seed for the charges, the loads, the items and the\n"
               "                      dealer's coins (default 1). A batch seeds its rounds\n"
               "                      N, N plus 1, and so on.\n"
               "  --charges N         charges each seat starts with, 1 to 8. In double or\n"
               "                      nothing the default draws 2 to 4 for each round, as\n"
               "                      the game does, and the round says how many it drew.\n"
               "                      With more than two seats the default is 4.\n"
               "  --players N         seats at the table, 2 to 4 (default 2). With more than\n"
               "                      two, the multiplayer rules apply and the solver plays\n"
               "                      every seat but yours, each for its own survival.\n"
               "  --reloads N         how many reloads the solver looks through, 0 to 6\n"
               "                      (default 2).\n"
               "  --node-limit N      stop each search once it has met N new positions,\n"
               "                      1 to 10000000000 (default 40000000). A search that\n"
               "                      stops there values every line it has not finished by\n"
               "                      each seat's share of the charges in hand, so its\n"
               "                      values, and the move it picks, may be wrong. A\n"
               "                      weighing in --watch, a chance the solver gives itself\n"
               "                      and a batch each say when that happened.\n"
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
               "                      lines with or without it. Without it, a seat still\n"
               "                      keeps its chance back whenever the number could tell\n"
               "                      you what only it has seen: for the rest of a load\n"
               "                      once it has heard a shell on a Burner Phone, and\n"
               "                      while it knows a shell its Magnifying Glass showed\n"
               "                      it that you have not seen.\n\n"
               "Every seat the solver plays chooses from what that seat can see: the shells\n"
               "it has seen, and the fact that another seat used a Burner Phone but not\n"
               "what it heard.\n\n"
               "--selfplay runs the solver against itself, --baseline runs it against a\n"
               "heuristic opponent, and --dealer runs it against the scripted dealer. Each\n"
               "reports how often seat 1 survives, and --dealer adds a 95 percent Wilson\n"
               "score interval for that rate. A round that reaches 400 moves without a\n"
               "winner is stopped, left out of the rate and the interval, and counted on\n"
               "a line of its own.\n\n"
               "A double or nothing load takes its counts the way the game's script does:\n"
               "2 to 8 shells with the live count half the total, rounded down and at\n"
               "least 1, so live shells never outnumber blanks, and 2 to 5 items a seat\n"
               "unless --items-per-load is given. The first load of a round at 2 charges\n"
               "deals no Hand Saw. Each load is announced as \"The gun is loaded with N\n"
               "live and M blank.\" A story stage takes its loads from the game's scene\n"
               "data, which has not been extracted, so here they are drawn at random from\n"
               "the solver's distribution and can include loads the stage never deals. So\n"
               "a survival rate from --dealer in a story stage is not measured on the\n"
               "game's own loads.\n\n"
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
      long long seed = 0;
      if (!cli::nextNumber(argc, argv, &i, arg, 0LL, 4294967295LL, &seed)) return 2;
      options.seed = static_cast<unsigned>(seed);
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
    } else if (arg == "--node-limit") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1LL, 10000000000LL, &options.nodeLimit)) return 2;
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

  if (batchRounds > 0) return runBatch(batchRounds, options, batchOpponent, config);

  Table table(tableOptions(options, config, options.seed, againstDealer));
  const Audience audience = options.watch ? Audience::Spectator : Audience::Player;
  std::size_t printed = 0;

  if (options.watch) {
    std::cout << "Buckshot Roulette, seed " << options.seed
              << ". The solver is p1 and the scripted dealer is p2.\n";
  } else if (againstDealer) {
    std::cout << "Buckshot Roulette, seed " << options.seed << ". You are p1. The dealer is p2.\n";
  } else {
    std::cout << "Buckshot Roulette, seed " << options.seed << ". You are p1.\n";
  }
  std::cout << "charges: " << table.charges() << "\n";
  std::cout << kShellNumbers << "\n";
  bool budgetNoted = false;

  while (table.next() != Step::Over) {
    printNew(table, &printed, audience);
    const Step step = table.next();
    if (step == Step::Load) {
      const bool first = table.loadNumber() == 0;
      table.load();
      if (first && options.mode == "story3") {
        printNew(table, &printed, audience);
        std::cout << kStoryThreeScale << "\n";
      }
      continue;
    }
    if (step == Step::Dealer) {
      if (!table.dealerMidTurn()) {
        std::cout << "\n"
                  << notation::board(options.watch ? table.state() : table.view(kPlayerSeat).state);
      }
      pause(options.paceMs);
      table.dealerPass();
      continue;
    }

    const int seat = table.state().current;
    const std::vector<Action> actions = table.legal();
    Action action = actions.front();
    if (options.watch) {
      pause(options.paceMs);
      bool found = false;
      action = watchedSolverMove(table, config, options, &budgetNoted, &found);
      if (!found) break;
    } else {
      // Whoever the other seats are, the board you see leaves out what they
      // have looked at.
      std::cout << "\n" << notation::board(table.view(kPlayerSeat).state);
      if (seat == options.you) {
        const int choice = chooseFromMenu(actions, table.state());
        if (choice < 0) {
          std::cout << "Leaving the table.\n";
          return 0;
        }
        action = actions[static_cast<std::size_t>(choice)];
      } else {
        pause(options.paceMs);
        const OpponentModel model =
            options.players > 2 ? OpponentModel::Paranoid : OpponentModel::Optimal;
        const SolveResult result =
            solve(table.view(seat), config, solveOptionsFor(options, seat, model));
        if (!result.ranked.empty()) matchLegal(actions, result.ranked.front().action, &action);
        if (!options.quiet && !result.ranked.empty() && chanceIsShared(table, seat)) {
          std::cout << "p" << (seat + 1) << " rates its chances at " << std::fixed
                    << std::setprecision(3) << result.ranked.front().value;
          if (result.nodeLimitHit) {
            std::cout << ", from a search that stopped at the node limit, so it may be off";
          }
          std::cout << ".\n";
        }
      }
    }
    table.play(action);
  }

  printNew(table, &printed, audience);
  std::cout << "\n"
            << notation::board(options.watch ? table.state() : table.view(kPlayerSeat).state);
  for (const Event& event : table.log()) {
    if (event.kind == Event::Kind::Over) std::cout << event.text << "\n";
  }
  return 0;
}
