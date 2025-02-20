#include "BotPlayer.h"
#include <algorithm>
#include <limits>

const int SEARCH_DEPTH = 10;

float BotPlayer::evaluateState(SimulatedGame *state) {
  // Terminal state checks.
  if (!state->getPlayerOne()->isAlive())
    return -10000.0f;
  if (!state->getPlayerTwo()->isAlive())
    return +10000.0f;

  float score = 0.0f;
  float healthWeight = 70.0f;

  // In simulation, player one is the bot.
  auto *botPlayer = state->getPlayerOne();
  auto *oppPlayer = state->getPlayerTwo();
  bool myTurn = state->isPlayerOneTurn;

  // Health difference bonus.
  float hpDifference = static_cast<float>(botPlayer->getHealth()) -
                       static_cast<float>(oppPlayer->getHealth());
  score += hpDifference * healthWeight;

  // Flat turn advantage bonus.
  score += (myTurn ? 50.0f : -50.0f);

  // Shell count bonus: more live rounds improve chances.
  int liveCount = state->getShotgun()->getLiveShellCount();
  score += (myTurn ? 10.0f : -10.0f) * liveCount;

  // Magnifying Glass bonus: reduces uncertainty if next shell unrevealed.
  if (myTurn && !botPlayer->isNextShellRevealed() &&
      botPlayer->hasItem("Magnifying Glass")) {
    float pLive = state->getShotgun()->getLiveShellProbability();
    float pBlank = state->getShotgun()->getBlankShellProbability();
    float uncertainty = pLive * pBlank; // Higher uncertainty = less confidence.
    score += 40.0f * uncertainty;
    if (botPlayer->getHealth() == 1)
      score += 10.0f;
  }

  // Handcuff bonuses:
  // Bonus if opponent is already handcuffed.
  if (oppPlayer->areHandcuffsApplied())
    score += 150.0f;
  // Bonus for having a handcuff item available (and not used this turn).
  if (botPlayer->hasItem("Handcuffs") && !botPlayer->hasUsedHandcuffsThisTurn())
    score += 40.0f;

  // Cigarette bonus: each missing HP (if it's our turn) increases score.
  int missingHealth = Player::maxHealth - botPlayer->getHealth();
  if (myTurn && botPlayer->hasItem("Cigarette"))
    score += missingHealth * 20.0f;

  // Penalize very low health.
  if (botPlayer->getHealth() == 1)
    score -= 90.0f;

  // If the next shell is revealed, adjust score based on its known type.
  if (botPlayer->isNextShellRevealed()) {
    float handSawMultiplier = (botPlayer->hasItem("Handsaw")) ? 2.25f : 1.0f;
    if (botPlayer->returnKnownNextShell() == ShellType::LIVE_SHELL) {
      // Larger bonus if opponent's health is low.
      float bonus = (oppPlayer->getHealth() <= 2) ? 100.0f : 50.0f;
      score += myTurn ? bonus * handSawMultiplier : -bonus * handSawMultiplier;
    } else {
      score += myTurn ? 25.0f * handSawMultiplier : -25.0f;
    }
  }

  return score;
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
      // If opponent is handcuffed, remove them and keep turn.
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
    // If opponent is handcuffed, remove them and keep turn.
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
    return expectiMiniMax(newState, depth - 1, newState->isPlayerOneTurn);
  } else if (pLive == 1.0f) {
    SimulatedGame *liveState = simulateLiveAction(state, action);
    return expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
  } else if (pBlank == 1.0f) {
    SimulatedGame *blankState = simulateBlankAction(state, action);
    return expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
  }
  auto nextStates = simulateAction(state, action);
  SimulatedGame *liveState = nextStates.first;
  SimulatedGame *blankState = nextStates.second;
  float liveVal =
      expectiMiniMax(liveState, depth - 1, liveState->isPlayerOneTurn);
  float blankVal =
      expectiMiniMax(blankState, depth - 1, blankState->isPlayerOneTurn);
  return pLive * liveVal + pBlank * blankVal;
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
  if (pLive == 0.0f)
    return Action::SHOOT_SELF;
  else if (pLive == 1.0f)
    return liveShellAction(currentShotgun);

  // Prefer healing if needed.
  if (health < maxHealth && hasItem("Cigarette"))
    return Action::SMOKE_CIGARETTE;

  // Build an initial simulated state.
  auto *simShotgun = new SimulatedShotgun(
      currentShotgun->getTotalShellCount(), currentShotgun->getLiveShellCount(),
      currentShotgun->getBlankShellCount(), currentShotgun->getSawUsed());
  // Create simulated players.
  auto *simP1 = new SimulatedPlayer(*this);
  auto *simP2 = new SimulatedPlayer(*this->opponent);
  simP1->setOpponent(simP2);
  simP2->setOpponent(simP1);

  auto *initState = new SimulatedGame(simP1, simP2, simShotgun);
  initState->isPlayerOneTurn = true;

  float bestValue = -std::numeric_limits<float>::infinity();
  Action bestAction = Action::SHOOT_OPPONENT;

  // Determine all feasible actions from this state.
  std::vector<Action> actionsToTry = determineFeasibleActions(initState);

  // Evaluate each action using expectiminimax.
  for (auto action : actionsToTry) {
    float actionValue;
    if (action == Action::SMOKE_CIGARETTE || action == Action::USE_HANDSAW ||
        action == Action::USE_HANDCUFFS) {
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
      !state->getShotgun()->getSawUsed())
    feasible.push_back(Action::USE_HANDSAW);

  // If the next shell is revealed as blank, SHOOT_SELF is safe.
  if (actingPlayer && actingPlayer->isNextShellRevealed() &&
      actingPlayer->returnKnownNextShell() == ShellType::BLANK_SHELL)
    feasible.push_back(Action::SHOOT_SELF);

  // Consider drinking Beer if health is low or inventory is nearly full.
  if (actingPlayer && actingPlayer->hasItem("Beer") &&
      ((actingPlayer->getHealth() <= 2) ||
       (actingPlayer->getItemCount() < MAX_ITEMS)))
    feasible.push_back(Action::DRINK_BEER);

  // Consider healing if health is not full.
  if (actingPlayer && actingPlayer->hasItem("Cigarette") &&
      (Player::maxHealth > actingPlayer->getHealth()))
    feasible.push_back(Action::SMOKE_CIGARETTE);

  return feasible;
}
