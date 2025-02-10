#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H

/**
 * @brief This class extends the Player class to define a Human Player.
 */
class SimulatedPlayer {
private:
  int health;

public:
  /**
   * @brief This constructor creates a human player object with
   */
  SimulatedPlayer();

  /**
   * @brief This constructor creates a human player object with name and health.
   * @param name String (Human player identifier).
   * @param health Integer (Health amount).
   */
  explicit SimulatedPlayer(int health);

  void loseHealth(bool sawUsed);

  int getHealth() const;

  bool isDead() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDPLAYER_H
