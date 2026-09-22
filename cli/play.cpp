/// Play a round against the solver. Every shell is drawn from a seeded
/// generator, so a whole game replays exactly from its seed.

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli/Args.h"
#include "engine/Notation.h"
#include "engine/Rules.h"
#include "solver/Solver.h"

namespace {

using namespace bsr;

struct Options {
  unsigned seed = 1;
  int charges = 4;
  int players = 2;
  int you = 0;
  int reloadBudget = 2;
  bool quiet = false;
};

const Outcome& sample(const std::vector<Outcome>& outcomes, std::mt19937* rng) {
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  double roll = uniform(*rng);
  for (const Outcome& outcome : outcomes) {
    roll -= outcome.probability;
    if (roll <= 0.0) return outcome;
  }
  return outcomes.back();
}

void dealItems(GameState* state, const RuleConfig& config, std::mt19937* rng) {
  if (config.itemPool.empty() || config.itemsPerLoad == 0) return;
  std::uniform_int_distribution<std::size_t> pick(0, config.itemPool.size() - 1);
  for (int seat = 0; seat < state->playerCount; ++seat) {
    PlayerState& player = state->players[seat];
    if (!player.alive()) continue;
    for (int i = 0; i < config.itemsPerLoad; ++i) {
      if (player.itemCount() >= config.itemLimit) break;
      ++player.items[itemIndex(config.itemPool[pick(*rng)])];
    }
  }
}

void reload(GameState* state, const RuleConfig& config, std::mt19937* rng, bool announce) {
  const auto table = rules::loadDistribution(config);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  double roll = uniform(*rng);
  std::uint8_t live = 1;
  std::uint8_t blank = 1;
  for (const auto& entry : table) {
    roll -= std::get<2>(entry);
    live = std::get<0>(entry);
    blank = std::get<1>(entry);
    if (roll <= 0.0) break;
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
  dealItems(state, config, rng);
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

/// Run rounds with nobody watching and report how often seat 1 survives.
int runBatch(int rounds, unsigned seed, int charges, int players, int reloadBudget,
             bool solverOnBothSides) {
  RuleConfig config = players > 2 ? RuleConfig::multiplayer(static_cast<std::uint8_t>(players),
                                                            static_cast<std::uint8_t>(charges))
                                  : RuleConfig::doubleOrNothing(static_cast<std::uint8_t>(charges));
  int wins = 0;
  int unfinished = 0;
  long long moves = 0;
  long long nodes = 0;
  // A round of this game can run a long way when both seats keep healing, so a
  // cap keeps a batch bounded. Rounds that hit it are reported rather than
  // counted as a result either way.
  constexpr int kMoveCap = 400;
  for (int round = 0; round < rounds; ++round) {
    std::mt19937 rng(seed + static_cast<unsigned>(round));
    GameState state;
    state.playerCount = static_cast<std::uint8_t>(players);
    for (int seat = 0; seat < players; ++seat) {
      state.players[seat].hp = static_cast<std::uint8_t>(charges);
      state.players[seat].maxHp = static_cast<std::uint8_t>(charges);
    }
    state.current = 0;
    reload(&state, config, &rng, false);
    int guard = 0;
    while (!state.roundOver() && guard++ < kMoveCap) {
      while (rules::applyPendingSkip(&state)) {
        if (state.roundOver()) break;
      }
      if (state.roundOver()) break;
      if (state.needsReload()) {
        reload(&state, config, &rng, false);
        continue;
      }
      Action action;
      if (state.current == 0 || solverOnBothSides) {
        SolveOptions options;
        options.seat = state.current;
        options.reloadBudget = reloadBudget;
        options.opponent = players > 2 ? OpponentModel::Paranoid : OpponentModel::Optimal;
        const SolveResult result = solve(state, config, options);
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
  std::cout << "rounds " << rounds << ", seed " << seed << ", " << charges << " charges, "
            << players << " seats, reload budget " << reloadBudget << "\n";
  std::cout << "seat 1 "
            << (solverOnBothSides ? "(solver, against the solver)"
                                  : "(solver, against the heuristic)")
            << " survived " << wins << " of " << rounds << " rounds, " << std::fixed
            << std::setprecision(1)
            << (100.0 * static_cast<double>(wins) / static_cast<double>(rounds)) << " percent\n";
  std::cout << moves << " moves played, " << nodes << " states examined\n";
  if (unfinished > 0) {
    std::cout << unfinished << " rounds reached the " << kMoveCap
              << " move cap without a winner and are counted as losses\n";
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

}  // namespace

int main(int argc, char** argv) {
  Options options;
  int batchRounds = 0;
  bool bothSolvers = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    long number = 0;
    if (arg == "--seed") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 4294967295L, &number)) return 2;
      options.seed = static_cast<unsigned>(number);
    } else if (arg == "--charges") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1, 8, &number)) return 2;
      options.charges = static_cast<int>(number);
    } else if (arg == "--players") {
      if (!cli::nextNumber(argc, argv, &i, arg, 2, kMaxPlayers, &number)) return 2;
      options.players = static_cast<int>(number);
    } else if (arg == "--reloads") {
      if (!cli::nextNumber(argc, argv, &i, arg, 0, 6, &number)) return 2;
      options.reloadBudget = static_cast<int>(number);
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else if (arg == "--selfplay" || arg == "--baseline") {
      if (!cli::nextNumber(argc, argv, &i, arg, 1, 100000, &number)) return 2;
      batchRounds = static_cast<int>(number);
      bothSolvers = arg == "--selfplay";
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "play [--seed N] [--charges N] [--players N] [--reloads N]\n"
                   "     [--selfplay ROUNDS | --baseline ROUNDS]\n\n"
                   "With no batch flag, play one round yourself against the solver. The\n"
                   "same seed replays the same shells. --selfplay runs the solver against\n"
                   "itself, --baseline runs it against a heuristic opponent, and both\n"
                   "report how often seat 1 survives.\n";
      return 0;
    }
  }
  options.players = std::max(2, std::min(options.players, static_cast<int>(kMaxPlayers)));
  options.charges = std::max(1, std::min(options.charges, 8));

  if (batchRounds > 0) {
    return runBatch(batchRounds, options.seed, options.charges, options.players,
                    options.reloadBudget, bothSolvers);
  }

  RuleConfig config = options.players > 2
                          ? RuleConfig::multiplayer(static_cast<std::uint8_t>(options.players),
                                                    static_cast<std::uint8_t>(options.charges))
                          : RuleConfig::doubleOrNothing(static_cast<std::uint8_t>(options.charges));

  GameState state;
  state.playerCount = static_cast<std::uint8_t>(options.players);
  for (int seat = 0; seat < options.players; ++seat) {
    state.players[seat].hp = static_cast<std::uint8_t>(options.charges);
    state.players[seat].maxHp = static_cast<std::uint8_t>(options.charges);
  }
  state.current = 0;

  std::mt19937 rng(options.seed);
  std::cout << "Buckshot Roulette, seed " << options.seed << ". You are p1.\n";
  reload(&state, config, &rng, true);

  while (!state.roundOver()) {
    while (rules::applyPendingSkip(&state)) {
      std::cout << "Handcuffs: that seat loses a turn.\n";
      if (state.roundOver()) break;
    }
    if (state.roundOver()) break;
    if (state.needsReload()) {
      reload(&state, config, &rng, true);
      continue;
    }

    std::cout << "\n" << notation::board(state);
    const std::vector<Action> actions = rules::legalActions(state, config);
    if (actions.empty()) break;

    Action action = actions.front();
    if (static_cast<int>(state.current) == options.you) {
      const int choice = chooseFromMenu(actions, state);
      if (choice < 0) {
        std::cout << "Leaving the table.\n";
        return 0;
      }
      action = actions[static_cast<std::size_t>(choice)];
    } else {
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

    const std::vector<Outcome> outcomes = rules::apply(state, action, config);
    const Outcome& chosen = sample(outcomes, &rng);
    if (chosen.shellFired) {
      std::cout << "  The shell was " << (chosen.shellType == Shell::Live ? "LIVE" : "blank")
                << ".\n";
    }
    state = chosen.state;
  }

  const int winner = state.soleSurvivor();
  std::cout << "\n" << notation::board(state);
  if (winner < 0) {
    std::cout << "Nobody is left standing.\n";
  } else if (winner == options.you) {
    std::cout << "You win the round.\n";
  } else {
    std::cout << "p" << (winner + 1) << " wins the round.\n";
  }
  return 0;
}
