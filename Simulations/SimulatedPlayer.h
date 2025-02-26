#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H

#include "Player.h"
#include <memory>

/**
 * @class SimulatedPlayer
 * @brief A non-interactive player used for game simulations.
 */
class SimulatedPlayer final : public Player {
public:
  /**
   * @brief Constructs a simulated player.
   * @param name The player's name.
   * @param health Initial health.
   */
  SimulatedPlayer(std::string name, int health);

  /**
   * @brief Constructs a simulated player with an assigned opponent.
   * @param name The player's name.
   * @param health Initial health.
   * @param opponent Pointer to the opponent player.
   */
  SimulatedPlayer(std::string name, int health, Player *opponent);

  /**
   * @brief Copy constructor.
   * @param other The player to copy.
   */
  SimulatedPlayer(const SimulatedPlayer &other);

  /**
   * @brief Move constructor.
   * @param other The player to move from.
   */
  SimulatedPlayer(SimulatedPlayer &&other) noexcept;

  /**
   * @brief Copy assignment operator.
   * @param other The player to copy.
   * @return Reference to this instance.
   */
  SimulatedPlayer &operator=(const SimulatedPlayer &other);

  /**
   * @brief Move assignment operator.
   * @param other The player to move from.
   * @return Reference to this instance.
   */
  SimulatedPlayer &operator=(SimulatedPlayer &&other) noexcept;

  /**
   * @brief Constructs a simulated player from a general Player instance.
   * @param other The player to copy.
   */
  explicit SimulatedPlayer(const Player &other);

  /**
   * @brief Deleted method - SimulatedPlayers don't make decisions.
   * @param currentShotgun The current shotgun state.
   * @return Never returns.
   * @throws std::logic_error Always.
   */
  [[noreturn]] Action chooseAction(Shotgun *currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H