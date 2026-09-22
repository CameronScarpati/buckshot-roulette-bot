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
};

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> words;
  std::istringstream stream(line);
  std::string word;
  while (stream >> word) words.push_back(word);
  return words;
}

bool parseSeat(const std::string& text, const GameState& state, int* seat) {
  if (text.size() >= 2 && (text[0] == 'p' || text[0] == 'P')) {
    const int value = std::atoi(text.c_str() + 1);
    if (value >= 1 && value <= state.playerCount) {
      *seat = value - 1;
      return true;
    }
  }
  if (text == "self" || text == "me") {
    *seat = state.current;
    return true;
  }
  return false;
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
  if (haveShell) {
    if (session->state.tube.empty()) {
      std::cout << "The tube is empty. Use load to start the next one.\n";
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
    std::cout << (best ? "  * " : "    ") << std::left << std::setw(34)
              << entry.action.describe(result.mover) << std::right << std::fixed
              << std::setprecision(4) << entry.value;
    if (!best) {
      std::cout << "   (" << std::showpos << std::setprecision(4) << (entry.value - top)
                << std::noshowpos << ")";
    }
    std::cout << "\n";
  }
  const std::vector<Action> best = result.bestActions();
  if (best.size() > 1) {
    std::cout << "  " << best.size() << " moves tie at the top; any of them is optimal.\n";
  }
  if (result.truncated) {
    std::cout << "  Note: the search hit its reload budget in some lines, so those were "
                 "valued by charges in hand.\n";
  }
  std::cout << "  " << result.assumptions << "\n";
  std::cout << "  " << result.nodes << " states examined.\n\n";
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
    phone <k> live|blank  a burner phone named shell k (1 is the chamber)
    use <item> [p<N>]     the seat to move used an item with no chance outcome

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
            bool asJson) {
  GameState state;
  std::string error;
  if (!notation::parse(position, &state, &error)) {
    std::cerr << error << "\n";
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
    config = RuleConfig::storyRound(mode.size() > 5 ? std::atoi(mode.c_str() + 5) : 2);
  }
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
  std::string startMode = "don";
  int startSeat = 0;
  int reloads = 2;
  bool asJson = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (arg == "--help" || arg == "-h") {
      std::cout << "advisor [--position \"<notation>\"] [--seat N] [--reloads N] "
                   "[--mode don|story2|mp] [--json]\n\n";
      printHelp();
      return 0;
    }
    if (arg == "--position" || arg == "-p") {
      startPosition = next();
    } else if (arg == "--seat") {
      startSeat = std::max(0, std::atoi(next().c_str()) - 1);
    } else if (arg == "--reloads") {
      reloads = std::atoi(next().c_str());
    } else if (arg == "--mode") {
      startMode = next();
    } else if (arg == "--json") {
      asJson = true;
    }
  }
  if (!startPosition.empty()) {
    return runOnce(startPosition, startSeat, reloads, startMode, asJson);
  }
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
      if (!parseSeat(words[1], session.state, &seat)) {
        std::cout << "Name a seat, as in seat p1.\n";
        continue;
      }
      session.options.seat = seat;
      std::cout << "Advising seat p" << (seat + 1) << ".\n";
      continue;
    }
    if (command == "reloads" && words.size() >= 2) {
      session.options.reloadBudget = std::max(0, std::atoi(words[1].c_str()));
      std::cout << "Looking through " << session.options.reloadBudget << " reloads.\n";
      continue;
    }
    if (command == "mode" && words.size() >= 2) {
      const std::string mode = words[1];
      if (mode == "don") {
        session.config = RuleConfig::doubleOrNothing(session.state.players[0].maxHp);
      } else if (mode.rfind("story", 0) == 0) {
        const int round = mode.size() > 5 ? std::atoi(mode.c_str() + 5) : 2;
        session.config = RuleConfig::storyRound(round);
      } else if (mode == "mp" || mode == "multiplayer") {
        session.config =
            RuleConfig::multiplayer(session.state.playerCount, session.state.players[0].maxHp);
        session.options.opponent = OpponentModel::Paranoid;
      } else {
        std::cout << "Modes: don, story1, story2, story3, mp.\n";
        continue;
      }
      std::cout << session.config.describe() << "\n";
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
      const int charges = std::atoi(words[2].c_str());
      PlayerState& player = session.state.players[seat];
      if (charges < 0 || charges > player.maxHp) {
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
      const int position = std::atoi(words[1].c_str());
      Shell shell;
      if (position < 1 || position > session.state.tube.size() || !parseShell(words[2], &shell)) {
        std::cout << "Say which shell and what it is, as in phone 3 blank (1 is the chamber).\n";
        continue;
      }
      const int seat = session.state.current;
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
      if (itemNeedsTarget(item)) {
        int seat = 0;
        if (words.size() < 3 || !parseSeat(words[2], session.state, &seat)) {
          std::cout << "That item needs a target, as in use cuff p2.\n";
          continue;
        }
        action.target = static_cast<std::uint8_t>(seat);
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
      const SolveResult result = solve(session.state, session.config, session.options);
      printRanking(result, session.state, session.options.seat);
      continue;
    }
    std::cout << "I do not know the command " << words[0] << ". Type help.\n";
  }
  return 0;
}
