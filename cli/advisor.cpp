/// Position advisor. Type the position you are looking at, or narrate the round
/// as it happens, and get every legal move ranked by the probability that you
/// are the last player standing.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define BSR_ISATTY _isatty
#define BSR_FILENO _fileno
#else
#include <unistd.h>
#define BSR_ISATTY isatty
#define BSR_FILENO fileno
#endif

#include "cli/Args.h"
#include "cli/RuleFlags.h"
#include "engine/Notation.h"
#include "engine/Rules.h"
#include "solver/Solver.h"

namespace {

using namespace bsr;

struct Session {
  GameState state;
  RuleConfig config = RuleConfig::doubleOrNothing(4);
  SolveOptions options;
  std::vector<GameState> history;
  /// Rule settings typed on the command line or with `rule`. Kept so that a
  /// later `mode` command, which rebuilds the configuration from a preset,
  /// does not quietly throw them away.
  std::vector<std::pair<std::string, std::string>> ruleSettings;
};

/// Rebuild the settings on top of whatever preset is now in force.
bool reapplySettings(Session* session) {
  std::string error;
  if (cli::applyRuleSettings(session->ruleSettings, &session->config, &error)) return true;
  std::cout << error << "\n";
  return false;
}

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> words;
  std::istringstream stream(line);
  std::string word;
  while (stream >> word) words.push_back(word);
  return words;
}

bool parseSeat(const std::string& text, const GameState& state, int* seat) {
  return cli::parseSeatToken(text, state.playerCount, state.current, seat);
}

bool parseShell(const std::string& text, Shell* shell) {
  if (text == "live" || text == "l") {
    *shell = Shell::Live;
    return true;
  }
  if (text == "blank" || text == "b") {
    *shell = Shell::Blank;
    return true;
  }
  return false;
}

/// Whether the chamber can fire the observed type at all. A chamber that has
/// already been pinned down can only fire what it was pinned to, and one that
/// is still a draw needs the pool to hold a shell that would produce it, which
/// with an inversion pending is the opposite type.
bool chamberCanFire(const Tube& tube, Shell fired, std::string* why) {
  if (tube.canFire(fired)) return true;
  if (tube.empty()) {
    *why = "The tube is empty. Use load to start the next one.\n";
  } else if (tube.truth[0] != Shell::Unknown) {
    *why = "That contradicts the chamber, which is already recorded as ";
    *why += tube.truth[0] == Shell::Live ? "live" : "blank";
    *why += ".\n";
  } else {
    const Shell drawn =
        tube.chamberInverted ? (fired == Shell::Live ? Shell::Blank : Shell::Live) : fired;
    *why = "That contradicts the tube: it does not hold another ";
    *why += drawn == Shell::Live ? "live" : "blank";
    *why += " shell.\n";
  }
  return false;
}

/// A tube cannot hold more shells of a type than its counts allow. Any command
/// that records a fact about a shell has to answer this before it is believed.
bool tubeStillFits(const Tube& tube) {
  int live = 0;
  int blank = 0;
  for (int i = 0; i < tube.size(); ++i) {
    if (tube.truth[i] == Shell::Live) ++live;
    if (tube.truth[i] == Shell::Blank) ++blank;
  }
  return live <= tube.live && blank <= tube.blank;
}

std::string contradictionMessage(Shell shell) {
  return std::string("That contradicts the tube: it does not hold another ") +
         (shell == Shell::Live ? "live" : "blank") + " shell.\n";
}

std::uint8_t allSeats(const GameState& state) {
  return static_cast<std::uint8_t>((1u << state.playerCount) - 1u);
}

/// Apply an action whose chance outcome is already known, by taking the branch
/// that matches what actually happened.
bool applyWithOutcome(Session* session, const Action& action, bool haveShell, Shell shell) {
  GameState before = session->state;
  bool legal = false;
  for (const Action& candidate : rules::legalActions(session->state, session->config)) {
    if (candidate == action) legal = true;
  }
  if (!legal) {
    std::cout << "That move is not available here: the seat to move either does not hold the "
                 "item or cannot use it in this position.\n";
    return false;
  }
  if (haveShell) {
    std::string why;
    if (!chamberCanFire(session->state.tube, shell, &why)) {
      std::cout << why;
      return false;
    }
    // Force the chamber to what was observed. With an inversion pending, the
    // shell drawn from the pool is the opposite of the one that fired, and
    // resolveChamberDraw is what moves the public counts to match.
    if (session->state.tube.chamberInverted) {
      const Shell drawn = shell == Shell::Live ? Shell::Blank : Shell::Live;
      session->state.tube.resolveChamberDraw(drawn, allSeats(session->state));
    } else {
      session->state.tube.resolve(0, shell, allSeats(session->state));
    }
    if (!tubeStillFits(session->state.tube)) {
      session->state = before;
      std::cout << contradictionMessage(shell);
      return false;
    }
  }
  std::vector<Outcome> outcomes = rules::apply(session->state, action, session->config);
  if (outcomes.empty()) {
    session->state = before;
    std::cout << "That move is not available here.\n";
    return false;
  }
  // With the chamber pinned there is only one branch with any weight left.
  const Outcome* chosen = &outcomes.front();
  for (const Outcome& outcome : outcomes) {
    if (outcome.probability > chosen->probability) chosen = &outcome;
  }
  session->history.push_back(before);
  session->state = chosen->state;
  return true;
}

/// Expired medicine has two branches of equal weight, so the caller says which
/// one happened and the matching branch is taken.
bool applyMedicine(Session* session, const Action& action, bool healed) {
  bool legal = false;
  for (const Action& candidate : rules::legalActions(session->state, session->config)) {
    if (candidate == action) legal = true;
  }
  if (!legal) {
    std::cout << "That move is not available here.\n";
    return false;
  }
  const int seat = session->state.current;
  const int before = session->state.players[seat].hp;
  for (const Outcome& outcome : rules::apply(session->state, action, session->config)) {
    const bool wentUp = outcome.state.players[seat].hp > before;
    if (wentUp != healed) continue;
    session->history.push_back(session->state);
    session->state = outcome.state;
    return true;
  }
  std::cout << "That outcome is not possible here.\n";
  return false;
}

void printRanking(const SolveResult& result, const GameState& state, int seat) {
  if (result.ranked.empty()) {
    std::cout << "No legal move from this position.\n";
    return;
  }
  std::cout << "\nAdvising seat p" << (seat + 1) << ", to move: p" << (result.mover + 1);
  if (result.mover != static_cast<int>(state.current)) {
    std::cout << " (p" << (static_cast<int>(state.current) + 1) << " is handcuffed and skipped)";
  }
  std::cout << "\n";
  const double top = result.ranked.front().value;
  for (const ActionValue& entry : result.ranked) {
    // An opponent's ranking is sorted the other way, so the test has to be
    // symmetric or every row looks best.
    const bool best = std::abs(top - entry.value) < 1e-9;
    // A stolen item makes for a long name, and a name that fills the column
    // used to run into the number after it.
    std::string name = entry.action.describe(result.mover);
    if (name.size() >= 34) name += " ";
    std::cout << (best ? "  * " : "    ") << std::left << std::setw(34) << name << std::right
              << std::fixed << std::setprecision(4) << entry.value;
    if (!best) {
      std::cout << "   (" << std::showpos << std::setprecision(4) << (entry.value - top)
                << std::noshowpos << ")";
    }
    std::cout << "\n";
  }
  // A tie among rows that are averages is an artefact of the averaging, not a
  // choice the seat holding the gun faces, so it is only worth saying when the
  // rows mean what they look like.
  const bool rowsAreAverages = result.opponentKnownShells > 0 && result.mover != seat;
  const std::vector<Action> best = result.bestActions();
  if (best.size() > 1 && !rowsAreAverages) {
    std::cout << "  " << best.size() << " moves tie at the top; any of them is optimal.\n";
  }
  if (rowsAreAverages) {
    // Every row above is averaged over a shell p<mover> can see and this seat
    // cannot, so no row says what that seat will actually do. The position is
    // worth what its choice makes it, which is lower than the rows look.
    std::cout << "  p" << (result.mover + 1) << " has looked at " << result.opponentKnownShells
              << " shell" << (result.opponentKnownShells == 1 ? "" : "s")
              << " you have not, so the rows above average over what it saw and you did not. "
                 "It does not have to average: this position is worth "
              << std::fixed << std::setprecision(4) << result.value << " to you.\n";
  }
  if (result.truncated) {
    std::cout << "  Note: the search hit its reload budget in some lines, so those were "
                 "valued by charges in hand.\n";
  }
  std::cout << "  " << result.assumptions << "\n";
  std::cout << "  " << result.nodes << " states examined.\n\n";
}

/// An item that the chosen rule set never deals is still parsed, still printed
/// on the board, and never offered as a move. Saying so is the difference
/// between an answer that looks complete and one that is.
std::string itemsOutsideThePool(const GameState& state, const RuleConfig& config) {
  std::string names;
  for (int index = 0; index < kItemCount; ++index) {
    const Item item = itemAt(index);
    bool held = false;
    for (int seat = 0; seat < state.playerCount; ++seat) {
      if (state.players[seat].items[index] > 0) held = true;
    }
    if (!held) continue;
    if (std::find(config.itemPool.begin(), config.itemPool.end(), item) != config.itemPool.end()) {
      continue;
    }
    if (!names.empty()) names += ", ";
    names += itemName(item);
  }
  if (names.empty()) return names;
  return "These rules never deal " + names +
         ", so a seat holding one has no move that uses it: " + config.describe();
}

void printHelp() {
  std::cout << R"(Commands

  Position
    set <notation>        replace the position, e.g.
                          set p1=3/4[saw,beer] p2=2/4[mg] tube=2L3B turn=p1
    load <n>L<m>B         start a fresh load, clearing knowledge and the saw
    state                 print the position in one line
    board                 print the position as a board
    seat p<N>             advise this seat (default p1)
    mode don|story<N>|mp  rule set: double or nothing, story round N, multiplayer
    reloads <n>           how many reloads to look through (default 2)
    rule <name> <value>   change a rule this engine had to assume, as in
                          rule reload-turn keep. rules lists them all
    rules                 the rule settings and what they are set to

  Edits
    hp p<N> <charges>     set charges
    give p<N> <item>      add an item        take p<N> <item>   remove an item
    turn p<N>             hand the turn to a seat
    cuff p<N>             cuff a seat        uncuff p<N>
    saw                   mark the barrel sawed
    invert                mark the chamber inverted

  Events, as they happen
    shot self live        you shot yourself and it was live
    shot p<N> blank       the seat to move shot p<N> and it was blank
    eject live|blank      a beer ejected a shell of that type
    mg live|blank         a magnifying glass showed the seat to move that shell
    phone <k> live|blank  a burner phone named shell k, counting from 2, since
                          a burner phone never names the chamber
    use <item> [p<N>]     the seat to move used an item with no chance outcome
    use med ok|bad        expired medicine, and how it went
    use adr p<N> <item> [p<M>]
                          the seat to move stole an item and used it, on p<M>
                          when the stolen item needs a target

  Other
    advise (or a blank line)   rank every move
    undo    help    quit
)";
}

/// Print a result as JSON, so another implementation can be compared against
/// this one move by move.
void printJson(const SolveResult& result) {
  std::cout << "{\"value\": " << std::fixed << std::setprecision(12) << result.value
            << ", \"actions\": [";
  for (std::size_t i = 0; i < result.ranked.size(); ++i) {
    if (i > 0) std::cout << ", ";
    std::cout << "{\"action\": \"" << result.ranked[i].action.describe(result.mover)
              << "\", \"value\": " << result.ranked[i].value << "}";
  }
  std::cout << "], \"nodes\": " << result.nodes
            << ", \"truncated\": " << (result.truncated ? "true" : "false") << "}\n";
}

int runOnce(const std::string& position, int seat, int reloads, const std::string& mode,
            bool asJson, const std::vector<std::pair<std::string, std::string>>& ruleSettings) {
  GameState state;
  std::string error;
  if (!notation::parse(position, &state, &error)) {
    std::cerr << error << "\n";
    return 1;
  }
  if (seat < 0 || seat >= state.playerCount) {
    std::cerr << "this position has " << static_cast<int>(state.playerCount)
              << " seats, so --seat must be between 1 and " << static_cast<int>(state.playerCount)
              << "\n";
    return 1;
  }
  RuleConfig config = RuleConfig::doubleOrNothing(state.players[0].maxHp);
  SolveOptions options;
  options.seat = seat;
  options.reloadBudget = reloads;
  if (mode == "mp" || mode == "multiplayer") {
    config = RuleConfig::multiplayer(state.playerCount, state.players[0].maxHp);
    options.opponent = OpponentModel::Paranoid;
  } else if (mode.rfind("story", 0) == 0) {
    long round = 2;
    if (mode.size() > 5 && !cli::parseWholeNumber(mode.substr(5), 1, 3, &round)) {
      std::cerr << "Modes: don, story1, story2, story3, mp.\n";
      return 1;
    }
    config = RuleConfig::storyRound(static_cast<int>(round));
  } else if (mode != "don") {
    std::cerr << "Modes: don, story1, story2, story3, mp.\n";
    return 1;
  }
  std::string settingError;
  if (!cli::applyRuleSettings(ruleSettings, &config, &settingError)) {
    std::cerr << settingError << "\n";
    return 1;
  }
  const std::string outside = itemsOutsideThePool(state, config);
  if (!outside.empty() && !asJson) std::cerr << outside << "\n";
  const SolveResult result = solve(state, config, options);
  if (asJson) {
    printJson(result);
  } else {
    std::cout << notation::board(state);
    printRanking(result, state, seat);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Session session;
  std::string error;
  if (!notation::parse("p1=4/4 p2=4/4 tube=2L2B turn=p1", &session.state, &error)) {
    std::cerr << "internal: " << error << "\n";
    return 1;
  }
  std::string startPosition;
  std::vector<std::pair<std::string, std::string>> ruleSettings;
  std::string startMode = "don";
  int startSeat = 0;
  int reloads = 2;
  bool asJson = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    long number = 0;
    if (arg == "--help" || arg == "-h") {
      std::cout << "advisor [--position \"<notation>\"] [--seat N] [--reloads N] "
                   "[--mode don|story2|mp] [--json]\n"
                   "        [rule settings, listed below]\n\n";
      printHelp();
      std::cout << "\n" << cli::ruleSettingsHelp();
      return 0;
    }
    if (arg == "--position" || arg == "-p") {
      // A written position is the one value that may look like a flag.
      if (i + 1 >= argc) {
        std::cerr << arg << " needs a position\n";
        return 2;
      }
      startPosition = argv[++i];
    } else if (arg == "--seat") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1, kMaxPlayers, &number)) return 2;
      startSeat = static_cast<int>(number) - 1;
    } else if (arg == "--reloads") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 6, &number)) return 2;
      reloads = static_cast<int>(number);
    } else if (arg == "--mode") {
      if (!cli::nextValue(argc, argv, &i, arg, &startMode)) return 2;
    } else if (arg == "--json") {
      asJson = true;
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
    } else {
      std::cerr << "unrecognised option " << arg << "\n";
      return 2;
    }
  }
  if (!startPosition.empty()) {
    return runOnce(startPosition, startSeat, reloads, startMode, asJson, ruleSettings);
  }
  if (asJson) {
    std::cerr << "--json prints one answer, so it needs --position\n";
    return 2;
  }
  session.ruleSettings = ruleSettings;
  if (!reapplySettings(&session)) return 2;
  session.options.seat = startSeat;
  session.options.reloadBudget = reloads;

  if (BSR_ISATTY(BSR_FILENO(stdin)) != 0) {
    std::cout << "Buckshot Roulette advisor. Type help for commands, quit to leave.\n";
    std::cout << notation::board(session.state) << "\n";
  }

  // A prompt is for a person. When input is piped, leaving it out keeps a
  // captured session readable.
  const bool interactive = BSR_ISATTY(BSR_FILENO(stdin)) != 0;
  std::string line;
  while (true) {
    if (interactive) std::cout << "> " << std::flush;
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      break;
    }
    std::vector<std::string> words = tokenize(line);
    if (words.empty()) words.emplace_back("advise");
    std::string command = words[0];
    std::transform(command.begin(), command.end(), command.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (command == "quit" || command == "exit") break;
    if (command == "help") {
      printHelp();
      continue;
    }
    if (command == "state") {
      std::cout << notation::print(session.state) << "\n";
      continue;
    }
    if (command == "board") {
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "undo") {
      if (session.history.empty()) {
        std::cout << "Nothing to undo.\n";
      } else {
        session.state = session.history.back();
        session.history.pop_back();
        std::cout << notation::board(session.state);
      }
      continue;
    }
    if (command == "set" || command == "pos") {
      const std::size_t pos = line.find(words[0]) + words[0].size();
      GameState parsed;
      if (!notation::parse(line.substr(pos), &parsed, &error)) {
        std::cout << error << "\n";
        continue;
      }
      session.history.push_back(session.state);
      session.state = parsed;
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "seat" && words.size() >= 2) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat) || seat >= session.state.playerCount) {
        std::cout << "Name a seat at this table, as in seat p1.\n";
        continue;
      }
      session.options.seat = seat;
      std::cout << "Advising seat p" << (seat + 1) << ".\n";
      continue;
    }
    if (command == "reloads" && words.size() >= 2) {
      long budget = 0;
      if (!cli::parseWholeNumber(words[1], 0, 6, &budget)) {
        std::cout << "Give a number of reloads between 0 and 6.\n";
        continue;
      }
      session.options.reloadBudget = static_cast<int>(budget);
      std::cout << "Looking through " << session.options.reloadBudget << " reloads.\n";
      continue;
    }
    if (command == "mode" && words.size() >= 2) {
      const std::string mode = words[1];
      if (mode == "don") {
        session.config = RuleConfig::doubleOrNothing(session.state.players[0].maxHp);
      } else if (mode.rfind("story", 0) == 0) {
        long round = 2;
        if (mode.size() > 5 && !cli::parseWholeNumber(mode.substr(5), 1, 3, &round)) {
          std::cout << "Modes: don, story1, story2, story3, mp.\n";
          continue;
        }
        session.config = RuleConfig::storyRound(static_cast<int>(round));
      } else if (mode == "mp" || mode == "multiplayer") {
        session.config =
            RuleConfig::multiplayer(session.state.playerCount, session.state.players[0].maxHp);
        session.options.opponent = OpponentModel::Paranoid;
      } else {
        std::cout << "Modes: don, story1, story2, story3, mp.\n";
        continue;
      }
      reapplySettings(&session);
      std::cout << session.config.describe() << "\n";
      continue;
    }
    if (command == "rule" && words.size() >= 3) {
      const std::string flag = words[1].rfind("--", 0) == 0 ? words[1] : "--" + words[1];
      std::string settingError;
      RuleConfig probe = session.config;
      if (!cli::applyRuleSetting(flag, words[2], &probe, &settingError)) {
        std::cout << settingError << "\n";
        continue;
      }
      session.ruleSettings.emplace_back(flag, words[2]);
      session.config = probe;
      std::cout << session.config.describe() << "\n";
      continue;
    }
    if (command == "rule" || command == "rules") {
      std::cout << cli::ruleSettingsHelp() << "\n" << session.config.describe() << "\n";
      continue;
    }
    if (command == "load" && words.size() >= 2) {
      GameState next = session.state;
      std::string spec = "p1=1/1 p2=1/1 tube=" + words[1];
      GameState probe;
      if (!notation::parse(spec, &probe, &error)) {
        std::cout << error << "\n";
        continue;
      }
      next.tube = Tube{};
      next.tube.live = probe.tube.live;
      next.tube.blank = probe.tube.blank;
      if (session.config.reloadClearsCuffs) {
        for (int seat = 0; seat < next.playerCount; ++seat) {
          next.players[seat].cuffed = false;
          next.players[seat].skipConsumed = false;
        }
      }
      session.history.push_back(session.state);
      session.state = next;
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "hp" && words.size() >= 3) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in hp p1 3.\n";
        continue;
      }
      long parsed = 0;
      if (!cli::parseWholeNumber(words[2], 0, 64, &parsed)) {
        std::cout << "Give a number of charges.\n";
        continue;
      }
      const int charges = static_cast<int>(parsed);
      PlayerState& player = session.state.players[seat];
      if (charges > player.maxHp) {
        std::cout << "That seat holds at most " << static_cast<int>(player.maxHp) << " charges.\n";
        continue;
      }
      session.history.push_back(session.state);
      player.hp = static_cast<std::uint8_t>(charges);
      std::cout << notation::board(session.state);
      continue;
    }
    if ((command == "give" || command == "take") && words.size() >= 3) {
      int seat = 0;
      Item item;
      if (!parseSeat(words[1], session.state, &seat) || !itemFromToken(words[2], &item)) {
        std::cout << "Say which seat and which item, as in give p1 saw.\n";
        continue;
      }
      session.history.push_back(session.state);
      std::uint8_t& count = session.state.players[seat].items[itemIndex(item)];
      if (command == "give") {
        ++count;
      } else if (count > 0) {
        --count;
      }
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "turn" && words.size() >= 2) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in turn p2.\n";
        continue;
      }
      session.history.push_back(session.state);
      session.state.current = static_cast<std::uint8_t>(seat);
      session.state.cuffUsedThisTurn = false;
      std::cout << notation::board(session.state);
      continue;
    }
    if ((command == "cuff" || command == "uncuff") && words.size() >= 2) {
      int seat = 0;
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in cuff p2.\n";
        continue;
      }
      session.history.push_back(session.state);
      session.state.players[seat].cuffed = command == "cuff";
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "saw" || command == "invert") {
      session.history.push_back(session.state);
      if (command == "saw") {
        session.state.tube.sawed = true;
      } else {
        session.state.tube.invertChamber();
      }
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "shot" && words.size() >= 3) {
      int target = 0;
      Shell shell;
      if (!parseSeat(words[1], session.state, &target) || !parseShell(words[2], &shell)) {
        std::cout << "Say who was shot and what came out, as in shot self live.\n";
        continue;
      }
      if (applyWithOutcome(&session, Action::shoot(target), true, shell)) {
        std::cout << notation::board(session.state);
      }
      continue;
    }
    if ((command == "eject" || command == "beer") && words.size() >= 2) {
      Shell shell;
      if (!parseShell(words[1], &shell)) {
        std::cout << "Say what was ejected, as in eject blank.\n";
        continue;
      }
      if (applyWithOutcome(&session, Action::use(Item::Beer), true, shell)) {
        std::cout << notation::board(session.state);
      }
      continue;
    }
    if (command == "mg" && words.size() >= 2) {
      Shell shell;
      if (!parseShell(words[1], &shell)) {
        std::cout << "Say what it showed, as in mg live.\n";
        continue;
      }
      if (session.state.tube.empty()) {
        std::cout << "The tube is empty.\n";
        continue;
      }
      const int seat = session.state.current;
      std::string why;
      if (!chamberCanFire(session.state.tube, shell, &why)) {
        std::cout << why;
        continue;
      }
      GameState probe = session.state;
      if (probe.tube.chamberInverted) {
        const Shell drawn = shell == Shell::Live ? Shell::Blank : Shell::Live;
        probe.tube.resolveChamberDraw(drawn, static_cast<std::uint8_t>(1u << seat));
      } else {
        probe.tube.resolve(0, shell, static_cast<std::uint8_t>(1u << seat));
      }
      if (!tubeStillFits(probe.tube)) {
        std::cout << contradictionMessage(shell);
        continue;
      }
      session.history.push_back(session.state);
      session.state = probe;
      std::uint8_t& count = session.state.players[seat].items[itemIndex(Item::MagnifyingGlass)];
      if (count > 0) --count;
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "phone" && words.size() >= 3) {
      long parsed = 0;
      const bool number = cli::parseWholeNumber(words[1], 0, 64, &parsed);
      const int position = number ? static_cast<int>(parsed) : 0;
      Shell shell;
      if (!number || position < 2 || position > session.state.tube.size() ||
          !parseShell(words[2], &shell)) {
        std::cout << "Say which shell and what it is, as in phone 3 blank. A burner phone "
                     "never names the chamber, so the number starts at 2.\n";
        continue;
      }
      const int seat = session.state.current;
      const Shell already = session.state.tube.truth[position - 1];
      if (already != Shell::Unknown && already != shell) {
        std::cout << "Shell " << position << " is already recorded as "
                  << (already == Shell::Live ? "live" : "blank") << ".\n";
        continue;
      }
      GameState probe = session.state;
      probe.tube.resolve(position - 1, shell, static_cast<std::uint8_t>(1u << seat));
      if (!tubeStillFits(probe.tube)) {
        std::cout << contradictionMessage(shell);
        continue;
      }
      session.history.push_back(session.state);
      session.state = probe;
      std::uint8_t& count = session.state.players[seat].items[itemIndex(Item::BurnerPhone)];
      if (count > 0) --count;
      std::cout << notation::board(session.state);
      continue;
    }
    if (command == "use" && words.size() >= 2) {
      Item item;
      if (!itemFromToken(words[1], &item)) {
        std::cout << "No item is called " << words[1] << ".\n";
        continue;
      }
      Action action = Action::use(item);
      if (item == Item::Adrenaline) {
        // Adrenaline is two decisions: whose item, and where the stolen item
        // points. Without both it would silently steal whatever the default is.
        int from = 0;
        Item stolen;
        if (words.size() < 4 || !parseSeat(words[2], session.state, &from) ||
            !itemFromToken(words[3], &stolen)) {
          std::cout << "Say whose item and which one, as in use adr p2 saw.\n";
          continue;
        }
        if (stolen == Item::Adrenaline) {
          std::cout << "Adrenaline cannot take another adrenaline.\n";
          continue;
        }
        if (session.state.players[from].items[itemIndex(stolen)] == 0) {
          std::cout << "p" << (from + 1) << " is not holding a " << itemName(stolen) << ".\n";
          continue;
        }
        int victim = from;
        if (itemNeedsTarget(stolen)) {
          if (words.size() < 5 || !parseSeat(words[4], session.state, &victim)) {
            std::cout << "That stolen item needs a target, as in use adr p2 cuff p3.\n";
            continue;
          }
        }
        action = Action::steal(from, stolen, victim);
      } else if (itemNeedsTarget(item)) {
        int seat = 0;
        if (words.size() < 3 || !parseSeat(words[2], session.state, &seat)) {
          std::cout << "That item needs a target, as in use cuff p2.\n";
          continue;
        }
        action.target = static_cast<std::uint8_t>(seat);
      }
      // Items whose result is a matter of chance are recorded through the
      // command that names the result, so that the advisor tracks what actually
      // happened rather than the likeliest branch.
      const Item effect = item == Item::Adrenaline ? action.stolen : item;
      if (effect == Item::MagnifyingGlass || effect == Item::Beer || effect == Item::BurnerPhone) {
        std::cout << "Say what it showed instead: mg live, eject blank, or phone 3 live.\n";
        continue;
      }
      if (effect == Item::ExpiredMedicine) {
        const std::string outcome = words.size() >= 3 ? words.back() : "";
        if (outcome != "ok" && outcome != "bad") {
          std::cout << "Expired medicine is a toss up, so say how it went: use med ok, or "
                       "use med bad.\n";
          continue;
        }
        if (!applyMedicine(&session, action, outcome == "ok")) continue;
        std::cout << notation::board(session.state);
        continue;
      }
      if (applyWithOutcome(&session, action, false, Shell::Unknown)) {
        std::cout << notation::board(session.state);
      }
      continue;
    }
    if (command == "advise" || command == "go") {
      if (session.state.needsReload()) {
        std::cout << "The tube is empty. Start the next load, as in load 2L3B.\n";
        continue;
      }
      const std::string outside = itemsOutsideThePool(session.state, session.config);
      if (!outside.empty()) std::cout << outside << "\n";
      const SolveResult result = solve(session.state, session.config, session.options);
      printRanking(result, session.state, session.options.seat);
      continue;
    }
    std::cout << "I do not know the command " << words[0] << ". Type help.\n";
  }
  return 0;
}
