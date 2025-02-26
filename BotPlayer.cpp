#include "BotPlayer.h"
#include "Items/Item.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>

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

float BotPlayer::evaluateItems(const std::vector<Item *> &items) {
  float total = 0.0f;
  for (const auto &item : items) {
    total += valueOfItem(item);
  }
  return total;
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

  // 2. Actionable Item Bonus: use determineFeasibleActions to compute bonus.
  // Here, we reward the current player for items they can use this turn.
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
  // With ITEM_WEIGHT now positive (acting as a bonus), this rewards having
  // actionable items.
  float itemScore = ITEM_WEIGHT * actionableBonus;

  // 3. Shell Composition: lower live shell probability is better.
  float shellScore = 0.0f;
  if (state->getShotgun()->getTotalShellCount() > 0) {
    auto liveProbability =
        static_cast<float>(state->getShotgun()->getLiveShellProbability());
    shellScore = SHELL_WEIGHT * (1.0f - liveProbability);
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

  // 5. Turn Advantage: bonus if it is our turn.
  float turnScore = (state->isPlayerOneTurnNow() ? TURN_WEIGHT : -TURN_WEIGHT);

  // Total evaluation: sum of all weighted components.
  float totalScore =
      healthScore + itemScore + shellScore + statusScore + turnScore;
  return totalScore;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());
  if (!simShotgun)
    return false;

  // Determine current and opponent players based on whose turn it is.
  auto *currentPlayer = (state->isPlayerOneTurnNow()) ? state->getPlayerOne()
                                                      : state->getPlayerTwo();
  auto *otherPlayer = (!state->isPlayerOneTurnNow()) ? state->getPlayerOne()
                                                     : state->getPlayerTwo();

  if (!currentPlayer || !otherPlayer)
    return false;

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
    } else {
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
  if (!state)
    return nullptr;

  auto nextState = std::make_unique<SimulatedGame>(*state);
  bool newTurn = performAction(action, nextState.get(), ShellType::LIVE_SHELL);
  nextState->changePlayerTurn(newTurn);
  return nextState;
}

std::unique_ptr<SimulatedGame>
BotPlayer::simulateBlankAction(SimulatedGame *state, Action action) {
  if (!state)
    return nullptr;

  auto nextState = std::make_unique<SimulatedGame>(*state);
  bool newTurn = performAction(action, nextState.get(), ShellType::BLANK_SHELL);
  nextState->changePlayerTurn(newTurn);
  return nextState;
}

std::pair<std::unique_ptr<SimulatedGame>, std::unique_ptr<SimulatedGame>>
BotPlayer::simulateAction(SimulatedGame *state, Action action) {
  if (!state) {
    return {nullptr, nullptr};
  }

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

  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  // For actions with a single outcome
  if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
      action == Action::USE_HANDCUFFS) {
    auto newState = std::make_unique<SimulatedGame>(*state);
    performAction(action, newState.get(), ShellType::LIVE_SHELL);
    return expectiMiniMax(newState.get(), depth - 1);
  }
  // For actions when the shell is certain
  else if (pLive >= 0.999f) { // Using approximate comparison for floating point
    auto liveState = simulateLiveAction(state, action);
    return expectiMiniMax(liveState.get(), depth - 1);
  } else if (pBlank >= 0.999f) {
    auto blankState = simulateBlankAction(state, action);
    return expectiMiniMax(blankState.get(), depth - 1);
  }

  // For actions with probabilistic outcomes
  auto [liveState, blankState] = simulateAction(state, action);
  float liveVal = liveState ? expectiMiniMax(liveState.get(), depth - 1) : 0.0f;
  float blankVal =
      blankState ? expectiMiniMax(blankState.get(), depth - 1) : 0.0f;

  return pLive * liveVal + pBlank * blankVal;
}

float BotPlayer::expectiMiniMax(SimulatedGame *state, int depth) {
  if (!state)
    return 0.0f;

  if (depth == 0 || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty())
    return evaluateState(state);

  std::vector<Action> actionsToTry = determineFeasibleActions(state);

  float bestValue = state->isPlayerOneTurnNow()
                        ? -std::numeric_limits<float>::infinity()
                        : std::numeric_limits<float>::infinity();

  for (auto action : actionsToTry) {
    float value = expectedValueForAction(state, action, depth);
    if (state->isPlayerOneTurnNow())
      bestValue = std::max(bestValue, value);
    else
      bestValue = std::min(bestValue, value);
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

  // If the opponent is nearly dead, or if the next live round can guarantee a
  // kill
  if (opponent->getHealth() == 1 ||
      (currentShotgun->getSawUsed() && opponent->getHealth() == 2)) {
    resetKnownNextShell();
    return Action::SHOOT_OPPONENT;
  }

  // If a Handsaw is available and the saw hasn't been applied, use it
  if (hasItem("Handsaw") && !currentShotgun->getSawUsed())
    return Action::USE_HANDSAW;

  // If health is not full and a healing item is available, use it
  if (health < getMaxHealth() && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  resetKnownNextShell();
  return Action::SHOOT_OPPONENT;
}

Action BotPlayer::chooseAction(Shotgun *currentShotgun) {
  if (!currentShotgun)
    return Action::SHOOT_OPPONENT;

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
  if (pBlank >= 0.999f)
    return Action::SHOOT_SELF;
  else if (pLive >= 0.999f)
    return liveShellAction(currentShotgun);

  std::cout << "Live Probability: " << pLive << "\n";
  std::cout << "Blank Probability: " << pBlank << "\n";

  // Prefer healing if needed
  if (health < getMaxHealth() && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  // Build a starting simulated state
  auto simShotgun = std::make_unique<SimulatedShotgun>(
      currentShotgun->getTotalShellCount(), currentShotgun->getLiveShellCount(),
      currentShotgun->getBlankShellCount(), currentShotgun->getSawUsed());

  // Create simulated players
  auto simP1 = std::make_unique<SimulatedPlayer>(*this);
  auto simP2 = std::make_unique<SimulatedPlayer>(*this->opponent);

  // Set opponent relationships
  simP1->setOpponent(simP2.get());
  simP2->setOpponent(simP1.get());

  // Create the initial game state
  auto initState = std::make_unique<SimulatedGame>(simP1.get(), simP2.get(),
                                                   simShotgun.get(), true);

  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  // Determine all possible actions from this state
  std::vector<Action> actionsToTry = determineFeasibleActions(initState.get());

  // Evaluate each action using expectiminimax
  for (auto action : actionsToTry) {
    float actionValue;

    std::cout << "Evaluating action: " << static_cast<int>(action) << "\n";

    if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
        action == Action::USE_HANDCUFFS) {
      auto newState = std::make_unique<SimulatedGame>(*initState);
      bool turnAfterAction =
          performAction(action, newState.get(), ShellType::LIVE_SHELL);
      newState->changePlayerTurn(turnAfterAction);
      actionValue = expectiMiniMax(newState.get(), SEARCH_DEPTH - 1);

      std::cout << "  [Single outcome] Action value: " << actionValue << "\n";
    } else {
      auto [liveState, blankState] = simulateAction(initState.get(), action);
      float liveVal =
          liveState ? expectiMiniMax(liveState.get(), SEARCH_DEPTH - 1) : 0.0f;
      float blankVal = blankState
                           ? expectiMiniMax(blankState.get(), SEARCH_DEPTH - 1)
                           : 0.0f;
      actionValue = pLive * liveVal + pBlank * blankVal;

      std::cout << "  [Dual outcome] liveVal: " << liveVal
                << ", blankVal: " << blankVal
                << ", action value: " << actionValue << "\n";
    }

    std::cout << "Action " << static_cast<int>(action)
              << " has overall value: " << actionValue << "\n";

    if (actionValue > bestValue) {
      bestValue = actionValue;
      bestAction = action;
    }
  }

  return bestAction;
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
  if (actingPlayer->hasItem("Handsaw") && !state->getShotgun()->getSawUsed() &&
      !actingPlayer->hasItem("Magnifying Glass"))
    feasible.push_back(Action::USE_HANDSAW);

  // If the next shell is revealed as blank, SHOOT_SELF is safe
  if (actingPlayer->isNextShellRevealed() &&
      actingPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL)
    feasible.push_back(Action::SHOOT_SELF);

  if (actingPlayer->hasItem("Beer") && !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::DRINK_BEER);

  // Consider healing if health is not full
  if (actingPlayer->hasItem("Cigarette") &&
      (getMaxHealth() > actingPlayer->getHealth()))
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}