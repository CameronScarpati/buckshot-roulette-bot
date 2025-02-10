#include "BotPlayer.h"
#include <algorithm>
#include <limits>

float BotPlayer::evaluateState(const SimulatedGame &state) {
  if (state.playerTwo.isDead())
    return 50.0f;
  if (state.playerOne.isDead())
    return -50.0f;

  float healthDiff = static_cast<float>(state.playerOne.getHealth()) -
                     static_cast<float>(state.playerTwo.getHealth());
  float shellBonus =
      static_cast<float>(state.shotgun.getTotalShellCount()) * 0.5f;

  return healthDiff + shellBonus;
}

bool BotPlayer::performAction(Action action, bool isPlayerOneTurn,
                              SimulatedPlayer &playerOne,
                              SimulatedPlayer &playerTwo,
                              SimulatedShotgun &shotgun, ShellType shell) {
  if (shell == ShellType::LIVE_SHELL) {
    shotgun.getLiveShell();
  } else
    shotgun.getBlankShell();

  SimulatedPlayer &currentPlayer = (isPlayerOneTurn ? playerOne : playerTwo);
  SimulatedPlayer &otherPlayer = (isPlayerOneTurn ? playerTwo : playerOne);

  switch (action) {
  case Action::SHOOT_SELF:
    if (shell == ShellType::LIVE_SHELL) {
      currentPlayer.loseHealth(false);
      return !isPlayerOneTurn;
    } else
      return isPlayerOneTurn;

  case Action::SHOOT_OPPONENT:
    if (shell == ShellType::LIVE_SHELL)
      otherPlayer.loseHealth(false);

    return !isPlayerOneTurn;
  case Action::SMOKE_CIGARETTE:
    break;
  case Action::USE_HANDCUFFS:
    break;
  case Action::USE_MAGNIFYING_GLASS:
    break;
  case Action::DRINK_BEER:
    break;
  case Action::USE_HANDSAW:
    break;
  }
  return !isPlayerOneTurn;
}

SimulatedGame BotPlayer::simulateLiveAction(const SimulatedGame &state,
                                            Action action) {
  SimulatedGame nextState = state;
  bool newTurn = performAction(action, nextState.isPlayerOneTurn,
                               nextState.playerOne, nextState.playerTwo,
                               nextState.shotgun, ShellType::LIVE_SHELL);

  nextState.isPlayerOneTurn = newTurn;

  return nextState;
}

SimulatedGame BotPlayer::simulateBlankAction(const SimulatedGame &state,
                                             Action action) {
  SimulatedGame nextState = state;
  bool newTurn = performAction(action, nextState.isPlayerOneTurn,
                               nextState.playerOne, nextState.playerTwo,
                               nextState.shotgun, ShellType::BLANK_SHELL);

  nextState.isPlayerOneTurn = newTurn;

  return nextState;
}

std::pair<SimulatedGame, SimulatedGame>
BotPlayer::simulateAction(const SimulatedGame &state, Action action) {
  SimulatedGame liveState = simulateLiveAction(state, action);
  SimulatedGame blankState = simulateBlankAction(state, action);

  return {liveState, blankState};
}

float BotPlayer::expectiMiniMax(const SimulatedGame &state, int depth,
                                bool maximizingPlayer) {
  if (depth == 0 || state.playerOne.isDead() || state.playerTwo.isDead() ||
      state.shotgun.isEmpty()) {
    return evaluateState(state);
  }

  float pLive = state.shotgun.getLiveShellProbability();
  float pBlank = state.shotgun.getBlankShellProbability();

  if (maximizingPlayer) { // Maximize outcomes.
    float bestValue = -std::numeric_limits<float>::infinity();

    for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
      auto action = static_cast<Action>(actionIndex);

      if (pLive == 1.0f) { // Guaranteed live round.
        const SimulatedGame &liveState = simulateLiveAction(state, action);
        float val =
            expectiMiniMax(liveState, depth - 1, liveState.isPlayerOneTurn);

        bestValue = std::max(bestValue, val);
      } else if (pBlank == 1.0f) { // Guaranteed blank round.
        const SimulatedGame &blankState = simulateBlankAction(state, action);
        float val =
            expectiMiniMax(blankState, depth - 1, blankState.isPlayerOneTurn);

        bestValue = std::max(bestValue, val);
      } else {
        auto nextStates = simulateAction(state, action);
        const SimulatedGame &liveState = nextStates.first;
        const SimulatedGame &blankState = nextStates.second;

        float liveVal =
            expectiMiniMax(liveState, depth - 1, liveState.isPlayerOneTurn);
        float blankVal =
            expectiMiniMax(blankState, depth - 1, blankState.isPlayerOneTurn);

        float expectedVal = pLive * liveVal + pBlank * blankVal;
        bestValue = std::max(bestValue, expectedVal);
      }
    }
    return bestValue;
  } else { // Minimize outcomes.
    float bestValue = std::numeric_limits<float>::infinity();

    for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
      auto action = static_cast<Action>(actionIndex);

      if (pLive == 1.0f) { // Guaranteed live round.
        const SimulatedGame &liveState = simulateLiveAction(state, action);
        float val =
            expectiMiniMax(liveState, depth - 1, liveState.isPlayerOneTurn);

        bestValue = std::min(bestValue, val);
      } else if (pBlank == 1.0f) { // Guaranteed blank round.
        const SimulatedGame &blankState = simulateBlankAction(state, action);
        float val =
            expectiMiniMax(blankState, depth - 1, blankState.isPlayerOneTurn);

        bestValue = std::min(bestValue, val);
      } else {
        // Mixed probability
        auto nextStates = simulateAction(state, action);
        const SimulatedGame &liveState = nextStates.first;
        const SimulatedGame &blankState = nextStates.second;

        float liveVal =
            expectiMiniMax(liveState, depth - 1, liveState.isPlayerOneTurn);
        float blankVal =
            expectiMiniMax(blankState, depth - 1, blankState.isPlayerOneTurn);

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

  SimulatedShotgun simShotgun(currentShotgun->getTotalShellCount(),
                              currentShotgun->getLiveShellCount(),
                              currentShotgun->getBlankShellCount());

  SimulatedPlayer simPlayerOne(this->getHealth());
  SimulatedPlayer simPlayerTwo(this->opponent->getHealth());

  SimulatedGame initState(simPlayerOne, simPlayerTwo, simShotgun, true);

  int searchDepth = 5;
  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_SELF;

  for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
    auto action = static_cast<Action>(actionIndex);

    auto nextStates = simulateAction(initState, action);
    const SimulatedGame &liveState = nextStates.first;
    const SimulatedGame &blankState = nextStates.second;

    float liveVal =
        expectiMiniMax(liveState, searchDepth - 1, liveState.isPlayerOneTurn);
    float blankVal =
        expectiMiniMax(blankState, searchDepth - 1, blankState.isPlayerOneTurn);

    float actionValue = pLive * liveVal + pBlank * blankVal;

    if (actionValue > bestValue) {
      bestValue = actionValue;
      bestAction = action;
    }
  }

  return bestAction;
}
