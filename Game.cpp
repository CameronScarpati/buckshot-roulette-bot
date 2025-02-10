#include "Game.h"
#include <iostream>

Game::Game(Player *pOne, Player *pTwo)
    : playerOne(pOne), playerTwo(pTwo), shotgun(new Shotgun()), currentRound(1),
      playerOneWins(0), playerTwoWins(0), isPlayerOneTurn(true) {
  std::cout << "Buckshot Roulette game starting. Good luck!" << std::endl;
  printShells();
}

void Game::printShells() {
  std::cout << "Shotgun was loaded with " << shotgun->getLiveShellCount()
            << " live rounds and " << shotgun->getBlankShellCount()
            << " blank rounds." << std::endl;
  std::cout << "Best of luck..." << std::endl;
}

void Game::performAction(Action action) {
  switch (action) {
  case Action::SHOOT_SELF: {
    Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
    ShellType currentShell = shotgun->getNextShell();

    std::cout << currentPlayer->getName() + " shoots themself." << std::endl;

    if (currentShell == ShellType::LIVE_SHELL) {
      currentPlayer->loseHealth(false);
      std::cout << "The shell was live. " << currentPlayer->getName()
                << " lost health." << std::endl;

      isPlayerOneTurn = !isPlayerOneTurn;
    } else
      std::cout << "The shell was blank. An extra turn was gained."
                << std::endl;

    break;
  }
  case Action::SHOOT_OPPONENT: {
    Player *currentPlayer = isPlayerOneTurn ? playerOne : playerTwo;
    Player *otherPlayer = isPlayerOneTurn ? playerTwo : playerOne;
    ShellType currentShell = shotgun->getNextShell();

    std::cout << currentPlayer->getName() + " shoots " + otherPlayer->getName()
              << std::endl;

    if (currentShell == ShellType::LIVE_SHELL) {
      std::cout << "The shell was live. " << otherPlayer->getName()
                << " lost health." << std::endl;
      otherPlayer->loseHealth(false);
    } else
      std::cout << "The shell was blank." << std::endl;

    isPlayerOneTurn = !isPlayerOneTurn;

    break;
  }
  }
}

void Game::runGame() {
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

      shotgun->loadShells();

      printShells();
      currentRound++;
      std::cout << "Round reset. Now starting round " << currentRound << "."
                << std::endl;
      std::cout << playerOne->getName() + " has " << playerOneWins
                << " round wins." << std::endl;
      std::cout << playerTwo->getName() + " has " << playerTwoWins
                << " round wins." << std::endl;
    } else if (shotgun->isEmpty()) {
      shotgun->loadShells();
      printShells();
    }

    Player *currentPlayer = (isPlayerOneTurn) ? playerOne : playerTwo;
    std::cout << playerOne->getName() + " has " << playerOne->getHealth()
              << " health." << std::endl;
    std::cout << playerTwo->getName() + " has " << playerTwo->getHealth()
              << " health." << std::endl;
    Action action = currentPlayer->chooseAction(shotgun);
    performAction(action);
  }

  std::cout << "Game Over!" << std::endl;
}

bool Game::checkRoundEnd() {
  return !playerOne->isAlive() || !playerTwo->isAlive();
}
