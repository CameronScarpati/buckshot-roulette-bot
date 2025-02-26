#ifndef BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H

#include "Player.h"
#include <string>

/**
 * @brief Represents a human player in the game.
 *
 * This class extends the Player class and allows human input for
 * decision-making.
 */
class HumanPlayer final : public Player {
public:
  /**
   * @brief Constructs a HumanPlayer with a given name and health.
   * @param name The player's name.
   * @param health The initial health of the player.
   */
  HumanPlayer(std::string name, int health);

  /**
   * @brief Constructs a HumanPlayer with a given name, health, and opponent.
   * @param name The player's name.
   * @param health The initial health of the player.
   * @param opponent Pointer to the opponent player.
   */
  HumanPlayer(std::string name, int health, Player *opponent);

  /**
   * @brief Allows the human player to choose an action via console input.
   * @param currentShotgun Pointer to the current shotgun state.
   * @return The selected action.
   */
  [[nodiscard]] Action chooseAction(Shotgun *currentShotgun) override;

private:
  /**
   * @brief Displays the available actions to the player.
   */
  static void displayActions();

  /**
   * @brief Validates if an action can be taken based on current inventory.
   * @param action The action to validate.
   * @return True if the action is valid.
   */
  [[nodiscard]] bool isValidAction(Action action) const;
};

#endif // BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H