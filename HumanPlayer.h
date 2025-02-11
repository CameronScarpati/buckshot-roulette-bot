#ifndef BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H

#include "Player.h"

/**
 * @brief Represents a human player in the game.
 *
 * This class extends the Player class and allows human input for
 * decision-making.
 */
class HumanPlayer : public Player {
public:
  /**
   * @brief Constructs a HumanPlayer with a given name and health.
   * @param name The player's name.
   * @param health The initial health of the player.
   */
  HumanPlayer(const std::string &name, int health);

  /**
   * @brief Constructs a HumanPlayer with a given name, health, and opponent.
   * @param name The player's name.
   * @param health The initial health of the player.
   * @param opponent Pointer to the opponent player.
   */
  HumanPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief Allows the human player to choose an action.
   * @param currentShotgun Pointer to the current shotgun state.
   * @return The selected action.
   */
  Action chooseAction(Shotgun *currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
