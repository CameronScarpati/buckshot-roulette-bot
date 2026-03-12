#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H

#include "Player.h"
#include "Shotgun.h"
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>

/**
 * @brief Manages the game loop, rounds, and turn order.
 *
 * Supports Human vs. Bot and Bot vs. Bot modes.
 */
class Game {
protected:
  // Number of round wins required to win the match.
  static constexpr int ROUNDS_TO_WIN = 3;
  // Number of items each player receives per distribution phase.
  static constexpr int MIN_ITEMS_PER_ROUND = 2;
  static constexpr int MAX_ITEMS_PER_ROUND = 5;
  // Default column width for console output formatting.
  static constexpr int DEFAULT_DISPLAY_WIDTH = 50;
  // Wider column width used for round headers and dividers.
  static constexpr int WIDE_DISPLAY_WIDTH = 60;
  // Pause duration (ms) after loading shells for dramatic effect.
  static constexpr std::chrono::milliseconds SHELL_LOAD_DELAY{750};

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
  virtual void printShells() const;

  /**
   * @brief Prints a divider line.
   * @param width The width of the divider line.
   */
  static void printDivider(int width = DEFAULT_DISPLAY_WIDTH);

  /**
   * @brief Prints a centered header with divider lines above and below.
   * @param title The header title.
   * @param width The width of the header.
   */
  static void printHeader(const std::string &title,
                           int width = DEFAULT_DISPLAY_WIDTH);

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

  /**
   * @brief Gets player one's round win count.
   * @return Number of rounds won by player one.
   */
  [[nodiscard]] int getPlayerOneWins() const noexcept;

  /**
   * @brief Gets player two's round win count.
   * @return Number of rounds won by player two.
   */
  [[nodiscard]] int getPlayerTwoWins() const noexcept;
};

#endif // BUCKSHOT_ROULETTE_BOT_GAME_H