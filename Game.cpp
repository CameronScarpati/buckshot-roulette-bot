#include "Game.h"
#include "BotPlayer.h"
#include "Items/Beer.h"
#include "Items/Cigarette.h"
#include "Items/Handcuffs.h"
#include "Items/Handsaw.h"
#include "Items/MagnifyingGlass.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace Color {
const std::string reset = "\033[0m";
const std::string red = "\033[31m";
const std::string green = "\033[32m";
const std::string yellow = "\033[33m";
const std::string blue = "\033[34m";
} // namespace Color

Game::Game(Player *pOne, Player *pTwo)
    : playerOne(pOne), playerTwo(pTwo), shotgun(new Shotgun()), currentRound(1),
      playerOneWins(0), playerTwoWins(0), isPlayerOneTurn(true) {}

void Game::distributeItems() {
  std::uniform_int_distribution<int> itemCountDist(2, 5);
  int itemCount = itemCountDist(gen);
  itemCount = 8;

  std::vector<std::function<Item *()>> itemFactories = {
      []() -> Item * { return new Cigarette(); },
      []() -> Item * { return new Handcuffs(); },
      []() -> Item * { return new MagnifyingGlass(); },
      []() -> Item * { return new Beer(); },
      []() -> Item * { return new Handsaw(); }};
  std::uniform_int_distribution<int> factoryDist(0, itemFactories.size() - 1);

  for (int i = 0; i < itemCount; i++) {
    if (playerOne->getItemCount() < MAX_ITEMS) {
      int index = factoryDist(gen);
      Item *newItem = itemFactories[index]();
      if (playerOne->addItem(newItem)) {
        std::cout << playerOne->getName()
                  << " received item: " << newItem->getName() << std::endl;
      }
    }
  }

  for (int i = 0; i < itemCount; i++) {
    if (playerTwo->getItemCount() < MAX_ITEMS) {
      int index = factoryDist(gen);
      Item *newItem = itemFactories[index]();
      if (playerTwo->addItem(newItem)) {
        std::cout << playerTwo->getName()
                  << " received item: " << newItem->getName() << std::endl;
      }
    }
  }
}

void Game::printShells() {
  std::cout << Color::red << "Shotgun was loaded with "
            << shotgun->getLiveShellCount() << " live rounds and "
            << shotgun->getBlankShellCount() << " blank rounds." << std::endl;
  std::cout << "Best of luck..." << Color::reset << std::endl;
}

void Game::printDivider(int width = 50) {
  std::cout << std::string(width, '-') << std::endl;
}

void Game::printHeader(const std::string &title, int width = 50) {
  printDivider(width);
  int padding = (width - title.size()) / 2;
  std::cout << std::setw(padding + title.size()) << title << std::endl;
  printDivider(width);
}

void Game::performAction(Action action) {
  Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
  Player *otherPlayer = isPlayerOneTurn ? playerTwo : playerOne;

  auto *maybeBot = dynamic_cast<BotPlayer *>(currentPlayer);

  switch (action) {
  case Action::SHOOT_SELF: {
    ShellType currentShell = shotgun->getNextShell();
    std::cout << currentPlayer->getName() + " shoots themself." << std::endl;

    if (currentShell == ShellType::LIVE_SHELL) {
      currentPlayer->loseHealth(shotgun->getSawUsed());
      std::cout << "The shell was live. " << currentPlayer->getName()
                << " lost health." << std::endl;
      if (otherPlayer->areHandcuffsApplied()) {
        otherPlayer->removeHandcuffs();
      } else
        isPlayerOneTurn = !isPlayerOneTurn;
    } else
      std::cout << "The shell was blank. An extra turn was gained."
                << std::endl;
    shotgun->resetSawUsed();
    break;
  }
  case Action::SHOOT_OPPONENT: {
    ShellType currentShell = shotgun->getNextShell();
    std::cout << currentPlayer->getName() + " shoots " + otherPlayer->getName()
              << std::endl;

    if (currentShell == ShellType::LIVE_SHELL) {
      std::cout << "The shell was live. " << otherPlayer->getName()
                << " lost health." << std::endl;
      otherPlayer->loseHealth(shotgun->getSawUsed());
    } else
      std::cout << "The shell was blank." << std::endl;

    if (otherPlayer->areHandcuffsApplied()) {
      otherPlayer->removeHandcuffs();
    } else
      isPlayerOneTurn = !isPlayerOneTurn;

    shotgun->resetSawUsed();
    break;
  }
  case Action::SMOKE_CIGARETTE: {
    if (!currentPlayer->useItemByName("Cigarette"))
      std::cout << currentPlayer->getName() << " has no Cigarette to smoke."
                << std::endl;
    break;
  }
  case Action::USE_HANDCUFFS: {
    if (!currentPlayer->useItemByName("Handcuffs"))
      std::cout << currentPlayer->getName() << " has no Handcuffs."
                << std::endl;
    break;
  }
  case Action::USE_MAGNIFYING_GLASS: {
    if (maybeBot)
      maybeBot->setKnownNextShell(shotgun->revealNextShell());
    if (!currentPlayer->useItemByName("Magnifying Glass", shotgun))
      std::cout << currentPlayer->getName() << " has no Magnifying Glass."
                << std::endl;
    break;
  }
  case Action::DRINK_BEER: {
    if (!currentPlayer->useItemByName("Beer", shotgun))
      std::cout << currentPlayer->getName() << " has no Beer." << std::endl;
    shotgun->resetSawUsed();
    break;
  }
  case Action::USE_HANDSAW: {
    if (!currentPlayer->useItemByName("Handsaw", shotgun))
      std::cout << currentPlayer->getName() << " has no Handsaw." << std::endl;
    break;
  }
  }
}

void Game::runGame() {
  printHeader("Buckshot Roulette", 60);
  std::cout << Color::blue << "Good luck to both players!" << Color::reset
            << std::endl
            << std::endl;

  distributeItems();
  shotgun->loadShells();
  std::this_thread::sleep_for(std::chrono::milliseconds(750));
  printShells();

  while (true) {
    if (checkRoundEnd()) {
      printDivider(60);
      if (!playerTwo->isAlive()) {
        std::cout << playerOne->getName() << " wins the round!" << std::endl;
        playerTwo->removeHandcuffs();
        playerOne->resetHandcuffUsage();
        playerOneWins++;
      } else if (!playerOne->isAlive()) {
        std::cout << playerTwo->getName() << " wins the round!" << std::endl;
        playerOne->removeHandcuffs();
        playerTwo->resetHandcuffUsage();
        playerTwoWins++;
      }
      playerOne->resetHealth();
      playerTwo->resetHealth();

      if (playerOneWins >= 3) {
        std::cout << Color::yellow << playerOne->getName()
                  << " wins the game by winning 3 rounds!" << Color::reset
                  << std::endl;
        break;
      } else if (playerTwoWins >= 3) {
        std::cout << Color::yellow << playerTwo->getName()
                  << " wins the game by winning 3 rounds!" << Color::reset
                  << std::endl;
        break;
      }

      distributeItems();
      shotgun->loadShells();
      std::this_thread::sleep_for(std::chrono::milliseconds(750));
      printShells();
      currentRound++;
      std::cout << std::endl;
      printHeader("Round " + std::to_string(currentRound), 60);
      std::cout << playerOne->getName() << " has " << playerOneWins
                << " round wins." << std::endl;
      std::cout << playerTwo->getName() << " has " << playerTwoWins
                << " round wins." << std::endl;
      printDivider(60);
    } else if (shotgun->isEmpty()) {
      distributeItems();
      shotgun->loadShells();
      std::this_thread::sleep_for(std::chrono::milliseconds(750));
      printShells();
    }

    std::cout << std::endl;
    printHeader("Player Status", 60);
    std::cout << *playerOne << std::endl;
    std::cout << *playerTwo << std::endl;
    printDivider(60);

    Player *currentPlayer = (isPlayerOneTurn) ? playerOne : playerTwo;
    Player *otherPlayer = (isPlayerOneTurn) ? playerTwo : playerOne;

    currentPlayer->printItems();

    Action action = currentPlayer->chooseAction(shotgun);
    performAction(action);
    otherPlayer->resetHandcuffUsage();
  }

  std::cout << std::endl;
  printHeader("Game Over!", 60);
}

bool Game::checkRoundEnd() {
  return !playerOne->isAlive() || !playerTwo->isAlive();
}

Player *Game::getPlayerOne() { return playerOne; }

Player *Game::getPlayerTwo() { return playerTwo; }

Shotgun *Game::getShotgun() { return shotgun; }
