#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H

#include "Game.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"

/**
 * @class SimulatedGame
 * @brief Simulates a game with two simulated players and a simulated shotgun.
 *
 * This class maintains the state of a simulated game including two players,
 * a shotgun, and a flag to indicate whose turn it is.
 */
class SimulatedGame {
public:
  SimulatedPlayer playerOne; ///< The first simulated player.
  SimulatedPlayer playerTwo; ///< The second simulated player.
  SimulatedShotgun shotgun;  ///< The simulated shotgun used in the game.
  bool isPlayerOneTurn; ///< True if it's player one's turn; false otherwise.

public:
  /**
   * @brief Constructs a new SimulatedGame instance.
   *
   * @param playerOne The first simulated player.
   * @param playerTwo The second simulated player.
   * @param shotgun The simulated shotgun for the game.
   * @param isPlayerTurn True if player one's turn; false if player two's turn.
   */
  SimulatedGame(SimulatedPlayer playerOne, SimulatedPlayer playerTwo,
                SimulatedShotgun shotgun, bool isPlayerTurn);
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H