#ifndef BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H

#include "Player.h"

/**
 * @brief This class extends the Player class to define a Human Player.
 */
class HumanPlayer : public Player {
public:
  /**
   * @brief This constructor creates a human player object with name and health.
   * @param name String (Human player identifier).
   * @param health Integer (Health amount).
   */
  HumanPlayer(const std::string &name, int health);

  /**
   * @brief This constructor creates a human player object with name, health and
   * opponent.
   * @param name String (Bot player identifier).
   * @param health Integer (Health amount).
   */
  HumanPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief This function allows the player to choose an action to perform.
   * @param currentShotgun Shotgun (Shotgun state)
   * @return Action that they performed.
   */
  Action chooseAction(const Shotgun &currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
