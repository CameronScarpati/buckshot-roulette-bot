#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H

#include "Player.h"
#include "Shotgun.h"
#include <functional>
#include <iostream>
#include <memory>
#include <random>

/**
 * @brief Manages the game loop, rounds, and turn order.
 *
 * Supports Human vs. Bot and Bot vs. Bot modes.
 */
class Game {
protected:
  Player *playerOne;                ///< Pointer to player one (non-owning).
  Player *playerTwo;                ///< Pointer to player two (non-owning).
  std::unique_ptr<Shotgun> shotgun; ///< Game's shotgun instance.
  int currentRound;                 ///< Current round number.
  int playerOneWins;                ///< Player one's win count.
  int playerTwoWins;                ///< Player two's win count.
  bool isPlayerOneTurn;             ///< Tracks whose turn it is.

  /**
   * @brief Checks if the round has ended due to player death.
   * @return True if the round is over.
   */
  bool checkRoundEnd() const noexcept;

  /**
   * @brief Handles end of round logic.
   * @return True if game is over, false if another round begins.
   */
  bool handleRoundEnd();

public:
  /**
   * @brief Initializes a new game instance.
   * @param p1 Pointer to player one.
   * @param p2 Pointer to player two.
   * @param isPlayerOneTurn Is it player one's turn?
   */
  Game(Player *p1, Player *p2, bool isPlayerOneTurn);

  /**
   * @brief Virtual destructor.
   */
  virtual ~Game() = default;

  /**
   * @brief Distributes items to players at the start of a round.
   */
  void distributeItems();

  /**
   * @brief Executes the given action and updates the game state.
   * @param action The action to perform.
   * @return True if action ends the player's turn, false otherwise.
   */
  bool performAction(Action action);

  /**
   * @brief Determines the next player's turn.
   * @param currentTurnEnded Whether the current action ends the player's turn.
   */
  void determineTurnOrder(bool currentTurnEnded);

  /**
   * @brief Displays the current shotgun shell status.
   */
  virtual void printShells();

  /**
   * @brief Prints a divider line.
   * @param width The width of the divider line.
   */
  static void printDivider(int width = 50);

  /**
   * @brief Prints a centered header with divider lines above and below.
   * @param title The header title.
   * @param width The width of the header.
   */
  static void printHeader(const std::string &title, int width = 50);

  /**
   * @brief Runs the game loop until a player wins.
   */
  virtual void runGame();

  /**
   * @brief Retrieves the first player.
   * @return Pointer to player one.
   */
  Player *getPlayerOne() const noexcept;

  /**
   * @brief Retrieves the second player.
   * @return Pointer to player two.
   */
  Player *getPlayerTwo() const noexcept;

  /**
   * @brief Retrieves the shotgun instance.
   * @return Pointer to the shotgun.
   */
  Shotgun *getShotgun() const noexcept;

  /**
   * @brief Checks if it's player one's turn.
   * @return True if it's player one's turn, false otherwise.
   */
  bool isPlayerOneTurnNow() const noexcept;

  /**
   * @breif Changes the player turn to the parameter.
   * @param playerOneTurn Player turn boolean.
   */
  void changePlayerTurn(bool playerOneTurn) noexcept;
};

#endif // BUCKSHOT_ROULETTE_BOT_GAME_H