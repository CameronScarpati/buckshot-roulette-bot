#include "Game.h"
#include "BotPlayer.h"
#include "Items/Beer.h"
#include "Items/Cigarette.h"
#include "Items/Handcuffs.h"
#include "Items/Handsaw.h"
#include "Items/MagnifyingGlass.h"
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

Game::Game(Player *pOne, Player *pTwo, bool isPlayerOneTurn)
    : playerOne(pOne), playerTwo(pTwo), shotgun(std::make_unique<Shotgun>()),
      currentRound(1), playerOneWins(0), playerTwoWins(0),
      isPlayerOneTurn(isPlayerOneTurn) {}

void Game::distributeItems() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<int> itemCountDist(2, 5);
  int itemCount = itemCountDist(gen);

  std::vector<std::string_view> itemTypes = {
      "Cigarette", "Handsaw", "Magnifying Glass", "Beer", "Handcuffs"};

  std::uniform_int_distribution<int> typeDist(0, itemTypes.size() - 1);

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

void Game::printShells() {
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
  int padding = (width - title.size()) / 2;
  std::cout << std::setw(padding + title.size()) << title << "\n";
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
    currentPlayer->resetHandcuffUsage();

    if (otherPlayer->areHandcuffsApplied()) {
      std::cout << otherPlayer->getName()
                << " is handcuffed and skips their turn!"
                << "\n";
      otherPlayer->removeHandcuffs();
    } else
      isPlayerOneTurn = !isPlayerOneTurn;
  }
}

bool Game::checkRoundEnd() const noexcept {
  return !playerOne->isAlive() || !playerTwo->isAlive();
}

bool Game::handleRoundEnd() {
  printDivider(60);
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

  if (playerOneWins >= 3) {
    std::cout << Color::yellow << playerOne->getName()
              << " wins the game by winning 3 rounds!" << Color::reset << "\n";
    return true;
  } else if (playerTwoWins >= 3) {
    std::cout << Color::yellow << playerTwo->getName()
              << " wins the game by winning 3 rounds!" << Color::reset << "\n";
    return true;
  }

  // Set up next round
  distributeItems();
  shotgun->loadShells();
  std::this_thread::sleep_for(std::chrono::milliseconds(750));
  printShells();
  currentRound++;

  std::cout << "\n";
  printHeader("Round " + std::to_string(currentRound), 60);
  std::cout << playerOne->getName() << " has " << playerOneWins
            << " round wins."
            << "\n";
  std::cout << playerTwo->getName() << " has " << playerTwoWins
            << " round wins."
            << "\n";
  printDivider(60);

  return false; // The Game continues
}

void Game::runGame() {
  printHeader("Buckshot Roulette", 60);
  std::cout << Color::blue << "Good luck to both players!" << Color::reset
            << "\n\n";

  distributeItems();
  shotgun->loadShells();
  std::this_thread::sleep_for(std::chrono::milliseconds(750));
  printShells();

  while (true) {
    if (checkRoundEnd()) {
      if (handleRoundEnd())
        break;
      continue;
    }

    if (shotgun->isEmpty()) {
      std::cout << "\nShotgun is empty. Reloading...\n";
      distributeItems();
      shotgun->loadShells();
      std::this_thread::sleep_for(std::chrono::milliseconds(750));
      printShells();
    }

    std::cout << "\n";
    printHeader("Player Status", 60);
    std::cout << *playerOne << "\n";
    std::cout << *playerTwo << "\n";
    printDivider(60);

    Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
    currentPlayer->printItems();

    std::cout << currentPlayer->getName() << "'s turn:\n";
    Action action = currentPlayer->chooseAction(shotgun.get());
    bool turnEnds = performAction(action);

    determineTurnOrder(turnEnds);
  }

  std::cout << "\n";
  printHeader("Game Over!", 60);
}

Player *Game::getPlayerOne() const noexcept { return playerOne; }

Player *Game::getPlayerTwo() const noexcept { return playerTwo; }

Shotgun *Game::getShotgun() const noexcept { return shotgun.get(); }

bool Game::isPlayerOneTurnNow() const noexcept { return isPlayerOneTurn; }

void Game::changePlayerTurn(bool playerOneTurn) noexcept {
  isPlayerOneTurn = playerOneTurn;
}