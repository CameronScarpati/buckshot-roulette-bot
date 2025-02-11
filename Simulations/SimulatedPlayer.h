#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H

#include "Player.h"

/**
 * @class SimulatedPlayer
 * @brief A non-interactive player used for game simulations.
 */
class SimulatedPlayer : public Player {
public:
  /**
   * @brief Constructs a simulated player.
   * @param name The player's name.
   * @param health Initial health.
   */
  SimulatedPlayer(const std::string &name, int health);

  /**
   * @brief Constructs a simulated player with an assigned opponent.
   * @param name The player's name.
   * @param health Initial health.
   * @param opponent Pointer to the opponent player.
   */
  SimulatedPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief Copy constructor.
   * @param other The player to copy.
   */
  SimulatedPlayer(const SimulatedPlayer &other);

  /**
   * @brief Constructs a simulated player from a general Player instance.
   * @param other The player to copy.
   */
  explicit SimulatedPlayer(const Player &other);

  /**
   * @brief Automatically selects an action using a simple heuristic.
   *
   * Chooses an action based on health and shell probabilities.
   * This is a placeholder for more advanced AI logic.
   *
   * @param currentShotgun The current shotgun state.
   * @return The chosen action.
   */
  Action chooseAction(const Shotgun *currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
