#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H

#include "Player.h"
#include "Shotgun.h"
#include <iostream>

/**
 * @brief Manages the overall game loop, rounds, and turn order.
 *
 * Inspired by the Steam guide for Buckshot Roulette, this class supports both
 * Human vs. Bot and Bot vs. Bot modes.
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
   *
   * @return true if the round is over, false otherwise.
   */
  bool checkRoundEnd();

public:
  /**
   * @brief Constructs a new Game instance.
   *
   * @param p1 Pointer to player one (human or first bot).
   * @param p2 Pointer to player two (dealer or second bot).
   */
  Game(Player *p1, Player *p2);

  /**
   * @brief Performs an action and updates the game state accordingly.
   *
   * @param action The action to perform.
   */
  void performAction(Action action);

  /**
   * @brief Prints the current shell status of the shotgun.
   */
  void printShells();

  /**
   * @brief Runs the main game loop until one player loses.
   */
  virtual void runGame();
};

#endif // BUCKSHOT_ROULETTE_BOT_GAME_H
