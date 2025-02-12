#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H

#include "Player.h"
#include "Simulations/SimulatedShotgun.h"
#include <iomanip>
#include <iostream>

/**
 * @brief Manages the game loop, rounds, and turn order.
 *
 * Supports Human vs. Bot and Bot vs. Bot modes.
 */
class Game {
protected:
  Player *playerOne; ///< Pointer to player one.
  Player *playerTwo; ///< Pointer to player two.
  Shotgun *shotgun;  ///< Game's shotgun instance.
  int currentRound;  ///< Current round number.
  int playerOneWins; ///< Player one's win count.
  int playerTwoWins; ///< Player two's win count.

  /**
   * @brief Checks if the round has ended.
   * @return True if the round is over.
   */
  bool checkRoundEnd();

public:
  bool isPlayerOneTurn; ///< Tracks whose turn it is.

  /**
   * @brief Initializes a new game instance.
   * @param p1 Pointer to player one.
   * @param p2 Pointer to player two.
   */
  Game(Player *p1, Player *p2);

  /**
   * @brief Distributes items to players at the start of the game.
   */
  void distributeItems();

  /**
   * @brief Executes the given action and updates the game state.
   * @param action The action to perform.
   */
  void performAction(Action action);

  /**
   * @brief Displays the current shotgun shell status.
   */
  virtual void printShells();

  /**
   * @brief Prints a divider line.
   * @param width The width of the divider line.
   */
  void printDivider(int width);

  /**
   * @brief Prints a centered header with divider lines above and below.
   * @param title The header title.
   * @param width The width of the header.
   */
  void printHeader(const std::string &title, int width);
  
  /**
   * @brief Runs the game loop until a player loses.
   */
  virtual void runGame();

  /**
   * @brief Retrieves the first player.
   * @return Pointer to player one.
   */
  Player *getPlayerOne();

  /**
   * @brief Retrieves the second player.
   * @return Pointer to player two.
   */
  Player *getPlayerTwo();

  /**
   * @brief Retrieves the shotgun instance.
   * @return Pointer to the shotgun.
   */
  Shotgun *getShotgun();
};

#endif // BUCKSHOT_ROULETTE_BOT_GAME_H
