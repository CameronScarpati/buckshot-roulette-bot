#include "BotPlayer.h"
#include <algorithm>
#include <limits>

const int SEARCH_DEPTH = 8;

float BotPlayer::evaluateState(SimulatedGame *state) {
  // Terminal states.
  if (!state->getPlayerOne()->isAlive())
    return -10000.0f;
  if (!state->getPlayerTwo()->isAlive())
    return +10000.0f;

  float score = 0.0f;
  float healthWeight = 70.0f;
  // Health difference: bot (player one) minus opponent.
  auto hpDifference = static_cast<float>(state->getPlayerOne()->getHealth()) -
                      static_cast<float>(state->getPlayerTwo()->getHealth());
  score += hpDifference * healthWeight;

  // In our simulation player one is the bot.
  bool myTurn = state->isPlayerOneTurn;
  auto *botPlayer = state->getPlayerOne();
  auto *oppPlayer = state->getPlayerTwo();

  // Bonus for a favorable shotgun state (more live rounds is better).
  score +=
      ((myTurn) ? 1 : -1) * 10.0f * state->getShotgun()->getLiveShellCount();

  // If it's our turn, and we have a magnifying glass with an unrevealed shell.
  if (myTurn && !botPlayer->isNextShellRevealed() &&
      botPlayer->hasItem("Magnifying Glass")) {
    float pLive = state->getShotgun()->getLiveShellProbability();
    float pBlank = state->getShotgun()->getBlankShellProbability();
    float uncertainty = pLive * pBlank;
    score += 40.0f * uncertainty;
    if (botPlayer->getHealth() == 1)
      score += 10.0f;
  }

  // Penalize very low health.
  if (botPlayer->getHealth() == 1)
    score -= 90.0f;

  // Factor in if the next shell is revealed (applied only once).
  if (botPlayer->isNextShellRevealed()) {
    float handSawMultiplier = (botPlayer->hasItem("Handsaw")) ? 2.25f : 1.0f;
    if (botPlayer->returnKnownNextShell() == ShellType::LIVE_SHELL) {
      // Increase bonus when the opponent is vulnerable.
      float bonus = (oppPlayer->getHealth() <= 2) ? 100.0f : 50.0f;
      score += myTurn ? bonus * handSawMultiplier : -bonus * handSawMultiplier;
    } else
      score += myTurn ? 25.0f * handSawMultiplier : -25.0f;
  }

  return score;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());

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
      return !state->isPlayerOneTurn;
    } else {
      simShotgun->simulateBlankShell();
      return state->isPlayerOneTurn;
    }
  case Action::SHOOT_OPPONENT:
    currentPlayer->resetKnownNextShell();
    state->getShotgun()->resetSawUsed();
    if (shell == ShellType::LIVE_SHELL) {
      otherPlayer->loseHealth(simShotgun->getSawUsed());
      simShotgun->simulateLiveShell();
    } else
      simShotgun->simulateBlankShell();
    return !state->isPlayerOneTurn;
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

    // TODO: Implement handcuffs.
  }
  return !state->isPlayerOneTurn;
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
    auto *liveReveal = new SimulatedGame(*state);
    auto *playerLive = (liveReveal->isPlayerOneTurn)
                           ? liveReveal->getPlayerOne()
                           : liveReveal->getPlayerTwo();
    playerLive->setKnownNextShell(ShellType::LIVE_SHELL);
    bool turnAfterLive = performAction(Action::USE_MAGNIFYING_GLASS, liveReveal,
                                       ShellType::LIVE_SHELL);
    liveReveal->isPlayerOneTurn = turnAfterLive;

    auto *blankReveal = new SimulatedGame(*state);
    auto *playerBlank = (blankReveal->isPlayerOneTurn)
                            ? blankReveal->getPlayerOne()
                            : blankReveal->getPlayerTwo();
    playerBlank->setKnownNextShell(ShellType::BLANK_SHELL);
    bool turnAfterBlank = performAction(Action::USE_MAGNIFYING_GLASS,
                                        blankReveal, ShellType::BLANK_SHELL);
    blankReveal->isPlayerOneTurn = turnAfterBlank;

    return {liveReveal, blankReveal};
  } else if (action == Action::DRINK_BEER) {
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

  if (action == Action::SMOKE_CIGARETTE) {
    auto *newState = new SimulatedGame(*state);
    bool turnAfterAction =
        performAction(action, newState, ShellType::LIVE_SHELL);
    newState->isPlayerOneTurn = turnAfterAction;
    return expectiMiniMax(newState, depth - 1, newState->isPlayerOneTurn);
  } else if (action == Action::USE_HANDSAW) {
    auto *newState = new SimulatedGame(*state);
    bool turnAfterAction =
        performAction(action, newState, ShellType::LIVE_SHELL);
    newState->isPlayerOneTurn = turnAfterAction;
    float value =
        expectiMiniMax(newState, depth - 1, newState->isPlayerOneTurn);
    // If the bot still has a Magnifying Glass, and hasn't revealed the next
    // shell, penalize the value of using the handsaw so that the bot will
    // prefer to reveal first.
    if (state->getPlayerOne()->hasItem("Magnifying Glass") &&
        !state->getPlayerOne()->isNextShellRevealed()) {
      value *= 0.3f;
    }
    return value;
  } else if (action == Action::DRINK_BEER) {
    auto nextStates = simulateAction(state, action);
    SimulatedGame *liveState = nextStates.first;
    SimulatedGame *blankState = nextStates.second;
    float liveVal =
        expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
    float blankVal =
        expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
    float baseValue = -100.0f + pLive * liveVal + pBlank * blankVal;

    auto *current = state->getPlayerOne(); // our bot in simulation
    auto *opp = state->getPlayerTwo();
    float reloadBonus = 0.0f;
    // Bonus for having room for more items.
    if (current->getItemCount() < MAX_ITEMS)
      reloadBonus += 50.0f;
    // Bonus if we're vulnerable compared to the opponent.
    if (current->getHealth() < opp->getHealth())
      reloadBonus += 30.0f;
    // Extra bonus when our health is very low.
    if (current->getHealth() <= 2) {
      reloadBonus += 75.0f;
      // Further bonus if we lack healing items.
      if (!current->hasItem("Cigarette"))
        reloadBonus += 25.0f;
    }

    return baseValue + reloadBonus;
  } else if (pLive == 1.0f) {
    SimulatedGame *liveState = simulateLiveAction(state, action);
    return expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
  } else if (pBlank == 1.0f) {
    SimulatedGame *blankState = simulateBlankAction(state, action);
    return expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
  } else {
    auto nextStates = simulateAction(state, action);
    SimulatedGame *liveState = nextStates.first;
    SimulatedGame *blankState = nextStates.second;
    float liveVal =
        expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
    float blankVal =
        expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
    return pLive * liveVal + pBlank * blankVal;
  }
}

float BotPlayer::expectiMiniMax(SimulatedGame *state, int depth,
                                bool maximizingPlayer) {
  if (depth == 0 || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty())
    return evaluateState(state);

  std::vector<Action> actionsToTry = determineFeasibleActions(state);

  float bestValue = maximizingPlayer ? -std::numeric_limits<float>::infinity()
                                     : std::numeric_limits<float>::infinity();

  for (auto action : actionsToTry) {
    float value = expectedValueForAction(state, action, depth);
    if (maximizingPlayer)
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
  // Guaranteed kill states.
  if (opponent->getHealth() == 1 ||
      (currentShotgun->getSawUsed() && opponent->getHealth() == 2)) {
    resetKnownNextShell();
    return Action::SHOOT_OPPONENT;
  }

  if (hasItem("Handsaw") && !currentShotgun->getSawUsed())
    return Action::USE_HANDSAW;

  if (health < maxHealth && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  resetKnownNextShell();
  return Action::SHOOT_OPPONENT;
}

Action BotPlayer::chooseAction(Shotgun *currentShotgun) {
  if (isNextShellRevealed()) {
    if (returnKnownNextShell() == ShellType::LIVE_SHELL) {
      return liveShellAction(currentShotgun);
    } else {
      resetKnownNextShell();
      return Action::SHOOT_SELF;
    }
  }

  float pLive = currentShotgun->getLiveShellProbability();
  float pBlank = currentShotgun->getBlankShellProbability();

  if (pLive == 0.0f)
    return Action::SHOOT_SELF;
  else if (pLive == 1.0f)
    return liveShellAction(currentShotgun);

  if (health < maxHealth && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  auto *simShotgun = new SimulatedShotgun(
      currentShotgun->getTotalShellCount(), currentShotgun->getLiveShellCount(),
      currentShotgun->getBlankShellCount(), currentShotgun->getSawUsed());
  auto *simPlayerOne = this;
  auto *simPlayerTwo = this->opponent;
  simPlayerOne->setOpponent(simPlayerTwo);

  auto *simP1 = new SimulatedPlayer(*this);
  auto *simP2 = new SimulatedPlayer(*this->opponent);
  simP1->setOpponent(simP2);
  simP2->setOpponent(simP1);

  auto *initState = new SimulatedGame(simP1, simP2, simShotgun);
  initState->isPlayerOneTurn = true;

  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  std::vector<Action> actionsToTry = determineFeasibleActions(initState);

  for (auto action : actionsToTry) {
    float actionValue;
    if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW) {
      auto *newState = new SimulatedGame(*initState);
      bool turnAfterAction =
          performAction(action, newState, ShellType::LIVE_SHELL);

      newState->isPlayerOneTurn = turnAfterAction;

      actionValue =
          expectiMiniMax(newState, SEARCH_DEPTH - 1, newState->isPlayerOneTurn);
    } else {
      auto nextStates = simulateAction(initState, action);
      SimulatedGame *liveState = nextStates.first;
      SimulatedGame *blankState = nextStates.second;
      float liveVal = expectiMiniMax(liveState, SEARCH_DEPTH - 1,
                                     liveState->isPlayerOneTurn);
      float blankVal = expectiMiniMax(blankState, SEARCH_DEPTH - 1,
                                      blankState->isPlayerOneTurn);
      actionValue = pLive * liveVal + pBlank * blankVal;
    }

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

  // If we can gain reliable information, do it first.
  if (actingPlayer && actingPlayer->hasItem("Magnifying Glass") &&
      !actingPlayer->isNextShellRevealed())
    feasible.push_back(Action::USE_MAGNIFYING_GLASS);

  // Prefer to use Handsaw (if available and not already used) early.
  if (actingPlayer && actingPlayer->hasItem("Handsaw") &&
      !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::USE_HANDSAW);

  // Always consider shooting the opponent.
  feasible.push_back(Action::SHOOT_OPPONENT);

  // If the next shell is revealed as blank, SHOOT_SELF is safe.
  if (actingPlayer && actingPlayer->isNextShellRevealed() &&
      actingPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL)
    feasible.push_back(Action::SHOOT_SELF);

  if (actingPlayer && actingPlayer->hasItem("Beer") &&
      ((actingPlayer->getHealth() <= 2) ||
       (actingPlayer->getItemCount() < MAX_ITEMS)))
    feasible.push_back(Action::DRINK_BEER);

  // Add the healing option if health is not full.
  if (actingPlayer && actingPlayer->hasItem("Cigarette") &&
      maxHealth > actingPlayer->getHealth())
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}
