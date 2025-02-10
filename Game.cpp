#include "Game.h"
#include <iostream>

Game::Game(Player *p1, Player *p2)
    : player1(p1), player2(p2), shotgun(Shotgun()), currentRound(1),
      playerOneWins(0), playerTwoWins(0), isPlayerOneTurn(true) {}

void Game::performAction(Action action) {
  switch (action) {
  case Action::SHOOT_SELF: {
    Player *currentPlayer = isPlayerOneTurn ? player1 : player2;
    ShellType currentShell = shotgun.getNextShell();

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
    Player *currentPlayer = isPlayerOneTurn ? player1 : player2;
    Player *otherPlayer = isPlayerOneTurn ? player2 : player1;
    ShellType currentShell = shotgun.getNextShell();

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
  std::cout << "Buckshot Roulette game starting. Good luck!" << std::endl;

  while (true) {
    if (checkRoundEnd()) {
      if (!player2->isAlive()) {
        std::cout << player1->getName() << " wins the round!" << std::endl;
        playerOneWins++;
        player1->resetHealth();
        player2->resetHealth();
      } else if (!player1->isAlive()) {
        std::cout << player2->getName() << " wins the round!" << std::endl;
        playerTwoWins++;
        player1->resetHealth();
        player2->resetHealth();
      }

      if (playerOneWins >= 3) {
        std::cout << player1->getName()
                  << " wins the game by winning 3 consecutive rounds!"
                  << std::endl;
        break;
      } else if (playerTwoWins >= 3) {
        std::cout << player2->getName()
                  << " wins the game by winning 3 consecutive rounds!"
                  << std::endl;
        break;
      }

      shotgun = Shotgun();
      currentRound++;
      std::cout << "Round reset. Now starting round " << currentRound << "."
                << std::endl;
      std::cout << player1->getName() + " has " << playerOneWins
                << " round wins." << std::endl;
      std::cout << player2->getName() + " has " << playerTwoWins
                << " round wins." << std::endl;
    } else if (shotgun.isEmpty()) {
      shotgun = Shotgun();
      continue;
    }

    Player *currentPlayer = (isPlayerOneTurn) ? player1 : player2;
    std::cout << player1->getName() + " has " << player1->getHealth()
              << " health." << std::endl;
    std::cout << player2->getName() + " has " << player2->getHealth()
              << " health." << std::endl;
    Action action = currentPlayer->chooseAction(shotgun);
    performAction(action);
  }

  std::cout << "Game Over!" << std::endl;
  if (player1->isAlive())
    std::cout << player1->getName() << " wins the game!" << std::endl;
  else
    std::cout << player2->getName() << " wins the game!" << std::endl;
}

bool Game::checkRoundEnd() {
  return !player1->isAlive() || !player2->isAlive();
}
