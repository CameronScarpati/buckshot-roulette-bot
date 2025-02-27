#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H

#include "Game.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"
#include <memory>

/**
 * @class SimulatedGame
 * @brief A game simulation used for expectiminimax search.
 *
 * This class extends Game but disables interactive elements.
 */
class SimulatedGame final : public Game {
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
   * @throws std::logic_error If other.shotgun is not a SimulatedShotgun
   */
  SimulatedGame(const SimulatedGame &other);

  /**
   * @brief Move constructor.
   * @param other The SimulatedGame instance to move from.
   */
  SimulatedGame(SimulatedGame &&other) noexcept;

  /**
   * @brief Copy assignment operator.
   * @param other The SimulatedGame instance to copy.
   * @return Reference to this instance.
   */
  SimulatedGame &operator=(const SimulatedGame &other);

  /**
   * @brief Move assignment operator.
   * @param other The SimulatedGame instance to move from.
   * @return Reference to this instance.
   */
  SimulatedGame &operator=(SimulatedGame &&other) noexcept;

  /**
   * @brief Destructor to clean up allocated memory.
   */
  ~SimulatedGame() override;

  /**
   * @brief Disables shell printing for simulations.
   * @throws std::logic_error Always.
   */
  void printShells() override;

  /**
   * @brief Disables game execution for simulations.
   * @throws std::logic_error Always.
   */
  void runGame() override;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H