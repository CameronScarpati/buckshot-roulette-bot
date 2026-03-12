#include "Game.h"
#include "BotPlayer.h"
#include "Items/Beer.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace Color {
const std::string reset = "\033[0m";
const std::string red = "\033[31m";
const std::string green = "\033[32m";
const std::string yellow = "\033[33m";
const std::string blue = "\033[34m";
} // namespace Color

Game::Game(Player *pOne, Player *pTwo, bool playerOneTurn)
    : playerOne(pOne), playerTwo(pTwo), shotgun(std::make_unique<Shotgun>()),
      currentRound(1), playerOneWins(0), playerTwoWins(0),
      isPlayerOneTurn(playerOneTurn) {}

void Game::distributeItems() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<int> itemCountDist(MIN_ITEMS_PER_ROUND,
                                                     MAX_ITEMS_PER_ROUND);
  int itemCount = itemCountDist(gen);

  std::vector<std::string_view> itemTypes = {
      "Cigarette", "Handsaw", "Magnifying Glass", "Beer", "Handcuffs"};

  std::uniform_int_distribution<int> typeDist(
      0, static_cast<int>(itemTypes.size()) - 1);

  // Distribute items to player one
  for (int i = 0; i < itemCount; i++) {
    if (playerOne->getItemCount() < MAX_ITEMS) {
      int index = typeDist(gen);
      auto newItem = Item::createByName(itemTypes[index]);
      if (newItem && playerOne->addItem(std::move(newItem))) {
        std::cout << playerOne->getName()
                  << " received item: " << itemTypes[index] << "\n";
      }
    }
  }

  // Distribute items to player two
  for (int i = 0; i < itemCount; i++) {
    if (playerTwo->getItemCount() < MAX_ITEMS) {
      int index = typeDist(gen);
      auto newItem = Item::createByName(itemTypes[index]);
      if (newItem && playerTwo->addItem(std::move(newItem))) {
        std::cout << playerTwo->getName()
                  << " received item: " << itemTypes[index] << "\n";
      }
    }
  }
}

void Game::printShells() const {
  std::cout << Color::red << "Shotgun was loaded with "
            << shotgun->getLiveShellCount() << " live rounds and "
            << shotgun->getBlankShellCount() << " blank rounds."
            << "\n";
  std::cout << "Best of luck..." << Color::reset << "\n";
}

void Game::printDivider(int width) {
  std::cout << std::string(width, '-') << "\n";
}

void Game::printHeader(const std::string &title, int width) {
  printDivider(width);
  auto padding = (static_cast<size_t>(width) - title.size()) / 2;
  std::cout << std::setw(static_cast<int>(padding + title.size())) << title
            << "\n";
  printDivider(width);
}

bool Game::performAction(Action action) {
  Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
  Player *otherPlayer = isPlayerOneTurn ? playerTwo : playerOne;

  bool turnEnds = true;
  auto *maybeBot = dynamic_cast<BotPlayer *>(currentPlayer);

  switch (action) {
  case Action::SHOOT_SELF: {
    ShellType currentShell = shotgun->getNextShell();
    std::cout << currentPlayer->getName() << " shoots themself."
              << "\n";

    if (currentShell == ShellType::LIVE_SHELL) {
      currentPlayer->loseHealth(shotgun->getSawUsed());
      std::cout << "The shell was live. " << currentPlayer->getName()
                << " lost health."
                << "\n";
    } else {
      std::cout << "The shell was blank. An extra turn was gained."
                << "\n";
      turnEnds = false;
    }
    shotgun->resetSawUsed();
    currentPlayer->resetKnownNextShell();
    break;
  }

  case Action::SHOOT_OPPONENT: {
    ShellType currentShell = shotgun->getNextShell();
    std::cout << currentPlayer->getName() << " shoots "
              << otherPlayer->getName() << "\n";

    if (currentShell == ShellType::LIVE_SHELL) {
      std::cout << "The shell was live. " << otherPlayer->getName()
                << " lost health."
                << "\n";
      otherPlayer->loseHealth(shotgun->getSawUsed());

      if (!otherPlayer->isAlive())
        std::cout << otherPlayer->getName() << " has been eliminated!"
                  << "\n";
      turnEnds = true;
    } else
      std::cout << "The shell was blank."
                << "\n";

    shotgun->resetSawUsed();
    currentPlayer->resetKnownNextShell();
    break;
  }

  case Action::SMOKE_CIGARETTE: {
    if (currentPlayer->useItemByName("Cigarette"))
      turnEnds = false;
    else {
      std::cout << currentPlayer->getName() << " has no Cigarette to smoke."
                << "\n";
      turnEnds = false;
    }
    break;
  }

  case Action::USE_HANDCUFFS: {
    if (!currentPlayer->hasUsedHandcuffsThisTurn() &&
        !otherPlayer->areHandcuffsApplied() &&
        currentPlayer->useItemByName("Handcuffs")) {
      otherPlayer->applyHandcuffs();
      currentPlayer->useHandcuffsThisTurn();
      std::cout << otherPlayer->getName() << " is now handcuffed."
                << "\n";
      turnEnds = false;
    } else {
      std::cout << currentPlayer->getName()
                << " has no Handcuffs or has already used them this turn."
                << "\n";
      turnEnds = false;
    }
    break;
  }

  case Action::USE_MAGNIFYING_GLASS: {
    if (maybeBot)
      maybeBot->setKnownNextShell(shotgun->revealNextShell());

    if (currentPlayer->useItemByName("Magnifying Glass", shotgun.get()))
      turnEnds = false;
    else {
      std::cout << currentPlayer->getName() << " has no Magnifying Glass."
                << "\n";
      turnEnds = false;
    }
    break;
  }

  case Action::DRINK_BEER: {
    if (currentPlayer->useItemByName("Beer", shotgun.get())) {
      // Beer ejects the current shell, so any previously revealed shell
      // knowledge (from Magnifying Glass) is now stale and must be cleared.
      currentPlayer->resetKnownNextShell();
      turnEnds = false;
    } else {
      std::cout << currentPlayer->getName() << " has no Beer."
                << "\n";
      turnEnds = false;
    }
    break;
  }

  case Action::USE_HANDSAW: {
    if (currentPlayer->useItemByName("Handsaw", shotgun.get())) {
      turnEnds = false;
    } else {
      std::cout << currentPlayer->getName() << " has no Handsaw."
                << "\n";
      turnEnds = false;
    }
    break;
  }
  }

  return turnEnds;
}

void Game::determineTurnOrder(bool currentTurnEnded) {
  Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
  Player *otherPlayer = isPlayerOneTurn ? playerTwo : playerOne;

  if (currentTurnEnded) {
    if (otherPlayer->areHandcuffsApplied()) {
      // Opponent is handcuffed: skip their turn, remove cuffs, and reset
      // handcuff usage so the current player can cuff again with a new item
      // on their next turn.  Double-cuffing within the same turn is still
      // blocked by the hasUsedHandcuffsThisTurn() check — this only opens
      // it up once the cuffed skip has been consumed.
      otherPlayer->removeHandcuffs();
      currentPlayer->resetHandcuffUsage();
    } else {
      // Actual turn switch: reset handcuff usage for the departing player.
      currentPlayer->resetHandcuffUsage();
      isPlayerOneTurn = !isPlayerOneTurn;
    }
  }
}

bool Game::checkRoundEnd() const noexcept {
  return !playerOne->isAlive() || !playerTwo->isAlive();
}

bool Game::handleRoundEnd() {
  printDivider(WIDE_DISPLAY_WIDTH);

  if (!playerTwo->isAlive()) {
    std::cout << playerOne->getName() << " wins the round!"
              << "\n";
    playerTwo->removeHandcuffs();
    playerOneWins++;
  } else if (!playerOne->isAlive()) {
    std::cout << playerTwo->getName() << " wins the round!"
              << "\n";
    playerOne->removeHandcuffs();
    playerTwoWins++;
  }

  playerOne->resetHealth();
  playerTwo->resetHealth();

  // Clear transient per-round state so nothing leaks into the next round.
  playerOne->resetKnownNextShell();
  playerTwo->resetKnownNextShell();
  playerOne->resetHandcuffUsage();
  playerTwo->resetHandcuffUsage();

  if (playerOneWins >= ROUNDS_TO_WIN) {
    std::cout << Color::yellow << playerOne->getName()
              << " wins the game by winning " << ROUNDS_TO_WIN << " rounds!"
              << Color::reset << "\n";
    return true;
  } else if (playerTwoWins >= ROUNDS_TO_WIN) {
    std::cout << Color::yellow << playerTwo->getName()
              << " wins the game by winning " << ROUNDS_TO_WIN << " rounds!"
              << Color::reset << "\n";
    return true;
  }

  // Set up next round
  distributeItems();
  shotgun->loadShells();
  std::this_thread::sleep_for(SHELL_LOAD_DELAY);
  printShells();
  currentRound++;

  // Update turn order for the next round
  isPlayerOneTurn = !isPlayerOneTurn;

  std::cout << "\n";
  printHeader("Round " + std::to_string(currentRound), WIDE_DISPLAY_WIDTH);
  std::cout << playerOne->getName() << " has " << playerOneWins
            << " round wins."
            << "\n";
  std::cout << playerTwo->getName() << " has " << playerTwoWins
            << " round wins."
            << "\n";
  printDivider(WIDE_DISPLAY_WIDTH);

  return false; // The Game continues
}

void Game::runGame() {
  printHeader("Buckshot Roulette", WIDE_DISPLAY_WIDTH);
  std::cout << Color::blue << "Good luck to both players!" << Color::reset
            << "\n\n";

  distributeItems();
  shotgun->loadShells();
  std::this_thread::sleep_for(SHELL_LOAD_DELAY);
  printShells();

  while (true) {
    if (checkRoundEnd()) {
      if (handleRoundEnd())
        break;
      continue;
    }

    if (shotgun->isEmpty()) {
      std::cout << "\nShotgun is empty. Reloading...\n";
      // Clear stale shell knowledge before reloading.
      playerOne->resetKnownNextShell();
      playerTwo->resetKnownNextShell();
      distributeItems();
      shotgun->loadShells();
      std::this_thread::sleep_for(SHELL_LOAD_DELAY);
      printShells();
    }

    std::cout << "\n";
    printHeader("Player Status", WIDE_DISPLAY_WIDTH);
    std::cout << *playerOne << "\n";
    std::cout << *playerTwo << "\n";
    printDivider(WIDE_DISPLAY_WIDTH);

    Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
    currentPlayer->printItems();

    std::cout << currentPlayer->getName() << "'s turn:\n";
    Action action = currentPlayer->chooseAction(shotgun.get());
    bool turnEnds = performAction(action);

    determineTurnOrder(turnEnds);
  }

  std::cout << "\n";
  printHeader("Game Over!", WIDE_DISPLAY_WIDTH);
}

Player *Game::getPlayerOne() const noexcept { return playerOne; }

Player *Game::getPlayerTwo() const noexcept { return playerTwo; }

Shotgun *Game::getShotgun() const noexcept { return shotgun.get(); }

bool Game::isPlayerOneTurnNow() const noexcept { return isPlayerOneTurn; }

void Game::changePlayerTurn(bool playerOneTurn) noexcept {
  isPlayerOneTurn = playerOneTurn;
}

int Game::getPlayerOneWins() const noexcept { return playerOneWins; }

int Game::getPlayerTwoWins() const noexcept { return playerTwoWins; }