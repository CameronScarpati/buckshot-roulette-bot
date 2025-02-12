#include "BotPlayer.h"
#include <algorithm>
#include <limits>

float BotPlayer::evaluateState(SimulatedGame *state) {
  if (!state->getPlayerOne()->isAlive())
    return -10000.0f;
  if (!state->getPlayerTwo()->isAlive())
    return +10000.0f;

  float score = 0.0f;

  float healthWeight = 70.0f;
  auto hpDifference = static_cast<float>(state->getPlayerOne()->getHealth()) -
                      static_cast<float>(state->getPlayerTwo()->getHealth());
  score += hpDifference * healthWeight;

  bool myTurn = state->isPlayerOneTurn;
  auto *currentPlayer =
      (myTurn) ? state->getPlayerOne() : state->getPlayerTwo();

  if (currentPlayer->isNextShellRevealed()) {
    float handSawMultiplier = 1.0f;
    if (currentPlayer->hasItem("Handsaw"))
      handSawMultiplier *= 3;

    if (currentPlayer->returnKnownNextShell() == ShellType::LIVE_SHELL) {
      if (myTurn)
        score += 50.0f * handSawMultiplier;
      else
        score -= 50.0f * handSawMultiplier;
    } else {
      if (myTurn)
        score += 25.0f * handSawMultiplier;
      else
        score -= 25.0f;
    }
  }

  if (myTurn && !currentPlayer->isNextShellRevealed() &&
      currentPlayer->hasItem("Magnifying Glass")) {
    float pLive = state->getShotgun()->getLiveShellProbability();
    float pBlank = state->getShotgun()->getBlankShellProbability();

    float uncertainty = pLive * pBlank;
    score += 25.0f * uncertainty;

    if (state->getPlayerOne()->getHealth() == 1)
      score += 10.0f;
  }

  if (state->getPlayerOne()->getHealth() == 1)
    score -= 40.0f;

  if (myTurn) {
    float pBlank = state->getShotgun()->getBlankShellProbability();
    int myHP = state->getPlayerOne()->getHealth();

    if (myHP > 1 && pBlank > 0.7f)
      score += 10.0f;
  }

  return score;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());
  if (!simShotgun)
    throw std::runtime_error("This is not a simulated shotgun.");

  auto *currentPlayer =
      (state->isPlayerOneTurn) ? state->getPlayerOne() : state->getPlayerTwo();
  auto *otherPlayer =
      (!state->isPlayerOneTurn) ? state->getPlayerOne() : state->getPlayerTwo();

  switch (action) {
  case Action::SHOOT_SELF:
    currentPlayer->resetKnownNextShell();
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
    if (shell == ShellType::LIVE_SHELL) {
      otherPlayer->loseHealth(simShotgun->getSawUsed());
      simShotgun->simulateLiveShell();
    } else
      simShotgun->simulateBlankShell();
    return !state->isPlayerOneTurn;

  case Action::USE_MAGNIFYING_GLASS:
    if (!currentPlayer->hasItem("Magnifying Glass"))
      return state->isPlayerOneTurn;

    currentPlayer->setKnownNextShell(shell);
    return state->isPlayerOneTurn;

  case Action::SMOKE_CIGARETTE:
    if (!currentPlayer->hasItem("Cigarette"))
      return state->isPlayerOneTurn;

    currentPlayer->smokeCigarette();
    return state->isPlayerOneTurn;

  case Action::DRINK_BEER:
    currentPlayer->resetKnownNextShell();
    if (!currentPlayer->hasItem("Beer"))
      return state->isPlayerOneTurn;
    return state->isPlayerOneTurn;

  case Action::USE_HANDSAW:
    if (!currentPlayer->hasItem("Handsaw"))
      return state->isPlayerOneTurn;

    state->getShotgun()->useHandsaw();
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
    auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());
    if (!simShotgun)
      throw std::runtime_error("This is not a simulated shotgun.");

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

  if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW) {
    auto *newState = new SimulatedGame(*state);
    bool turnAfterAction =
        performAction(action, newState, ShellType::LIVE_SHELL);
    newState->isPlayerOneTurn = turnAfterAction;
    return expectiMiniMax(newState, depth - 1, newState->isPlayerOneTurn);
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

Action BotPlayer::chooseAction(Shotgun *currentShotgun) {
  if (health < maxHealth && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  if (isNextShellRevealed()) {
    resetKnownNextShell();
    if (returnKnownNextShell() == ShellType::LIVE_SHELL) {
      if (currentShotgun->getSawUsed() || !hasItem("Handsaw") ||
          opponent->getHealth() == 1)
        return Action::SHOOT_OPPONENT;
      return Action::USE_HANDSAW;
    } else
      return Action::SHOOT_SELF;
  }

  float pLive = currentShotgun->getLiveShellProbability();
  float pBlank = currentShotgun->getBlankShellProbability();

  if (pLive == 0.0f)
    return Action::SHOOT_SELF;
  else if (pLive == 1.0f) {
    if (currentShotgun->getSawUsed() || !hasItem("Handsaw") ||
        opponent->getHealth() == 1)
      return Action::SHOOT_OPPONENT;
    return Action::USE_HANDSAW;
  }

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

  int searchDepth = 6;
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
          expectiMiniMax(newState, searchDepth - 1, newState->isPlayerOneTurn);
    } else {
      auto nextStates = simulateAction(initState, action);
      SimulatedGame *liveState = nextStates.first;
      SimulatedGame *blankState = nextStates.second;
      float liveVal = expectiMiniMax(liveState, searchDepth - 1,
                                     liveState->isPlayerOneTurn);
      float blankVal = expectiMiniMax(blankState, searchDepth - 1,
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
  std::vector<Action> feasible = {Action::SHOOT_OPPONENT};

  if (actingPlayer && actingPlayer->getHealth() > 1 &&
      !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::SHOOT_SELF);
  if (actingPlayer->hasItem("Magnifying Glass"))
    feasible.push_back(Action::USE_MAGNIFYING_GLASS);
  if (actingPlayer->hasItem("Beer"))
    feasible.push_back(Action::DRINK_BEER);
  if (actingPlayer->hasItem("Handsaw") && !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::USE_HANDSAW);
  if (actingPlayer->hasItem("Cigarette") &&
      maxHealth > actingPlayer->getHealth())
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}
