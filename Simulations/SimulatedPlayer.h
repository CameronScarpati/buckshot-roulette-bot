#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H

#include "Player.h"

/**
 * @brief Represents a simulated player in the game.
 *
 * This class extends the Player class and implements an automated decision
 * process for simulation purposes.
 */
class SimulatedPlayer : public Player {
public:
  /**
   * @brief Constructs a SimulatedPlayer with a given name and health.
   * @param name The player's name.
   * @param health The player's starting health.
   */
  SimulatedPlayer(const std::string &name, int health);

  /**
   * @brief Constructs a SimulatedPlayer with a given name, health, and
   * opponent.
   * @param name The player's name.
   * @param health The player's starting health.
   * @param opponent Pointer to the opponent player.
   */
  SimulatedPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief Automatically chooses an action based on a simple simulation
   * heuristic.
   *
   * This implementation uses a basic heuristic:
   * - If health is below maximum and a Cigarette is available, use it.
   * - Otherwise, if all shells are live (pLive == 1.0), shoot the opponent.
   * - Otherwise, if all shells are blank (pLive == 0.0), shoot self.
   * - Otherwise, default to shooting the opponent.
   *
   * This method can later be replaced by a more sophisticated simulation-based
   * algorithm.
   *
   * @param currentShotgun Pointer to the current shotgun state.
   * @return The selected action.
   */
  Action chooseAction(const Shotgun *currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
