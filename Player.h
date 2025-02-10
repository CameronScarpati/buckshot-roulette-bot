#ifndef BUCKSHOT_ROULETTE_BOT_PLAYER_H
#define BUCKSHOT_ROULETTE_BOT_PLAYER_H

#include "Shotgun.h"
#include <string>

/**
 * @brief Defines the possible actions a player can perform.
 */
enum class Action : int { SHOOT_SELF = 0, SHOOT_OPPONENT = 1 };

/**
 * @brief Represents a player in the game.
 *
 * This class defines the attributes and actions of a player, including health
 * management, opponent interactions, and decision-making.
 */
class Player {
protected:
  std::string name;     ///< Player's name.
  int health;           ///< Current health of the player.
  static int maxHealth; ///< Maximum health for all players.
  Player *opponent;     ///< Pointer to the opponent player.

public:
  /**
   * @brief Constructs a Player with a given name and health.
   * @param name The player's name.
   * @param health The initial health of the player.
   */
  Player(std::string name, int health);

  /**
   * @brief Constructs a Player with a given name, health, and opponent.
   * @param name The player's name.
   * @param health The initial health of the player.
   * @param opponent Pointer to the opponent player.
   */
  Player(std::string name, int health, Player *opponent);

  /**
   * @brief Sets the opponent for this player.
   * @param opp Pointer to the opponent player.
   */
  void setOpponent(Player *opp);

  /**
   * @brief Allows the player to choose an action.
   * @param currentShotgun Pointer to the current shotgun state.
   * @return The selected action.
   */
  [[nodiscard("Action needs to be performed.")]] virtual Action
  chooseAction(const Shotgun *currentShotgun) = 0;

  /**
   * @brief Reduces the player's health when they get shot.
   * @param sawUsed True if a saw item was used for double damage.
   */
  void loseHealth(bool sawUsed);

  /**
   * @brief Heals the player by restoring one health point.
   */
  void useStimulant();

  /**
   * @brief Sets a new maximum health value and resets the player's health.
   * @param newHealth The new maximum health value.
   */
  void resetHealth(int newHealth);

  /**
   * @brief Resets the player's health to the current maximum.
   */
  void resetHealth();

  /**
   * @brief Checks if the player is still alive.
   * @return True if the player is alive, false otherwise.
   */
  bool isAlive() const;

  /**
   * @brief Retrieves the player's name.
   * @return The player's name as a string.
   */
  std::string getName() const;

  /**
   * @brief Retrieves the player's current health.
   * @return The player's health as an integer.
   */
  int getHealth() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_PLAYER_H
