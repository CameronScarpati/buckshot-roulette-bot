#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H

#include "Game.h"

/**
 * @brief Represents a simulated game state for expectiminimax search.
 *
 * This class extends Game but overrides interactive methods (such as
 * printShells() and runGame()) so that no output or user interaction occurs.
 */
class SimulatedGame : public Game {
public:
  /**
   * @brief Constructs a simulated game instance.
   *
   * This constructor simply calls the base Game constructor.
   *
   * @param p1 Pointer to player one.
   * @param p2 Pointer to player two.
   */
  SimulatedGame(Player *p1, Player *p2, Shotgun *shotgun);

  /**
   * @brief Copy constructor for SimulatedGame.
   * @param other The SimulatedGame instance to copy.
   */
  SimulatedGame(const SimulatedGame &other);

  /**
   * @brief Overrides printShells() to do nothing.
   */
  void printShells() override;

  /**
   * @brief Overrides runGame() to do nothing.
   */
  void runGame() override;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
