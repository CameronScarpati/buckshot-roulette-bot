#include "BotPlayer.h"
#include "Exceptions.h"
#include "Items/Item.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <unordered_map>

const std::chrono::milliseconds BotPlayer::TIME_LIMIT;
constexpr int BotPlayer::MAX_SEARCH_DEPTH;
constexpr int BotPlayer::MIN_SEARCH_DEPTH;
constexpr float BotPlayer::EPSILON;

float BotPlayer::valueOfItem(const Item *item) {
  if (!item)
    return 0.0f;

  std::string_view name = item->getName();
  if (name == "Beer")
    return BEER_VALUE;
  else if (name == "Cigarette")
    return CIGARETTE_VALUE;
  else if (name == "Handcuffs")
    return HANDCUFFS_VALUE;
  else if (name == "Magnifying Glass")
    return MAGNIFYING_GLASS_VALUE;
  else if (name == "Handsaw")
    return HANDSAW_VALUE;

  return 0.0f;
}

float BotPlayer::evaluateState(SimulatedGame *state) {
  // Terminal conditions: if one player's HP is zero, assign an extreme score.
  if (!state->getPlayerOne()->isAlive())
    return TERMINAL_LOSS_SCORE;
  if (!state->getPlayerTwo()->isAlive())
    return TERMINAL_WIN_SCORE;

  // All scoring is consistently from playerOne (bot) perspective.

  // 1. Health Differential: normalized difference.
  float healthScore =
      HEALTH_WEIGHT * ((static_cast<float>(state->getPlayerOne()->getHealth()) /
                        static_cast<float>(getMaxHealth())) -
                       (static_cast<float>(state->getPlayerTwo()->getHealth()) /
                        static_cast<float>(getMaxHealth())));

  // 2. Item Value Comparison: total held item value for each player.
  float p1ItemValue = 0.0f;
  float p2ItemValue = 0.0f;
  try {
    for (const auto *item : state->getPlayerOne()->getItemsView())
      p1ItemValue += valueOfItem(item);
    for (const auto *item : state->getPlayerTwo()->getItemsView())
      p2ItemValue += valueOfItem(item);
  } catch (const std::exception &) {
    // Continue with default scores on error
  }
  float itemScore = ITEM_WEIGHT * (p1ItemValue - p2ItemValue);

  // 3. Status Effects: Handcuffs, Handsaw, and Magnifying Glass statuses.
  float statusScore = 0.0f;
  // Opponent cuffed is good for us; us being cuffed is bad
  if (state->getPlayerTwo()->areHandcuffsApplied())
    statusScore += HANDCUFF_WEIGHT;
  if (state->getPlayerOne()->areHandcuffsApplied())
    statusScore -= HANDCUFF_WEIGHT;
  // Us knowing the next shell is good; opponent knowing is bad
  if (state->getPlayerOne()->isNextShellRevealed())
    statusScore += MAGNIFYING_GLASS_WEIGHT;
  if (state->getPlayerTwo()->isNextShellRevealed())
    statusScore -= MAGNIFYING_GLASS_WEIGHT;
  // Saw active on our turn means we benefit; on opponent's turn they benefit
  if (state->isPlayerOneTurnNow() && state->getShotgun()->getSawUsed())
    statusScore += HANDSAW_WEIGHT;
  if (!state->isPlayerOneTurnNow() && state->getShotgun()->getSawUsed())
    statusScore -= HANDSAW_WEIGHT;

  // 4. Turn Advantage: bonus if it is our turn.
  float turnScore = (state->isPlayerOneTurnNow() ? TURN_WEIGHT : -TURN_WEIGHT);

  // 5. Shell Distribution Favorability: extreme distributions (far from 50/50)
  // favor the active player since they can choose shoot-self (if mostly blanks)
  // or shoot-opponent (if mostly lives) optimally.
  float shellScore = 0.0f;
  int totalShells = state->getShotgun()->getTotalShellCount();
  if (totalShells > 0) {
    float pLive = state->getShotgun()->getLiveShellProbability();
    float extremity = std::abs(pLive - 0.5f) * 2.0f; // 0 at 50/50, 1 at 100%
    shellScore = SHELL_DISTRIBUTION_WEIGHT * extremity;
    if (!state->isPlayerOneTurnNow())
      shellScore = -shellScore;
  }

  // 6. Item Synergy: complementary item combos are worth more than their parts.
  float synergyScore = 0.0f;
  bool p1HasCuffs = state->getPlayerOne()->hasItem("Handcuffs");
  bool p1HasSaw = state->getPlayerOne()->hasItem("Handsaw");
  bool p1HasMag = state->getPlayerOne()->hasItem("Magnifying Glass");
  bool p1HasBeer = state->getPlayerOne()->hasItem("Beer");
  bool p2HasCuffs = state->getPlayerTwo()->hasItem("Handcuffs");
  bool p2HasSaw = state->getPlayerTwo()->hasItem("Handsaw");
  bool p2HasMag = state->getPlayerTwo()->hasItem("Magnifying Glass");
  bool p2HasBeer = state->getPlayerTwo()->hasItem("Beer");

  float p1Synergy = 0.0f;
  float p2Synergy = 0.0f;
  // Handcuffs + Handsaw: lock opponent down and deal double damage
  if (p1HasCuffs && p1HasSaw) p1Synergy += 2.0f;
  if (p2HasCuffs && p2HasSaw) p2Synergy += 2.0f;
  // Handcuffs + Magnifying Glass: guaranteed optimal play with no retaliation
  if (p1HasCuffs && p1HasMag) p1Synergy += 1.5f;
  if (p2HasCuffs && p2HasMag) p2Synergy += 1.5f;
  // Magnifying Glass + Handsaw: see live shell then double it
  if (p1HasMag && p1HasSaw) p1Synergy += 1.0f;
  if (p2HasMag && p2HasSaw) p2Synergy += 1.0f;
  // Beer + Magnifying Glass: manipulate shells with information
  if (p1HasBeer && p1HasMag) p1Synergy += 0.5f;
  if (p2HasBeer && p2HasMag) p2Synergy += 0.5f;
  synergyScore = ITEM_SYNERGY_WEIGHT * (p1Synergy - p2Synergy);

  // Total evaluation: sum of all weighted components.
  return healthScore + itemScore + statusScore + turnScore + shellScore + synergyScore;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());

  // Determine current and opponent players based on whose turn it is.
  auto *currentPlayer = (state->isPlayerOneTurnNow()) ? state->getPlayerOne()
                                                      : state->getPlayerTwo();
  auto *otherPlayer = (!state->isPlayerOneTurnNow()) ? state->getPlayerOne()
                                                     : state->getPlayerTwo();

  switch (action) {
  case Action::SHOOT_SELF:
    currentPlayer->resetKnownNextShell();
    if (shell == ShellType::LIVE_SHELL) {
      currentPlayer->loseHealth(simShotgun->getSawUsed());
      simShotgun->resetSawUsed();
      simShotgun->simulateLiveShell();

      // If the opponent is handcuffed, remove them and keep turn.
      if (otherPlayer->areHandcuffsApplied()) {
        otherPlayer->removeHandcuffs();
        return state->isPlayerOneTurnNow();
      } else {
        currentPlayer->resetHandcuffUsage();
        return !state->isPlayerOneTurnNow();
      }
    } else {
      simShotgun->resetSawUsed();
      simShotgun->simulateBlankShell();
      return state->isPlayerOneTurnNow(); // Blank shell grants extra turn.
    }

  case Action::SHOOT_OPPONENT:
    currentPlayer->resetKnownNextShell();
    if (shell == ShellType::LIVE_SHELL) {
      otherPlayer->loseHealth(simShotgun->getSawUsed());
      simShotgun->resetSawUsed();
      simShotgun->simulateLiveShell();
    } else {
      simShotgun->resetSawUsed();
      simShotgun->simulateBlankShell();
    }

    // If the opponent is handcuffed, remove them and keep turn.
    if (otherPlayer->areHandcuffsApplied()) {
      otherPlayer->removeHandcuffs();
      return state->isPlayerOneTurnNow();
    } else {
      currentPlayer->resetHandcuffUsage();
      return !state->isPlayerOneTurnNow();
    }

  case Action::USE_MAGNIFYING_GLASS:
    currentPlayer->setKnownNextShell(shell);
    currentPlayer->removeItemByName("Magnifying Glass");
    return state->isPlayerOneTurnNow();

  case Action::SMOKE_CIGARETTE:
    currentPlayer->smokeCigarette();
    currentPlayer->removeItemByName("Cigarette");
    return state->isPlayerOneTurnNow();

  case Action::DRINK_BEER:
    currentPlayer->resetKnownNextShell();
    currentPlayer->removeItemByName("Beer");
    return state->isPlayerOneTurnNow();

  case Action::USE_HANDSAW:
    simShotgun->useHandsaw();
    currentPlayer->removeItemByName("Handsaw");
    return state->isPlayerOneTurnNow();

  case Action::USE_HANDCUFFS:
    otherPlayer->applyHandcuffs();
    currentPlayer->useHandcuffsThisTurn();
    currentPlayer->removeItemByName("Handcuffs");
    return state->isPlayerOneTurnNow();

  default:
    return !state->isPlayerOneTurnNow();
  }
}

std::unique_ptr<SimulatedGame>
BotPlayer::simulateLiveAction(SimulatedGame *state, Action action) {
  auto nextState = std::make_unique<SimulatedGame>(*state);
  bool newTurn = performAction(action, nextState.get(), ShellType::LIVE_SHELL);
  nextState->changePlayerTurn(newTurn);
  return nextState;
}

std::unique_ptr<SimulatedGame>
BotPlayer::simulateBlankAction(SimulatedGame *state, Action action) {
  auto nextState = std::make_unique<SimulatedGame>(*state);
  bool newTurn = performAction(action, nextState.get(), ShellType::BLANK_SHELL);
  nextState->changePlayerTurn(newTurn);
  return nextState;
}

std::unique_ptr<SimulatedGame>
BotPlayer::simulateNonProbabilisticAction(SimulatedGame *state, Action action) {
  auto nextState = std::make_unique<SimulatedGame>(*state);
  // For non-probabilistic actions like using items, a shell type doesn't matter
  bool newTurn = performAction(action, nextState.get(), ShellType::LIVE_SHELL);
  nextState->changePlayerTurn(newTurn);
  return nextState;
}

std::pair<std::unique_ptr<SimulatedGame>, std::unique_ptr<SimulatedGame>>
BotPlayer::simulateAction(SimulatedGame *state, Action action) {
  if (action == Action::USE_MAGNIFYING_GLASS) {
    // Simulate both outcomes for shell revelation.
    auto liveReveal = std::make_unique<SimulatedGame>(*state);
    auto *playerLive = (liveReveal->isPlayerOneTurnNow())
                           ? liveReveal->getPlayerOne()
                           : liveReveal->getPlayerTwo();
    if (playerLive) {
      playerLive->setKnownNextShell(ShellType::LIVE_SHELL);
      performAction(Action::USE_MAGNIFYING_GLASS, liveReveal.get(),
                    ShellType::LIVE_SHELL);
    }

    auto blankReveal = std::make_unique<SimulatedGame>(*state);
    auto *playerBlank = (blankReveal->isPlayerOneTurnNow())
                            ? blankReveal->getPlayerOne()
                            : blankReveal->getPlayerTwo();
    if (playerBlank) {
      playerBlank->setKnownNextShell(ShellType::BLANK_SHELL);
      performAction(Action::USE_MAGNIFYING_GLASS, blankReveal.get(),
                    ShellType::BLANK_SHELL);
    }

    return {std::move(liveReveal), std::move(blankReveal)};
  } else if (action == Action::DRINK_BEER) {
    // Simulate beer usage for both live and blank shell outcomes.
    auto liveBranch = std::make_unique<SimulatedGame>(*state);
    auto *liveShotgun =
        dynamic_cast<SimulatedShotgun *>(liveBranch->getShotgun());
    if (liveShotgun && liveShotgun->getLiveShellCount() > 0)
      liveShotgun->simulateLiveShell();
    bool turnAfterLive = performAction(Action::DRINK_BEER, liveBranch.get(),
                                       ShellType::LIVE_SHELL);
    liveBranch->changePlayerTurn(turnAfterLive);

    auto blankBranch = std::make_unique<SimulatedGame>(*state);
    auto *blankShotgun =
        dynamic_cast<SimulatedShotgun *>(blankBranch->getShotgun());
    if (blankShotgun && blankShotgun->getBlankShellCount() > 0)
      blankShotgun->simulateBlankShell();
    bool turnAfterBlank = performAction(Action::DRINK_BEER, blankBranch.get(),
                                        ShellType::BLANK_SHELL);
    blankBranch->changePlayerTurn(turnAfterBlank);

    return {std::move(liveBranch), std::move(blankBranch)};
  }

  // For other actions, simulate both shell outcomes
  std::unique_ptr<SimulatedGame> liveState = simulateLiveAction(state, action);
  std::unique_ptr<SimulatedGame> blankState =
      simulateBlankAction(state, action);
  return {std::move(liveState), std::move(blankState)};
}

// Computes the expected value of an action by branching over probabilistic
// shell outcomes.  Deterministic actions (items) need only one branch;
// probabilistic actions (shoot, beer, magnifying glass) require weighting the
// live and blank outcomes by their respective probabilities.
float BotPlayer::expectedValueForAction(SimulatedGame *state, Action action,
                                        int depth) {
  if (depth <= 0)
    return 0.0f;

  // Deterministic actions have a single outcome — no shell randomness involved.
  if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
      action == Action::USE_HANDCUFFS) {
    auto newState = simulateNonProbabilisticAction(state, action);
    float result = expectiMiniMax(newState.get(), depth - 1);
    return result;
  }

  // If the acting player knows the next shell (from magnifying glass),
  // treat shot and beer actions as deterministic using that knowledge.
  auto *actingPlayer = state->isPlayerOneTurnNow() ? state->getPlayerOne()
                                                   : state->getPlayerTwo();
  if (actingPlayer->isNextShellRevealed() &&
      (action == Action::SHOOT_SELF || action == Action::SHOOT_OPPONENT ||
       action == Action::DRINK_BEER)) {
    if (actingPlayer->returnKnownNextShell() == ShellType::LIVE_SHELL) {
      auto liveState = simulateLiveAction(state, action);
      return expectiMiniMax(liveState.get(), depth - 1);
    } else {
      auto blankState = simulateBlankAction(state, action);
      return expectiMiniMax(blankState.get(), depth - 1);
    }
  }

  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  // When shell outcome is certain, only one branch needs evaluation.
  if (std::abs(pLive - 1.0f) < EPSILON) {
    auto liveState = simulateLiveAction(state, action);
    return expectiMiniMax(liveState.get(), depth - 1);
  } else if (std::abs(pBlank - 1.0f) < EPSILON) {
    auto blankState = simulateBlankAction(state, action);
    return expectiMiniMax(blankState.get(), depth - 1);
  }

  // Chance node: branch into both live and blank outcomes, then combine
  // using E[V] = P(live) * V(live) + P(blank) * V(blank).
  auto [liveState, blankState] = simulateAction(state, action);

  float liveVal = 0.0f;
  if (liveState)
    liveVal = expectiMiniMax(liveState.get(), depth - 1);

  float blankVal = 0.0f;
  if (blankState)
    blankVal = expectiMiniMax(blankState.get(), depth - 1);

  return pLive * liveVal + pBlank * blankVal;
}

// Core expectiminimax search with alpha-beta pruning.
// Player 1 (the bot) is the MAX player; Player 2 is the MIN player.
// Chance nodes are handled inside expectedValueForAction, which weights
// outcomes by shell probabilities.
float BotPlayer::expectiMiniMax(
    SimulatedGame *state, int depth, float alpha, float beta,
    std::chrono::steady_clock::time_point startTime) {
  // Bail out early if time budget is exhausted; return static evaluation.
  if (timeExpired(startTime))
    return evaluateState(state);

  // Base case: leaf node — evaluate the position heuristically.
  if (depth == 0 || !state->getPlayerOne() || !state->getPlayerTwo() ||
      !state->getShotgun() || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty())
    return evaluateState(state);

  // Generate and prioritize legal actions to improve pruning efficiency.
  std::vector<Action> actionsToTry;
  try {
    actionsToTry =
        prioritizeStrategicActions(determineFeasibleActions(state), state);
  } catch (const GameException &) {
    return evaluateState(state);
  } catch (const std::exception &) {
    return evaluateState(state);
  }

  // MAX node (Player 1) starts at -inf; MIN node (Player 2) at +inf.
  float bestValue = state->isPlayerOneTurnNow()
                        ? -std::numeric_limits<float>::infinity()
                        : std::numeric_limits<float>::infinity();

  for (auto action : actionsToTry) {
    float value;
    try {
      // Evaluate this action's expected value across chance outcomes.
      value = expectedValueForAction(state, action, depth);
    } catch (const GameException &) {
      continue; // Skip this action if it causes a game exception
    } catch (const std::exception &) {
      continue; // Skip this action if it causes an exception
    }

    if (state->isPlayerOneTurnNow()) {
      // MAX node: keep the highest-valued action.
      bestValue = std::max(bestValue, value);
      alpha = std::max(alpha, bestValue);
      if (beta <= alpha)
        break; // Beta cutoff — MIN has a better option elsewhere.
    } else {
      // MIN node: keep the lowest-valued action.
      bestValue = std::min(bestValue, value);
      beta = std::min(beta, bestValue);
      if (beta <= alpha)
        break; // Alpha cutoff — MAX has a better option elsewhere.
    }

    if (timeExpired(startTime))
      return bestValue;
  }

  return bestValue;
}

BotPlayer::BotPlayer(std::string playerName, int playerHealth)
    : Player(std::move(playerName), playerHealth) {}

BotPlayer::BotPlayer(std::string playerName, int playerHealth,
                     Player *playerOpponent)
    : Player(std::move(playerName), playerHealth, playerOpponent) {}

Action BotPlayer::liveShellAction(Shotgun *currentShotgun) {
  if (!currentShotgun)
    return Action::SHOOT_OPPONENT;

  // Guaranteed kill option.
  if (opponent->getHealth() == 1 ||
      (currentShotgun->getSawUsed() && opponent->getHealth() == 2)) {
    resetKnownNextShell();
    return Action::SHOOT_OPPONENT;
  }

  // If handsaw can create a guaranteed kill, use it first.
  if (hasItem("Handsaw") && !currentShotgun->getSawUsed() &&
      opponent->getHealth() <= 2)
    return Action::USE_HANDSAW;

  // If Handcuffs available, use them to control opponent's turn.
  if (hasItem("Handcuffs") && !hasUsedHandcuffsThisTurn())
    return Action::USE_HANDCUFFS;

  // If a Handsaw is available and the saw hasn't been applied, use it.
  if (hasItem("Handsaw") && !currentShotgun->getSawUsed())
    return Action::USE_HANDSAW;

  // Shoot the opponent - don't waste turns healing when we have a confirmed
  // live shell ready. Press the advantage.
  resetKnownNextShell();
  return Action::SHOOT_OPPONENT;
}

std::vector<Action>
BotPlayer::prioritizeStrategicActions(const std::vector<Action> &actions,
                                      SimulatedGame *state) {
  std::vector<Action> prioritized = actions;

  // Define priority categories
  std::vector<Action> high_priority;
  std::vector<Action> medium_priority;
  std::vector<Action> low_priority;

  auto *actingPlayer = state->isPlayerOneTurnNow() ? state->getPlayerOne()
                                                   : state->getPlayerTwo();

  for (const auto &action : actions) {
    // High priority: known blank shell for self, known live for opponent, or
    // magnifying glass
    if ((action == Action::SHOOT_SELF && actingPlayer->isNextShellRevealed() &&
         actingPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL) ||
        (action == Action::SHOOT_OPPONENT &&
         actingPlayer->isNextShellRevealed() &&
         actingPlayer->returnKnownNextShell() == ShellType::LIVE_SHELL) ||
        action == Action::USE_MAGNIFYING_GLASS) {
      high_priority.push_back(action);
    }
    // Medium priority: handcuffs, handsaw, beer (beer is a key tempo tool
    // for manipulating shell odds)
    else if (action == Action::USE_HANDCUFFS || action == Action::USE_HANDSAW ||
             action == Action::DRINK_BEER) {
      medium_priority.push_back(action);
    }
    // Low priority: other actions
    else
      low_priority.push_back(action);
  }

  // Combine prioritized actions
  prioritized.clear();
  prioritized.insert(prioritized.end(), high_priority.begin(),
                     high_priority.end());
  prioritized.insert(prioritized.end(), medium_priority.begin(),
                     medium_priority.end());
  prioritized.insert(prioritized.end(), low_priority.begin(),
                     low_priority.end());

  return prioritized;
}

Action BotPlayer::chooseAction(Shotgun *currentShotgun) {
  // If the next shell is already revealed, decide based on its type
  if (isNextShellRevealed()) {
    if (returnKnownNextShell() == ShellType::LIVE_SHELL)
      return liveShellAction(currentShotgun);
    else {
      resetKnownNextShell();
      return Action::SHOOT_SELF;
    }
  }

  float pLive = currentShotgun->getLiveShellProbability();
  float pBlank = currentShotgun->getBlankShellProbability();

  // If only blanks remain, shoot self; if only lives, act accordingly
  if (std::abs(pBlank - 1.0f) < EPSILON)
    return Action::SHOOT_SELF;
  else if (std::abs(pLive - 1.0f) < EPSILON)
    return liveShellAction(currentShotgun);

  // Heuristic: use magnifying glass first when the shell outcome is genuinely
  // uncertain.  Skip it when we can already deduce the shell (e.g. only one
  // type remains — handled above — or only one shell left where the
  // probabilities already tell us everything we need).
  if (hasItem("Magnifying Glass") && currentShotgun->getTotalShellCount() > 1)
    return Action::USE_MAGNIFYING_GLASS;

  // Heuristic: use handcuffs before committing to a shot when odds are uncertain.
  // Prevents opponent from retaliating regardless of outcome.
  if (hasItem("Handcuffs") && !hasUsedHandcuffsThisTurn() &&
      pLive > 0.3f && pBlank > 0.3f)
    return Action::USE_HANDCUFFS;

  // Heuristic: use beer when very few shells remain to gain certainty or
  // trigger a reload for fresh items. With <=2 shells at 50/50, beer gives
  // perfect information about the remaining shell(s).
  if (hasItem("Beer") && currentShotgun->getTotalShellCount() <= 2 &&
      pLive > 0.3f && pBlank > 0.3f)
    return Action::DRINK_BEER;

  std::cout << "Live Probability: " << pLive << "\n";
  std::cout << "Blank Probability: " << pBlank << "\n";

  try {
    // Build a starting simulated state
    auto simShotgun = std::make_unique<SimulatedShotgun>(
        currentShotgun->getTotalShellCount(),
        currentShotgun->getLiveShellCount(),
        currentShotgun->getBlankShellCount(), currentShotgun->getSawUsed());

    // Create simulated players using the available constructor with deep copies
    // Use consistent names to ensure proper item management
    auto simP1 = std::make_unique<SimulatedPlayer>("Player1", this->health);
    auto simP2 = std::make_unique<SimulatedPlayer>("Player2",
                                                   this->opponent->getHealth());

    // Copy items manually
    for (const auto &item : getItemsView()) {
      if (item) {
        auto newItem = Item::createByName(item->getName());
        if (newItem)
          simP1->addItem(std::move(newItem));
      }
    }

    for (const auto &item : opponent->getItemsView()) {
      if (item) {
        auto newItem = Item::createByName(item->getName());
        if (newItem)
          simP2->addItem(std::move(newItem));
      }
    }

    if (isNextShellRevealed())
      simP1->setKnownNextShell(returnKnownNextShell());
    if (opponent->isNextShellRevealed())
      simP2->setKnownNextShell(opponent->returnKnownNextShell());
    if (areHandcuffsApplied())
      simP1->applyHandcuffs();
    if (opponent->areHandcuffsApplied())
      simP2->applyHandcuffs();
    if (hasUsedHandcuffsThisTurn())
      simP1->useHandcuffsThisTurn();
    simP1->setOpponent(simP2.get());
    simP2->setOpponent(simP1.get());

    // We'll use a unique_ptr to ensure proper cleanup here
    SimulatedPlayer *p1Ptr = simP1.release();
    SimulatedPlayer *p2Ptr = simP2.release();
    SimulatedShotgun *shotgunPtr = simShotgun.release();

    std::unique_ptr<SimulatedGame> initState =
        std::make_unique<SimulatedGame>(p1Ptr, p2Ptr, shotgunPtr, true);

    // Now ownership of player pointers has been transferred to the
    // SimulatedGame

    float bestValue = -std::numeric_limits<float>::infinity();
    Action bestAction = Action::SHOOT_OPPONENT;

    // Get current time for time-limited search
    auto startTime = std::chrono::steady_clock::now();

    // Determine all possible actions from this state
    std::vector<Action> actionsToTry;
    try {
      actionsToTry = prioritizeStrategicActions(
          determineFeasibleActions(initState.get()), initState.get());
    } catch (const GameException &) {
      // Fallback to a basic set of actions if there's a game error
      actionsToTry = {Action::SHOOT_OPPONENT, Action::SHOOT_SELF};
    } catch (const std::exception &) {
      // Fallback to a basic set of actions if there's an error
      actionsToTry = {Action::SHOOT_OPPONENT, Action::SHOOT_SELF};
    }

    // Iterative deepening: search at increasing depths starting from
    // MIN_SEARCH_DEPTH, refining the best action at each level until the
    // time budget is exhausted or MAX_SEARCH_DEPTH is reached.
    int maxDepthReached = 0;
    for (int depth = MIN_SEARCH_DEPTH; depth <= MAX_SEARCH_DEPTH; depth++) {
      bool timeOut = false;
      std::unordered_map<Action, float> actionValues;

      // Evaluate each action using expectedValueForAction (handles known
      // shells, deterministic items, and probabilistic branches uniformly)
      for (auto action : actionsToTry) {
        float actionValue;

        std::cout << "Evaluating action: " << static_cast<int>(action)
                  << " at depth " << depth << "\n";

        try {
          actionValue =
              expectedValueForAction(initState.get(), action, depth);
        } catch (const GameException &) {
          continue;
        } catch (const std::exception &) {
          continue;
        }

        // Check for timeout after evaluation
        if (timeExpired(startTime)) {
          timeOut = true;
          break;
        }

        actionValues[action] = actionValue;
        std::cout << "Action " << static_cast<int>(action)
                  << " has value: " << actionValue << " at depth " << depth
                  << "\n";
      }

      // Update the best action if we completed this depth
      if (!timeOut || !actionValues.empty()) {
        maxDepthReached = depth;

        // Find the best action at this depth
        for (const auto &[action, value] : actionValues) {
          if (value > bestValue) {
            bestValue = value;
            bestAction = action;
          }
        }
      }

      // Break if time limit reached
      if (timeOut) {
        std::cout << "Search timed out at depth " << depth << "\n";
        break;
      }
    }

    std::cout << "Completed search to depth " << maxDepthReached << "\n";
    std::cout << "Best action: " << static_cast<int>(bestAction)
              << " with value " << bestValue << "\n";

    return bestAction;
  } catch (const GameException &e) {
    std::cerr << "Game exception in search: " << e.what() << std::endl;
    // Fallback to a reasonable default strategy
    if (hasItem("Magnifying Glass")) {
      return Action::USE_MAGNIFYING_GLASS;
    } else if (hasItem("Handsaw") && !currentShotgun->getSawUsed()) {
      return Action::USE_HANDSAW;
    } else {
      return Action::SHOOT_OPPONENT;
    }
  } catch (const std::exception &e) {
    std::cerr << "Exception in search: " << e.what() << std::endl;
    return Action::SHOOT_OPPONENT;
  }
}

std::vector<Action> BotPlayer::determineFeasibleActions(SimulatedGame *state) {
  if (!state)
    return {Action::SHOOT_OPPONENT};

  auto *actingPlayer = state->isPlayerOneTurnNow() ? state->getPlayerOne()
                                                   : state->getPlayerTwo();
  std::vector<Action> feasible;

  if (!actingPlayer)
    return {Action::SHOOT_OPPONENT};

  // Always consider shooting the opponent
  feasible.push_back(Action::SHOOT_OPPONENT);

  // Consider using handcuffs if available and not already used
  if (actingPlayer->hasItem("Handcuffs") &&
      !actingPlayer->hasUsedHandcuffsThisTurn())
    feasible.push_back(Action::USE_HANDCUFFS);

  // Use informational items if available
  if (actingPlayer->hasItem("Magnifying Glass") &&
      !actingPlayer->isNextShellRevealed())
    feasible.push_back(Action::USE_MAGNIFYING_GLASS);

  // Prefer to use Handsaw if available and if the saw hasn't been applied
  if (actingPlayer->hasItem("Handsaw") && !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::USE_HANDSAW);

  // Always consider shooting self - the search will evaluate whether it's
  // worth the risk (e.g., high blank probability gives a free turn)
  feasible.push_back(Action::SHOOT_SELF);

  // Consider Beer if available
  if (actingPlayer->hasItem("Beer"))
    feasible.push_back(Action::DRINK_BEER);

  // Consider healing if health is not full
  if (actingPlayer->hasItem("Cigarette") &&
      (getMaxHealth() > actingPlayer->getHealth()))
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}