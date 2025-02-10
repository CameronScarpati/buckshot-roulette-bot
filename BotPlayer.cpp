#include "BotPlayer.h"
#include <algorithm>
#include <limits>

float BotPlayer::evaluateState(SimulatedGame *state) {
  if (!state->getPlayerOne()->isAlive())
    return -50.0f;
  if (!state->getPlayerTwo()->isAlive())
    return 50.0f;

  float healthWeight = 2.0f;
  float healthDiff = static_cast<float>((state->getPlayerOne()->getHealth() -
                                         state->getPlayerTwo()->getHealth())) *
                     healthWeight;
  float lowHealthPenalty = 0.0f;
  if (state->getPlayerOne()->getHealth() == 1)
    lowHealthPenalty -= 3.0f;

  return healthDiff + lowHealthPenalty;
}

bool BotPlayer::performAction(Action action, SimulatedGame *state,
                              ShellType shell) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(state->getShotgun());
  if (!simShotgun)
    throw std::logic_error("Shotgun instance is not a SimulatedShotgun!");

  auto *currentPlayer = dynamic_cast<SimulatedPlayer *>(
      state->isPlayerOneTurn ? state->getPlayerOne() : state->getPlayerTwo());
  auto *otherPlayer = dynamic_cast<SimulatedPlayer *>(
      state->isPlayerOneTurn ? state->getPlayerTwo() : state->getPlayerOne());

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
    } else
      simShotgun->simulateBlankShell();
    return !state->isPlayerOneTurn;

  case Action::USE_MAGNIFYING_GLASS:

    return state->isPlayerOneTurn;

    // TODO Implement items.
    //  case Action::SMOKE_CIGARETTE:S
    //    // ...
    //    break;
    //  case Action::USE_HANDCUFFS:
    //    // ...
    //    break;
    //  case Action::DRINK_BEER:
    //    // ...
    //    break;
    //  case Action::USE_HANDSAW:
    //    // ...
    //    break;
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
  SimulatedGame *liveState = simulateLiveAction(state, action);
  SimulatedGame *blankState = simulateBlankAction(state, action);
  return {liveState, blankState};
}

float BotPlayer::expectiMiniMax(SimulatedGame *state, int depth,
                                bool maximizingPlayer) {
  if (depth == 0 || !state->getPlayerOne()->isAlive() ||
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty()) {
    return evaluateState(state);
  }

  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  if (maximizingPlayer) {
    float bestValue = -std::numeric_limits<float>::infinity();

    for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
      auto action = static_cast<Action>(actionIndex);

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

    for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
      auto action = static_cast<Action>(actionIndex);

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
  float pLive = currentShotgun->getLiveShellProbability();
  float pBlank = currentShotgun->getBlankShellProbability();

  if (pLive == 0.0f) {
    return Action::SHOOT_SELF;
  } else if (pLive == 1.0f)
    return Action::SHOOT_OPPONENT;

  auto *simShotgun = new SimulatedShotgun(currentShotgun->getTotalShellCount(),
                                          currentShotgun->getLiveShellCount(),
                                          currentShotgun->getBlankShellCount());
  auto *simPlayerOne = new SimulatedPlayer(this->getName(), this->getHealth());
  auto *simPlayerTwo = new SimulatedPlayer(
      this->opponent->getName(), this->opponent->getHealth(), simPlayerOne);
  simPlayerOne->setOpponent(simPlayerTwo);
  auto *initState = new SimulatedGame(simPlayerOne, simPlayerTwo, simShotgun);
  initState->isPlayerOneTurn = true;

  int searchDepth = 5;
  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
    auto action = static_cast<Action>(actionIndex);

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
