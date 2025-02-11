#include "BotPlayer.h"
#include <algorithm>
#include <limits>

float BotPlayer::evaluateState(SimulatedGame *state) {
  // If we're simulating from the bot's perspective, assume Bot is Player One.
  // If your code has Bot as Player Two, invert references appropriately.

  // Quick check for terminal states:
  if (!state->getPlayerOne()->isAlive())
    return -50.0f; // Bot is dead
  if (!state->getPlayerTwo()->isAlive())
    return 50.0f; // Opponent is dead

  // Increase the base healthWeight to discourage self-harm
  float healthWeight = 12.0f;

  // Health difference: (Bot's HP) - (Opponent's HP), times a weighting factor.
  float healthDiff = static_cast<float>(state->getPlayerOne()->getHealth() -
                                        state->getPlayerTwo()->getHealth()) *
                     healthWeight;

  // Extra penalty if bot is at 1 HP => strongly discourage self-shooting
  float lowHealthPenalty = 0.0f;
  if (state->getPlayerOne()->getHealth() == 1) {
    lowHealthPenalty -= 30.0f;
  }

  // Slight bonus if it is currently the bot’s turn
  // (i.e., can act right now, possibly with new items).
  float turnBonus = (state->isPlayerOneTurn) ? 5.0f : 0.0f;

  // Sum them up for final evaluation
  return healthDiff + lowHealthPenalty + turnBonus;
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
    } else {
      simShotgun->simulateBlankShell();
    }
    return !state->isPlayerOneTurn;

  case Action::USE_MAGNIFYING_GLASS:
    if (!currentPlayer->hasItem("Magnifying Glass"))
      return state->isPlayerOneTurn;
    currentPlayer->useItemByName("Magnifying Glass", simShotgun);

    if (!simShotgun->isNextShellRevealed())
      simShotgun->setRevealedNextShell(shell);

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
      !state->getPlayerTwo()->isAlive() || state->getShotgun()->isEmpty()) {
    return evaluateState(state);
  }

  float pLive = state->getShotgun()->getLiveShellProbability();
  float pBlank = state->getShotgun()->getBlankShellProbability();

  if (maximizingPlayer) {
    float bestValue = -std::numeric_limits<float>::infinity();

    Action actionsToTry[3] = {Action::SHOOT_SELF, Action::SHOOT_OPPONENT,
                              Action::USE_MAGNIFYING_GLASS};

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

    Action actionsToTry[3] = {Action::SHOOT_SELF, Action::SHOOT_OPPONENT,
                              Action::USE_MAGNIFYING_GLASS};

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
    : Player(name, health), nextShellRevealed(false),
      knownNextShell(ShellType::BLANK_SHELL) {}

BotPlayer::BotPlayer(const std::string &name, int health, Player *opponent)
    : Player(name, health, opponent), nextShellRevealed(false),
      knownNextShell(ShellType::BLANK_SHELL) {}

Action BotPlayer::chooseAction(const Shotgun *currentShotgun) {
  if (nextShellRevealed) {
    nextShellRevealed = false;
    if (knownNextShell == ShellType::LIVE_SHELL) {
      return Action::SHOOT_OPPONENT;
    } else
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
  auto *simPlayerOne = new SimulatedPlayer(this->getName(), this->getHealth());
  auto *simPlayerTwo = new SimulatedPlayer(
      this->opponent->getName(), this->opponent->getHealth(), simPlayerOne);
  simPlayerOne->setOpponent(simPlayerTwo);
  auto *initState = new SimulatedGame(simPlayerOne, simPlayerTwo, simShotgun);
  initState->isPlayerOneTurn = true;

  int searchDepth = 5;
  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  std::vector<Action> actionsToTry = {Action::SHOOT_SELF,
                                      Action::SHOOT_OPPONENT};

  if (this->hasItem("Magnifying Glass"))
    actionsToTry.push_back(Action::USE_MAGNIFYING_GLASS);

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

void BotPlayer::setKnownNextShell(ShellType nextShell) {
  knownNextShell = nextShell;
  nextShellRevealed = true;
}

bool BotPlayer::isNextShellRevealed() const { return nextShellRevealed; }

ShellType BotPlayer::returnKnownNextShell() const { return knownNextShell; }
