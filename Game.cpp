#include "Game.h"
#include "Items/Beer.h"
#include "Items/Cigarette.h"
#include "Items/Handcuffs.h"
#include "Items/Handsaw.h"
#include "Items/MagnifyingGlass.h"
#include <iostream>
#include <random>
#include <vector>

Game::Game(Player *pOne, Player *pTwo)
    : playerOne(pOne), playerTwo(pTwo), shotgun(new Shotgun()), currentRound(1),
      playerOneWins(0), playerTwoWins(0), isPlayerOneTurn(true) {}

void Game::distributeItems() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> itemCountDist(2, 5);
  // Each player gets an independent random count.
  int itemCount = itemCountDist(gen);

  // Create a list of item factory lambdas.
  std::vector<std::function<Item *()>> itemFactories = {
      []() -> Item * { return new Cigarette(); },
      []() -> Item * { return new Handcuffs(); },
      []() -> Item * { return new MagnifyingGlass(); },
      []() -> Item * { return new Beer(); },
      []() -> Item * { return new Handsaw(); }};
  std::uniform_int_distribution<int> factoryDist(0, itemFactories.size() - 1);

  // Distribute items to player one.
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

  // Distribute items to player two.
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
  std::cout << "Shotgun was loaded with " << shotgun->getLiveShellCount()
            << " live rounds and " << shotgun->getBlankShellCount()
            << " blank rounds." << std::endl;
  std::cout << "Best of luck..." << std::endl;
}

void Game::performAction(Action action) {
  Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
  Player *otherPlayer = isPlayerOneTurn ? playerTwo : playerOne;

  switch (action) {
  case Action::SHOOT_SELF: {
    ShellType currentShell = shotgun->getNextShell();

    std::cout << currentPlayer->getName() + " shoots themself." << std::endl;

    if (currentShell == ShellType::LIVE_SHELL) {
      currentPlayer->loseHealth(shotgun->getSawUsed());
      std::cout << "The shell was live. " << currentPlayer->getName()
                << " lost health." << std::endl;
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
    else
      std::cout << currentPlayer->getName() << " uses Handcuffs on "
                << otherPlayer->getName() << ", skipping their turn."
                << std::endl;
    break;
  }
  case Action::USE_MAGNIFYING_GLASS: {
    if (!currentPlayer->useItemByName("Magnifying Glass", shotgun))
      std::cout << currentPlayer->getName() << " has no Magnifying Glass."
                << std::endl;
    break;
  }
  case Action::DRINK_BEER: {
    if (!currentPlayer->useItemByName("Beer", shotgun))
      std::cout << currentPlayer->getName() << " has no Beer." << std::endl;
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
  std::cout << "Buckshot Roulette game starting. Good luck!" << std::endl;
  distributeItems();
  shotgun->loadShells();
  printShells();

  while (true) {
    if (checkRoundEnd()) {
      if (!playerTwo->isAlive()) {
        std::cout << playerOne->getName() << " wins the round!" << std::endl;
        playerOneWins++;
        playerOne->resetHealth();
        playerTwo->resetHealth();
      } else if (!playerOne->isAlive()) {
        std::cout << playerTwo->getName() << " wins the round!" << std::endl;
        playerTwoWins++;
        playerOne->resetHealth();
        playerTwo->resetHealth();
      }

      if (playerOneWins >= 3) {
        std::cout << playerOne->getName()
                  << " wins the game by winning 3 rounds!" << std::endl;
        break;
      } else if (playerTwoWins >= 3) {
        std::cout << playerTwo->getName()
                  << " wins the game by winning 3 rounds!" << std::endl;
        break;
      }

      distributeItems();
      shotgun->loadShells();
      printShells();
      currentRound++;
      std::cout << "Round reset. Now starting round " << currentRound << "."
                << std::endl;
      std::cout << playerOne->getName() << " has " << playerOneWins
                << " round wins." << std::endl;
      std::cout << playerTwo->getName() << " has " << playerTwoWins
                << " round wins." << std::endl;
    } else if (shotgun->isEmpty()) {
      distributeItems();
      shotgun->loadShells();
      printShells();
    }

    Player *currentPlayer = (isPlayerOneTurn) ? playerOne : playerTwo;
    std::cout << playerOne->getName() << " has " << playerOne->getHealth()
              << " health." << std::endl;
    std::cout << playerTwo->getName() << " has " << playerTwo->getHealth()
              << " health." << std::endl;

    currentPlayer->printItems();

    if (!currentPlayer->areHandcuffsApplied()) {
      Action action = currentPlayer->chooseAction(shotgun);
      performAction(action);
    } else {
      isPlayerOneTurn = !isPlayerOneTurn;
      currentPlayer->removeHandcuffs();
    }
  }

  std::cout << "Game Over!";
}

bool Game::checkRoundEnd() {
  return !playerOne->isAlive() || !playerTwo->isAlive();
}

Player *Game::getPlayerOne() { return playerOne; }

Player *Game::getPlayerTwo() { return playerTwo; }

Shotgun *Game::getShotgun() { return shotgun; }
