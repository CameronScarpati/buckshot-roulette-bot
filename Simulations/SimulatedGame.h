#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H

#include "Game.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"

/**
 * @class SimulatedGame
 * @brief A game simulation used for expectiminimax search.
 *
 * This class extends Game but disables interactive elements.
 */
class SimulatedGame : public Game {
public:
  /**
   * @brief Constructs a simulated game.
   * @param p1 Pointer to player one.
   * @param p2 Pointer to player two.
   * @param shotgun Pointer to the shotgun.
   * @param isPlayerOneTurn Is it player one's turn?
   */
  SimulatedGame(SimulatedPlayer *p1, SimulatedPlayer *p2,
                SimulatedShotgun *shotgun, bool isPlayerOneTurn);

  /**
   * @brief Copy constructor.
   * @param other The SimulatedGame instance to copy.
   */
  SimulatedGame(const SimulatedGame &other);

  /**
   * @brief Disables shell printing.
   */
  void printShells() override;

  /**
   * @brief Disables game execution.
   */
  void runGame() override;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
