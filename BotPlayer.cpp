#include "BotPlayer.h"
#include "Items/Item.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>

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
    return -1000.0f;
  if (!state->getPlayerTwo()->isAlive())
    return +1000.0f;

  // 1. Health Differential: normalized difference.
  float healthScore =
      HEALTH_WEIGHT * ((static_cast<float>(state->getPlayerOne()->getHealth()) /
                        static_cast<float>(getMaxHealth())) -
                       (static_cast<float>(state->getPlayerTwo()->getHealth()) /
                        static_cast<float>(getMaxHealth())));

  // 2. Item Value Comparison: items current player can use this turn.
  auto *currentPlayer = state->isPlayerOneTurnNow() ? state->getPlayerOne()
                                                    : state->getPlayerTwo();
  auto *otherPlayer = !state->isPlayerOneTurnNow() ? state->getPlayerOne()
                                                   : state->getPlayerTwo();

  std::vector<Action> feasible = determineFeasibleActions(state);
  float actionableBonus = 0.0f;
  for (auto action : feasible) {
    switch (action) {
    case Action::SMOKE_CIGARETTE:
      actionableBonus += CIGARETTE_VALUE;
      break;
    case Action::DRINK_BEER:
      actionableBonus += BEER_VALUE;
      break;
    case Action::USE_HANDCUFFS:
      actionableBonus += HANDCUFFS_VALUE;
      break;
    case Action::USE_MAGNIFYING_GLASS:
      actionableBonus += MAGNIFYING_GLASS_VALUE;
      break;
    case Action::USE_HANDSAW:
      actionableBonus += HANDSAW_VALUE;
      break;
    default:
      break;
    }
  }

  // This rewards having actionable items.
  float itemScore = ITEM_WEIGHT * actionableBonus;

  // 3. Shell Composition: lower live shell probability is better.
  float shellScore = 0.0f;
  float liveProbability;

  if (state->getShotgun()->getTotalShellCount() > 0) {
    liveProbability =
        static_cast<float>(state->getShotgun()->getLiveShellProbability());
    shellScore = SHELL_WEIGHT * (1.0f - liveProbability);

    // Add penalty if it's our turn and most shells are live
    if (state->isPlayerOneTurnNow() && liveProbability > 0.7f)
      shellScore -= DANGEROUS_TURN_PENALTY * liveProbability;
  }

  // 4. Status Effects: Handcuffs, Handsaw, and Magnifying Glass statuses.
  float statusScore = 0.0f;
  if (state->getPlayerTwo()->areHandcuffsApplied())
    statusScore += HANDCUFF_WEIGHT;
  if (state->getPlayerOne()->areHandcuffsApplied())
    statusScore -= HANDCUFF_WEIGHT;
  if (state->getPlayerTwo()->isNextShellRevealed())
    statusScore += MAGNIFYING_GLASS_WEIGHT;
  if (state->getPlayerOne()->isNextShellRevealed())
    statusScore -= MAGNIFYING_GLASS_WEIGHT;
  if (state->isPlayerOneTurnNow() && state->getShotgun()->getSawUsed())
    statusScore += HANDSAW_WEIGHT;
  if (!state->isPlayerOneTurnNow() && state->getShotgun()->getSawUsed())
    statusScore -= HANDSAW_WEIGHT;

  // 5. Item count advantage
  float itemCountScore;
  try {
    int playerItemCount =
        static_cast<int>(currentPlayer->getItemsView().size());
    int opponentItemCount =
        static_cast<int>(otherPlayer->getItemsView().size());
    itemCountScore = ITEM_COUNT_WEIGHT *
                     static_cast<float>((playerItemCount - opponentItemCount));
  } catch (...) {
    // In case of any issues with item access, continue
  }

  // 6. Turn Advantage: bonus if it is our turn.
  float turnScore = (state->isPlayerOneTurnNow() ? TURN_WEIGHT : -TURN_WEIGHT);

  // Total evaluation: sum of all weighted components.
  return healthScore + itemScore + shellScore + statusScore + turnScore +
         itemCountScore;
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
    simShotgun->resetSawUsed();
    if (shell == ShellType::LIVE_SHELL) {
      simShotgun->simulateLiveShell();
      currentPlayer->loseHealth(simShotgun->getSawUsed());

      // If the opponent is handcuffed, remove them and keep turn.
      if (otherPlayer->areHandcuffsApplied()) {
        otherPlayer->removeHandcuffs();
        return state->isPlayerOneTurnNow();
      } else {
        currentPlayer->resetHandcuffUsage();
        return !state->isPlayerOneTurnNow();
      }
    } else {
      simShotgun->simulateBlankShell();
      return state->isPlayerOneTurnNow(); // Blank shell grants extra turn.
    }

  case Action::SHOOT_OPPONENT:
    currentPlayer->resetKnownNextShell();
    simShotgun->resetSawUsed();
    if (shell == ShellType::LIVE_SHELL) {
      otherPlayer->loseHealth(simShotgun->getSawUsed());
      simShotgun->simulateLiveShell();
    } else
      simShotgun->simulateBlankShell();

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

float BotPlayer::expectedValueForAction(SimulatedGame *state, Action action,
                                        int depth) {
  if (depth <= 0)
    return 0.0f;

  // A single deterministic outcome does not need to consider probabilities
  if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
      action == Action::USE_HANDCUFFS) {
    auto newState = simulateNonProbabilisticAction(state, action);
    float result = expectiMiniMax(newState.get(), depth - 1);
    return result;
  }

  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  // For actions when the shell is certain (using epsilon for float comparison)
  if (std::abs(pLive - 1.0f) < EPSILON) {
    auto liveState = simulateLiveAction(state, action);
    float result = expectiMiniMax(liveState.get(), depth - 1);
    return result;
  } else if (std::abs(pBlank - 1.0f) < EPSILON) {
    auto blankState = simulateBlankAction(state, action);
    float result = expectiMiniMax(blankState.get(), depth - 1);
    return result;
  }

  auto [liveState, blankState] = simulateAction(state, action);

  float liveVal = 0.0f;
  if (liveState)
    liveVal = expectiMiniMax(liveState.get(), depth - 1);

  float blankVal = 0.0f;
  if (blankState)
    blankVal = expectiMiniMax(blankState.get(), depth - 1);

  return pLive * liveVal + pBlank * blankVal;
}

float BotPlayer::expectiMiniMax(
    SimulatedGame *state, int depth, float alpha, float beta,
    std::chrono::steady_clock::time_point startTime) {
  // Check if time limit exceeded
  if (timeExpired(startTime))
    return evaluateState(state);

  if (depth == 0 || !state->getPlayerOne() || !state->getPlayerTwo() ||
      !state->getShotgun() || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty())
    return evaluateState(state);

  std::vector<Action> actionsToTry;
  try {
    actionsToTry =
        prioritizeStrategicActions(determineFeasibleActions(state), state);
  } catch (...) {
    return evaluateState(state);
  }

  float bestValue = state->isPlayerOneTurnNow()
                        ? -std::numeric_limits<float>::infinity()
                        : std::numeric_limits<float>::infinity();

  for (auto action : actionsToTry) {
    float value;
    try {
      value = expectedValueForAction(state, action, depth);
    } catch (...) {
      continue; // Skip this action if it causes an exception
    }

    if (state->isPlayerOneTurnNow()) {
      bestValue = std::max(bestValue, value);
      alpha = std::max(alpha, bestValue);
      if (beta <= alpha)
        break; // Beta cutoff
    } else {
      bestValue = std::min(bestValue, value);
      beta = std::min(beta, bestValue);
      if (beta <= alpha)
        break; // Alpha cutoff
    }

    if (timeExpired(startTime))
      return bestValue;
  }

  return bestValue;
}

BotPlayer::BotPlayer(std::string name, int health)
    : Player(std::move(name), health) {}

BotPlayer::BotPlayer(std::string name, int health, Player *opponent)
    : Player(std::move(name), health, opponent) {}

Action BotPlayer::liveShellAction(Shotgun *currentShotgun) {
  if (!currentShotgun)
    return Action::SHOOT_OPPONENT;

  // Guaranteed kill option.
  if (opponent->getHealth() == 1 ||
      (currentShotgun->getSawUsed() && opponent->getHealth() == 2)) {
    resetKnownNextShell();
    return Action::SHOOT_OPPONENT;
  }

  // If Handcuffs available, use them to control opponent's turn.
  if (hasItem("Handcuffs") && !hasUsedHandcuffsThisTurn())
    return Action::USE_HANDCUFFS;

  // If a Handsaw is available and the saw hasn't been applied, use it.
  if (hasItem("Handsaw") && !currentShotgun->getSawUsed())
    return Action::USE_HANDSAW;

  // If health is not full and a healing item is available, use it.
  if (health < getMaxHealth() && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

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
    // Medium priority: handcuffs, handsaw
    else if (action == Action::USE_HANDCUFFS || action == Action::USE_HANDSAW) {
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

  std::cout << "Live Probability: " << pLive << "\n";
  std::cout << "Blank Probability: " << pBlank << "\n";

  // Prefer healing if needed and at significant health disadvantage
  if (health < getMaxHealth() && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

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
    } catch (...) {
      // Fallback to a basic set of actions if there's an error
      actionsToTry = {Action::SHOOT_OPPONENT, Action::SHOOT_SELF};
    }

    // Try iterative deepening with a lower maximum depth
    int maxDepthReached = 0;
    for (int depth = MIN_SEARCH_DEPTH; depth <= std::min(MAX_SEARCH_DEPTH, 5);
         depth++) {
      bool timeOut = false;
      std::unordered_map<Action, float> actionValues;

      // Evaluate each action using expectiminimax at current depth
      for (auto action : actionsToTry) {
        float actionValue;

        std::cout << "Evaluating action: " << static_cast<int>(action)
                  << " at depth " << depth << "\n";

        try {
          if (action == Action::SMOKE_CIGARETTE ||
              action == Action::USE_HANDSAW ||
              action == Action::USE_HANDCUFFS) {
            auto newState =
                simulateNonProbabilisticAction(initState.get(), action);
            actionValue = expectiMiniMax(
                newState.get(), depth - 1,
                -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(), startTime);
          } else {
            auto [liveState, blankState] =
                simulateAction(initState.get(), action);
            float liveVal =
                liveState
                    ? expectiMiniMax(liveState.get(), depth - 1,
                                     -std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::infinity(),
                                     startTime)
                    : 0.0f;

            // Check for timeout after first branch
            if (timeExpired(startTime)) {
              timeOut = true;
              break;
            }

            float blankVal =
                blankState
                    ? expectiMiniMax(blankState.get(), depth - 1,
                                     -std::numeric_limits<float>::infinity(),
                                     std::numeric_limits<float>::infinity(),
                                     startTime)
                    : 0.0f;
            actionValue = pLive * liveVal + pBlank * blankVal;
          }
        } catch (...) {
          // Skip this action if it caused an exception
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
  } catch (const std::exception &e) {
    std::cerr << "Exception in search: " << e.what() << std::endl;
    // Fallback to a reasonable default strategy
    if (hasItem("Magnifying Glass")) {
      return Action::USE_MAGNIFYING_GLASS;
    } else if (hasItem("Handsaw") && !currentShotgun->getSawUsed()) {
      return Action::USE_HANDSAW;
    } else {
      return Action::SHOOT_OPPONENT;
    }
  } catch (...) {
    std::cerr << "Unknown exception in search" << std::endl;
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

  // If the next shell is revealed as blank, SHOOT_SELF is safe
  if ((actingPlayer->isNextShellRevealed() &&
       actingPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL) ||
      std::abs(state->getShotgun()->getBlankShellProbability() - 1.0f) <
          EPSILON)
    feasible.push_back(Action::SHOOT_SELF);

  // Consider Beer only if a shotgun hasn't been sawed
  if (actingPlayer->hasItem("Beer") && !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::DRINK_BEER);

  // Consider healing if health is not full
  if (actingPlayer->hasItem("Cigarette") &&
      (getMaxHealth() > actingPlayer->getHealth()))
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}