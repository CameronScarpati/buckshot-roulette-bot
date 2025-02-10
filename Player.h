#ifndef BUCKSHOT_ROULETTE_BOT_PLAYER_H
#define BUCKSHOT_ROULETTE_BOT_PLAYER_H

#include "Item.h"
#include "Shotgun.h"
#include <array>
#include <string>

/**
 * @brief Defines possible player actions.
 */
enum class Action : int {
  SHOOT_SELF = 0,
  SHOOT_OPPONENT = 1,
  SMOKE_CIGARETTE = 2,      ///< Restores 1 HP.
  USE_HANDCUFFS = 3,        ///< Skips opponent's turn.
  USE_MAGNIFYING_GLASS = 4, ///< Reveals next shell.
  DRINK_BEER = 5,           ///< Ejects current shell.
  USE_HANDSAW = 6           ///< Doubles live round damage.
};

static const int MAX_ITEMS = 8;

/**
 * @brief Represents a player in the game.
 *
 * Manages player health, inventory, and interactions.
 */
class Player {
protected:
  std::string name;                    ///< Player's name.
  int health;                          ///< Current health.
  static int maxHealth;                ///< Maximum player health.
  Player *opponent;                    ///< Pointer to opponent.
  std::array<Item *, MAX_ITEMS> items; ///< Fixed-size inventory.
  int itemCount;                       ///< Number of items held.
  bool handcuffsApplied;               ///< Tracks if handcuffs are applied.

public:
  /**
   * @brief Constructs a player with a name and health.
   * @param name The player's name.
   * @param health The player's starting health.
   */
  Player(std::string name, int health);

  /**
   * @brief Constructs a player with a name, health, and opponent.
   * @param name The player's name.
   * @param health The player's starting health.
   * @param opponent Pointer to the opponent player.
   */
  Player(std::string name, int health, Player *opponent);

  /**
   * @brief Sets the player's opponent.
   * @param opp Pointer to the opponent player.
   */
  void setOpponent(Player *opp);

  /**
   * @brief Determines the player's next action.
   * @param currentShotgun Pointer to the current shotgun state.
   * @return The selected action.
   */
  [[nodiscard("Action needs to be performed.")]] virtual Action
  chooseAction(const Shotgun *currentShotgun) = 0;

  /**
   * @brief Reduces player health, considering saw usage.
   * @param sawUsed Whether a saw was used (causes extra damage).
   */
  void loseHealth(bool sawUsed);

  /**
   * @brief Restores 1 HP by smoking a cigarette.
   */
  void smokeCigarette();

  /**
   * @brief Resets player health to a new value.
   * @param newHealth The new health value.
   */
  void resetHealth(int newHealth);

  /**
   * @brief Resets player health to the default max health.
   */
  void resetHealth();

  /**
   * @brief Checks if the player is still alive.
   * @return True if the player has health remaining.
   */
  bool isAlive() const;

  /**
   * @brief Gets the player's name.
   * @return The player's name as a string.
   */
  std::string getName() const;

  /**
   * @brief Gets the player's current health.
   * @return The player's health value.
   */
  int getHealth() const;

  /**
   * @brief Applies handcuffs to restrict the opponent's next turn.
   */
  void applyHandcuffs();

  /**
   * @brief Removes handcuffs from the opponent.
   */
  void removeHandcuffs();

  /**
   * @brief Checks if handcuffs are currently applied.
   * @return True if handcuffs are active.
   */
  bool areHandcuffsApplied() const;

  /**
   * @brief Adds an item to the player's inventory.
   * @param newItem Pointer to the item.
   * @return True if the item was added, false if inventory is full.
   */
  bool addItem(Item *newItem);

  /**
   * @brief Uses an item from the inventory.
   * @param index The index of the item to use.
   */
  void useItem(int index);

  /**
   * @brief Uses an item with shotgun interaction.
   * @param index The index of the item to use.
   * @param shotgun Pointer to the current shotgun.
   */
  void useItem(int index, Shotgun *shotgun);

  /**
   * @brief Gets the number of items in the inventory.
   * @return The count of held items.
   */
  int getItemCount() const;

  /**
   * @brief Uses an item by name, if found.
   * @param itemName The name of the item.
   * @param shotgun Optional pointer to the shotgun.
   * @return True if the item was found and used.
   */
  bool useItemByName(const std::string &itemName, Shotgun *shotgun = nullptr);

  /**
   * @brief Checks if the player has an item by name.
   * @param itemName The item name to search for.
   * @return True if the item is found.
   */
  bool hasItem(const std::string &itemName) const;

  /**
   * @brief Prints the player's inventory.
   */
  void printItems() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_PLAYER_H
