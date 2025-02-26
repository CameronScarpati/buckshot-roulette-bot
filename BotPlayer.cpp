#include "BotPlayer.h"
#include "Items/Item.h"
#include <algorithm>
#include <limits>

float BotPlayer::valueOfItem(const Item *item) {
  std::string name = item->getName();
  if (name == "Beer")
    return BEER_VALUE;
  else if (name == "Cigarette")
    return CIGARETTE_VALUE;
  else if (name == "Handcuffs")
    return HANDCUFFS_VALUE;
  else if (name == "MagnifyingGlass")
    return MAGNIFYING_GLASS_VALUE;
  else if (name == "Handsaw")
    return HANDSAW_VALUE;
  return 0.0f;
}

float BotPlayer::evaluateItems(const std::vector<Item *> &items) {
  float total = 0;
  for (const auto &item : items)
    total += valueOfItem(item);
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
      HEALTH_WEIGHT *
      ((static_cast<float>(state->getPlayerOne()->getHealth()) / maxHealth) -
       (static_cast<float>(state->getPlayerTwo()->getHealth()) / maxHealth));

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
  if (state->isPlayerOneTurn && state->getShotgun()->getSawUsed())
    statusScore += HANDSAW_WEIGHT;
  if (!state->isPlayerOneTurn && state->getShotgun()->getSawUsed())
    statusScore -= HANDSAW_WEIGHT;

  // 5. Turn Advantage: bonus if it is our turn.
  float turnScore = (state->isPlayerOneTurn ? TURN_WEIGHT : -TURN_WEIGHT);

  // Total evaluation: sum of all weighted components.
  float totalScore =
      healthScore + itemScore + shellScore + statusScore + turnScore;
  return totalScore;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());

  // Determine current and opponent players based on whose turn it is.
  auto *currentPlayer =
      (state->isPlayerOneTurn) ? state->getPlayerOne() : state->getPlayerTwo();
  auto *otherPlayer =
      (!state->isPlayerOneTurn) ? state->getPlayerOne() : state->getPlayerTwo();

  switch (action) {
  case Action::SHOOT_SELF:
    currentPlayer->resetKnownNextShell();
    state->getShotgun()->resetSawUsed();
    if (shell == ShellType::LIVE_SHELL) {
      simShotgun->simulateLiveShell();
      currentPlayer->loseHealth(simShotgun->getSawUsed());
      // If the opponent is handcuffed, remove them and keep turn.
      if (otherPlayer->areHandcuffsApplied()) {
        otherPlayer->removeHandcuffs();
        return state->isPlayerOneTurn;
      } else {
        currentPlayer->resetHandcuffUsage();
        return !state->isPlayerOneTurn;
      }
    } else {
      simShotgun->simulateBlankShell();
      return state->isPlayerOneTurn; // Blank shell grants extra turn.
    }

  case Action::SHOOT_OPPONENT:
    currentPlayer->resetKnownNextShell();
    state->getShotgun()->resetSawUsed();
    if (shell == ShellType::LIVE_SHELL) {
      otherPlayer->loseHealth(simShotgun->getSawUsed());
      simShotgun->simulateLiveShell();
    } else {
      simShotgun->simulateBlankShell();
    }
    // If the opponent is handcuffed, remove them and keep turn.
    if (otherPlayer->areHandcuffsApplied()) {
      otherPlayer->removeHandcuffs();
      return state->isPlayerOneTurn;
    } else {
      currentPlayer->resetHandcuffUsage();
      return !state->isPlayerOneTurn;
    }

  case Action::USE_MAGNIFYING_GLASS:
    currentPlayer->setKnownNextShell(shell);
    currentPlayer->removeItemByName("Magnifying Glass");
    return state->isPlayerOneTurn;

  case Action::SMOKE_CIGARETTE:
    currentPlayer->smokeCigarette();
    currentPlayer->removeItemByName("Cigarette");
    return state->isPlayerOneTurn;

  case Action::DRINK_BEER:
    currentPlayer->resetKnownNextShell();
    currentPlayer->removeItemByName("Beer");
    return state->isPlayerOneTurn;

  case Action::USE_HANDSAW:
    state->getShotgun()->useHandsaw();
    currentPlayer->removeItemByName("Handsaw");
    return state->isPlayerOneTurn;

  case Action::USE_HANDCUFFS:
    otherPlayer->applyHandcuffs();
    currentPlayer->useHandcuffsThisTurn();
    currentPlayer->removeItemByName("Handcuffs");
    return state->isPlayerOneTurn;

  default:
    return !state->isPlayerOneTurn;
  }
}

SimulatedGame *BotPlayer::simulateLiveAction(SimulatedGame *state,
                                             Action action) {
  auto *nextState = new SimulatedGame(*state);
  bool newTurn = performAction(action, nextState, ShellType::LIVE_SHELL);
  nextState->isPlayerOneTurn = newTurn;
  return nextState;
}

SimulatedGame *BotPlayer::simulateBlankAction(SimulatedGame *state,
                                              Action action) {
  auto *nextState = new SimulatedGame(*state);
  bool newTurn = performAction(action, nextState, ShellType::BLANK_SHELL);
  nextState->isPlayerOneTurn = newTurn;
  return nextState;
}

std::pair<SimulatedGame *, SimulatedGame *>
BotPlayer::simulateAction(SimulatedGame *state, Action action) {
  if (action == Action::USE_MAGNIFYING_GLASS) {
    // Simulate both outcomes for shell revelation.
    auto *liveReveal = new SimulatedGame(*state);
    auto *playerLive = (liveReveal->isPlayerOneTurn)
                           ? liveReveal->getPlayerOne()
                           : liveReveal->getPlayerTwo();
    playerLive->setKnownNextShell(ShellType::LIVE_SHELL);
    performAction(Action::USE_MAGNIFYING_GLASS, liveReveal,
                  ShellType::LIVE_SHELL);

    auto *blankReveal = new SimulatedGame(*state);
    auto *playerBlank = (blankReveal->isPlayerOneTurn)
                            ? blankReveal->getPlayerOne()
                            : blankReveal->getPlayerTwo();
    playerBlank->setKnownNextShell(ShellType::BLANK_SHELL);
    performAction(Action::USE_MAGNIFYING_GLASS, blankReveal,
                  ShellType::BLANK_SHELL);

    return {liveReveal, blankReveal};
  } else if (action == Action::DRINK_BEER) {
    // Simulate beer usage for both live and blank shell outcomes.
    auto *liveBranch = new SimulatedGame(*state);
    auto *liveShotgun =
        dynamic_cast<SimulatedShotgun *>(liveBranch->getShotgun());
    if (liveShotgun->getLiveShellCount() > 0)
      liveShotgun->simulateLiveShell();
    bool turnAfterLive =
        performAction(Action::DRINK_BEER, liveBranch, ShellType::LIVE_SHELL);
    liveBranch->isPlayerOneTurn = turnAfterLive;

    auto *blankBranch = new SimulatedGame(*state);
    auto *blankShotgun =
        dynamic_cast<SimulatedShotgun *>(blankBranch->getShotgun());
    if (blankShotgun->getBlankShellCount() > 0)
      blankShotgun->simulateBlankShell();
    bool turnAfterBlank =
        performAction(Action::DRINK_BEER, blankBranch, ShellType::BLANK_SHELL);
    blankBranch->isPlayerOneTurn = turnAfterBlank;

    return {liveBranch, blankBranch};
  }

  SimulatedGame *liveState = simulateLiveAction(state, action);
  SimulatedGame *blankState = simulateBlankAction(state, action);
  return {liveState, blankState};
}

float BotPlayer::expectedValueForAction(SimulatedGame *state, Action action,
                                        int depth) {
  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
      action == Action::USE_HANDCUFFS) {
    auto *newState = new SimulatedGame(*state);
    performAction(action, newState, ShellType::LIVE_SHELL);
    return expectiMiniMax(newState, depth - 1);
  } else if (pLive == 1.0f) {
    SimulatedGame *liveState = simulateLiveAction(state, action);
    return expectiMiniMax(liveState, depth - 1);
  } else if (pBlank == 1.0f) {
    SimulatedGame *blankState = simulateBlankAction(state, action);
    return expectiMiniMax(blankState, depth - 1);
  }
  auto nextStates = simulateAction(state, action);
  SimulatedGame *liveState = nextStates.first;
  SimulatedGame *blankState = nextStates.second;
  float liveVal = expectiMiniMax(liveState, depth - 1);
  float blankVal = expectiMiniMax(blankState, depth - 1);
  return pLive * liveVal + pBlank * blankVal;
}

float BotPlayer::expectiMiniMax(SimulatedGame *state, int depth) {
  if (depth == 0 || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty())
    return evaluateState(state);

  std::vector<Action> actionsToTry = determineFeasibleActions(state);

  float bestValue = state->isPlayerOneTurn
                        ? -std::numeric_limits<float>::infinity()
                        : std::numeric_limits<float>::infinity();

  for (auto action : actionsToTry) {
    float value = expectedValueForAction(state, action, depth);
    if (state->isPlayerOneTurn)
      bestValue = std::max(bestValue, value);
    else
      bestValue = std::min(bestValue, value);
  }

  return bestValue;
}

BotPlayer::BotPlayer(const std::string &name, int health)
    : Player(name, health) {}

BotPlayer::BotPlayer(const std::string &name, int health, Player *opponent)
    : Player(name, health, opponent) {}

Action BotPlayer::liveShellAction(Shotgun *currentShotgun) {
  // If the opponent is nearly dead, or if the next live round can guarantee a
  // kill.
  if (opponent->getHealth() == 1 ||
      (currentShotgun->getSawUsed() && opponent->getHealth() == 2)) {
    resetKnownNextShell();
    return Action::SHOOT_OPPONENT;
  }

  // If a Handsaw is available and the saw hasn't been applied, use it.
  if (hasItem("Handsaw") && !currentShotgun->getSawUsed())
    return Action::USE_HANDSAW;

  // If health is not full and a healing item is available, use it.
  if (health < maxHealth && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  resetKnownNextShell();
  return Action::SHOOT_OPPONENT;
}

Action BotPlayer::chooseAction(Shotgun *currentShotgun) {
  // If the next shell is already revealed, decide based on its type.
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

  // If only blanks remain, shoot self; if only lives, act accordingly.
  if (pBlank == 1.0f)
    return Action::SHOOT_SELF;
  else if (pLive == 1.0f)
    return liveShellAction(currentShotgun);
  std::cout << "Live Probability: " << pLive << "\n";
  std::cout << "Blank Probability: " << pBlank << "\n";

  // Prefer healing if needed.
  if (health < maxHealth && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  // Build a starting simulated state.
  auto *simShotgun = new SimulatedShotgun(
      currentShotgun->getTotalShellCount(), currentShotgun->getLiveShellCount(),
      currentShotgun->getBlankShellCount(), currentShotgun->getSawUsed());
  // Create simulated players.
  auto *simP1 = new SimulatedPlayer(*this);
  auto *simP2 = new SimulatedPlayer(*this->opponent);
  simP1->setOpponent(simP2);
  simP2->setOpponent(simP1);

  auto *initState = new SimulatedGame(simP1, simP2, simShotgun, true);

  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  // Determine all possible actions from this state.
  std::vector<Action> actionsToTry = determineFeasibleActions(initState);

  // Evaluate each action using expectiminimax.
  for (auto action : actionsToTry) {
    float actionValue;

    std::cout << "Evaluating action: " << static_cast<int>(action) << "\n";

    if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
        action == Action::USE_HANDCUFFS) {
      auto *newState = new SimulatedGame(*initState);
      bool turnAfterAction =
          performAction(action, newState, ShellType::LIVE_SHELL);
      newState->isPlayerOneTurn = turnAfterAction;
      actionValue = expectiMiniMax(newState, SEARCH_DEPTH - 1);

      std::cout << "  [Single outcome] Action value: " << actionValue << "\n";
    } else {
      auto nextStates = simulateAction(initState, action);
      SimulatedGame *liveState = nextStates.first;
      SimulatedGame *blankState = nextStates.second;
      float liveVal = expectiMiniMax(liveState, SEARCH_DEPTH - 1);
      float blankVal = expectiMiniMax(blankState, SEARCH_DEPTH - 1);
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
  auto *actingPlayer =
      state->isPlayerOneTurn ? state->getPlayerOne() : state->getPlayerTwo();
  std::vector<Action> feasible;

  // Always consider shooting the opponent.
  feasible.push_back(Action::SHOOT_OPPONENT);

  // Consider using handcuffs if available and not already used.
  if (actingPlayer && actingPlayer->hasItem("Handcuffs") &&
      !actingPlayer->hasUsedHandcuffsThisTurn())
    feasible.push_back(Action::USE_HANDCUFFS);

  // Use informational items if available.
  if (actingPlayer && actingPlayer->hasItem("Magnifying Glass") &&
      !actingPlayer->isNextShellRevealed())
    feasible.push_back(Action::USE_MAGNIFYING_GLASS);

  // Prefer to use Handsaw if available and if the saw hasn't been applied.
  if (actingPlayer && actingPlayer->hasItem("Handsaw") &&
      !state->getShotgun()->getSawUsed() &&
      !actingPlayer->hasItem("Magnifying Glass"))
    feasible.push_back(Action::USE_HANDSAW);

  // If the next shell is revealed as blank, SHOOT_SELF is safe.
  if (actingPlayer && actingPlayer->isNextShellRevealed() &&
      actingPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL)
    feasible.push_back(Action::SHOOT_SELF);

  if (actingPlayer && actingPlayer->hasItem("Beer") &&
      !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::DRINK_BEER);

  // Consider healing if health is not full.
  if (actingPlayer && actingPlayer->hasItem("Cigarette") &&
      (Player::maxHealth > actingPlayer->getHealth()))
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}
