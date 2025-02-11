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
  float hpDifference =
      state->getPlayerOne()->getHealth() - state->getPlayerTwo()->getHealth();
  score += hpDifference * healthWeight;

  float shellsWeight = 15.0f;
  float liveRounds = state->getShotgun()->getLiveShellCount();
  float blankRounds = state->getShotgun()->getBlankShellCount();
  score += (blankRounds - liveRounds) * shellsWeight;

  bool myTurn = state->isPlayerOneTurn;
  auto *currentPlayer =
      (myTurn) ? state->getPlayerOne() : state->getPlayerTwo();

  if (currentPlayer->isNextShellRevealed()) {
    if (currentPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL) {
      if (myTurn)
        score += 50.0f;
      else
        score += 15.0f;
    } else {
      if (myTurn)
        score -= 25.0f;
      else
        score -= 50.0f;
    }
  }

  if (myTurn && !currentPlayer->isNextShellRevealed() &&
      currentPlayer->hasItem("Magnifying Glass")) {
    float pLive = state->getShotgun()->getLiveShellProbability();
    float pBlank = 1.0f - pLive;

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

    if (myHP > 1 && pBlank > 0.6f)
      score += 10.0f;
  }

  return score;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());
  if (!simShotgun)
    throw std::logic_error("Shotgun instance is not a SimulatedShotgun!");

  auto *currentPlayer =
      (state->isPlayerOneTurn) ? state->getPlayerOne() : state->getPlayerTwo();
  auto *otherPlayer =
      (!state->isPlayerOneTurn) ? state->getPlayerOne() : state->getPlayerTwo();

  switch (action) {
  case Action::SHOOT_SELF:
    if (shell == ShellType::LIVE_SHELL) {
      simShotgun->simulateLiveShell();
      currentPlayer->loseHealth(false);
      return !state->isPlayerOneTurn;
    } else {
      simShotgun->simulateBlankShell();
      return state->isPlayerOneTurn;
    }

  case Action::SHOOT_OPPONENT:
    if (shell == ShellType::LIVE_SHELL) {
      otherPlayer->loseHealth(false);
      simShotgun->simulateLiveShell();
    } else {
      simShotgun->simulateBlankShell();
    }
    return !state->isPlayerOneTurn;

  case Action::USE_MAGNIFYING_GLASS:
    if (!currentPlayer->hasItem("Magnifying Glass"))
      return state->isPlayerOneTurn;
    return state->isPlayerOneTurn;

    // TODO: Implement other items (cigarettes, handcuffs, beer, handsaw).
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
    auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());
    if (simShotgun && !simShotgun->isNextShellRevealed()) {
      auto *liveReveal = new SimulatedGame(*state);
      bool turnAfterLive = performAction(Action::USE_MAGNIFYING_GLASS,
                                         liveReveal, ShellType::LIVE_SHELL);
      liveReveal->isPlayerOneTurn = turnAfterLive;

      auto *blankReveal = new SimulatedGame(*state);
      bool turnAfterBlank = performAction(Action::USE_MAGNIFYING_GLASS,
                                          blankReveal, ShellType::BLANK_SHELL);
      blankReveal->isPlayerOneTurn = turnAfterBlank;

      return {liveReveal, blankReveal};
    } else {
      auto *singleState = new SimulatedGame(*state);
      bool newTurn = performAction(Action::USE_MAGNIFYING_GLASS, singleState,
                                   ShellType::BLANK_SHELL);
      singleState->isPlayerOneTurn = newTurn;
      return {singleState, singleState};
    }
  }

  SimulatedGame *liveState = simulateLiveAction(state, action);
  SimulatedGame *blankState = simulateBlankAction(state, action);
  return {liveState, blankState};
}

float BotPlayer::expectiMiniMax(SimulatedGame *state, int depth,
                                bool maximizingPlayer) {
  if (depth == 0 || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty())
    return evaluateState(state);

  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  if (maximizingPlayer) {
    float bestValue = -std::numeric_limits<float>::infinity();

    std::vector<Action> actionsToTry = determineFeasibleActions(state);

    for (auto action : actionsToTry) {
      if (pLive == 1.0f) {
        SimulatedGame *liveState = simulateLiveAction(state, action);
        float val =
            expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
        bestValue = std::max(bestValue, val);
      } else if (pBlank == 1.0f) {
        SimulatedGame *blankState = simulateBlankAction(state, action);
        float val =
            expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
        bestValue = std::max(bestValue, val);
      } else {
        auto nextStates = simulateAction(state, action);
        SimulatedGame *liveState = nextStates.first;
        SimulatedGame *blankState = nextStates.second;

        float liveVal =
            expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
        float blankVal =
            expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);

        float expectedVal = pLive * liveVal + pBlank * blankVal;
        bestValue = std::max(bestValue, expectedVal);
      }
    }

    return bestValue;
  } else {
    float bestValue = std::numeric_limits<float>::infinity();

    std::vector<Action> actionsToTry = determineFeasibleActions(state);

    for (auto action : actionsToTry) {
      if (pLive == 1.0f) {
        SimulatedGame *liveState = simulateLiveAction(state, action);
        float val =
            expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
        bestValue = std::min(bestValue, val);
      } else if (pBlank == 1.0f) {
        SimulatedGame *blankState = simulateBlankAction(state, action);
        float val =
            expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
        bestValue = std::min(bestValue, val);
      } else {
        auto nextStates = simulateAction(state, action);
        SimulatedGame *liveState = nextStates.first;
        SimulatedGame *blankState = nextStates.second;

        float liveVal =
            expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
        float blankVal =
            expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);

        float expectedVal = pLive * liveVal + pBlank * blankVal;
        bestValue = std::min(bestValue, expectedVal);
      }
    }

    return bestValue;
  }
}

BotPlayer::BotPlayer(const std::string &name, int health)
    : Player(name, health) {}

BotPlayer::BotPlayer(const std::string &name, int health, Player *opponent)
    : Player(name, health, opponent) {}

Action BotPlayer::chooseAction(const Shotgun *currentShotgun) {
  if (isNextShellRevealed()) {
    nextShellRevealed = false;
    if (returnKnownNextShell() == ShellType::LIVE_SHELL)
      return Action::SHOOT_OPPONENT;
    else
      return Action::SHOOT_SELF;
  }

  float pLive = currentShotgun->getLiveShellProbability();
  float pBlank = currentShotgun->getBlankShellProbability();

  if (pLive == 0.0f)
    return Action::SHOOT_SELF;
  else if (pLive == 1.0f)
    return Action::SHOOT_OPPONENT;

  auto *simShotgun = new SimulatedShotgun(currentShotgun->getTotalShellCount(),
                                          currentShotgun->getLiveShellCount(),
                                          currentShotgun->getBlankShellCount());
  auto *simPlayerOne = this;
  auto *simPlayerTwo = this->opponent;
  simPlayerOne->setOpponent(simPlayerTwo);

  auto *simP1 = new SimulatedPlayer(*this);
  auto *simP2 = new SimulatedPlayer(*this->opponent);
  simP1->setOpponent(simP2);
  simP2->setOpponent(simP1);

  auto *initState = new SimulatedGame(simP1, simP2, simShotgun);
  initState->isPlayerOneTurn = true;

  int searchDepth = 8;
  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  std::vector<Action> actionsToTry = determineFeasibleActions(initState);

  for (auto action : actionsToTry) {
    auto nextStates = simulateAction(initState, action);
    SimulatedGame *liveState = nextStates.first;
    SimulatedGame *blankState = nextStates.second;

    float liveVal =
        expectiMiniMax(liveState, searchDepth - 1, liveState->isPlayerOneTurn);
    float blankVal = expectiMiniMax(blankState, searchDepth - 1,
                                    blankState->isPlayerOneTurn);

    float actionValue = pLive * liveVal + pBlank * blankVal;
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
  std::vector<Action> feasible = {Action::SHOOT_SELF, Action::SHOOT_OPPONENT};

  if (actingPlayer && actingPlayer->hasItem("Magnifying Glass"))
    feasible.push_back(Action::USE_MAGNIFYING_GLASS);

  return feasible;
}
