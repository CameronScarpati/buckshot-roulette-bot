#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H

#include "Player.h"
#include "Shotgun.h"
#include <iostream>

/**
 * @brief The Game class manages the overall game loop, rounds, and turn order.
 * Inspired by the Steam guide for Buckshot Roulette,
 * this implementation supports both Human vs. Bot and Bot vs. Bot modes.
 */
class Game {
private:
  Player *playerOne;
  Player *playerTwo;
  Shotgun *shotgun;
  int currentRound;
  int playerOneWins;
  int playerTwoWins;
  bool isPlayerOneTurn;

  /**
   * @brief Checks if the current round is over.
   * @return Boolean (True/False - Over/Not Over).
   */
  bool checkRoundEnd();

public:
  /**
   * @brief Constructs a new Game instance.
   * @param p1 Pointer to playerOne (the human or first bot).
   * @param p2 Pointer to playerTwo (the Dealer or second bot).
   */
  Game(Player *p1, Player *p2);

  /**
   * @brief Changes the game state for an action performed.
   * @param action Action (To be performed).
   */
  void performAction(Action action);

  void printShells();

  /**
   * @brief Runs the main game loop until one player loses.
   */
  virtual void runGame();
};

#endif // BUCKSHOT_ROULETTE_BOT_GAME_H
