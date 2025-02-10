#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H

/**
 * @class SimulatedPlayer
 * @brief Represents a simulated player with health management.
 *
 * This class provides methods to manage a player's health, including decreasing
 * health and checking if the player is dead.
 */
class SimulatedPlayer {
private:
  int health; ///< The current health of the simulated player.

public:
  /**
   * @brief Constructs a new SimulatedPlayer object with default health.
   */
  SimulatedPlayer();

  /**
   * @brief Constructs a new SimulatedPlayer object with specified health.
   * @param health The initial health value for the player.
   */
  explicit SimulatedPlayer(int health);

  /**
   * @brief Reduces the player's health.
   *
   * Decreases the player's health based on whether a saw was used.
   * @param sawUsed Indicates if a saw was used, which may affect the amount of
   * health lost.
   */
  void loseHealth(bool sawUsed);

  /**
   * @brief Retrieves the player's current health.
   * @return The current health value.
   */
  int getHealth() const;

  /**
   * @brief Determines whether the player is dead.
   *
   * A player is considered dead if their health is zero or below.
   * @return true if the player is dead, false otherwise.
   */
  bool isDead() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
