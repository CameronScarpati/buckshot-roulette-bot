#ifndef BUCKSHOT_ROULETTE_BOT_PLAYER_H
#define BUCKSHOT_ROULETTE_BOT_PLAYER_H

#include "Items/Item.h"
#include "Shotgun.h"
#include <array>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

/**
 * @enum Action
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
 * @class Player
 * @brief Represents a player with health, inventory, and game actions.
 */
class Player {
protected:
  std::string name;          ///< Player's name.
  int health;                ///< Current health.
  static int maxHealth;      ///< Maximum player health.
  Player *opponent;          ///< Pointer to opponent.
  std::vector<Item *> items; ///< Inventory.
  int itemCount;             ///< Number of items held.
  bool handcuffsApplied;     ///< Whether handcuffs are applied.
  bool nextShellRevealed;    ///< Indicates if the next shell has been revealed.
  ShellType knownNextShell =
      ShellType::BLANK_SHELL; ///< Stores the revealed shell type.
  bool handcuffsUsedThisTurn; ///< Tracks if handcuffs were used this turn.

public:
  /**
   * @brief Constructs a player.
   * @param name Player's name.
   * @param health Starting health.
   */
  Player(std::string name, int health);

  /**
   * @brief Constructs a player with an assigned opponent.
   * @param name Player's name.
   * @param health Starting health.
   * @param opponent Pointer to the opponent.
   */
  Player(std::string name, int health, Player *opponent);

  /**
   * @brief Copy constructor.
   * @param other The player to copy.
   */
  Player(const Player &other);

  /**
   * @brief Copy assignment operator.
   * @param other The player to copy.
   * @return A reference to this instance.
   */
  Player &operator=(const Player &other);

  /**
   * @brief Sets the player's opponent.
   * @param opp Pointer to the opponent.
   */
  void setOpponent(Player *opp);

  /**
   * @brief Determines the player's next action.
   * @param currentShotgun Pointer to the current shotgun.
   * @return The chosen action.
   */
  [[nodiscard]] virtual Action chooseAction(Shotgun *currentShotgun) = 0;

  /**
   * @brief Reduces player health.
   * @param sawUsed If true, deals extra damage.
   */
  void loseHealth(bool sawUsed);

  /**
   * @brief Restores 1 HP.
   */
  void smokeCigarette();

  /**
   * @brief Resets player health to a new value.
   * @param newHealth The new health value.
   */
  void resetHealth(int newHealth);

  /**
   * @brief Resets player health to the default max.
   */
  void resetHealth();

  /**
   * @brief Checks if the player is alive.
   * @return True if health is above 0.
   */
  bool isAlive() const;

  /**
   * @brief Gets the player's name.
   * @return Player's name.
   */
  std::string getName() const;

  /**
   * @brief Gets the player's current health.
   * @return Player's health.
   */
  int getHealth() const;

  /**
   * @brief Applies handcuffs to the opponent.
   */
  void applyHandcuffs();

  /**
   * @brief Removes handcuffs.
   */
  void removeHandcuffs();

  /**
   * @brief Checks if handcuffs are applied.
   * @return True if active.
   */
  bool areHandcuffsApplied() const;

  /**
   * @brief Sets the known next shell type.
   * @param nextShell The shell type revealed.
   */
  void setKnownNextShell(ShellType nextShell);

  /**
   * @brief Checks if the next shell has been revealed.
   * @return True if revealed, false otherwise.
   */
  bool isNextShellRevealed() const;

  /**
   * @brief Returns the known next shell type.
   * @return The revealed shell type.
   */
  ShellType returnKnownNextShell() const;

  /**
   * @brief Resets the known next shell type.
   */
  void resetKnownNextShell();

  /**
   * @brief Adds an item to the inventory.
   * @param newItem Pointer to the item.
   * @return True if added, false if full.
   */
  bool addItem(Item *newItem);

  /**
   * @brief Uses an item by index.
   * @param index The item index.
   */
  void useItem(int index);

  /**
   * @brief Uses an item with shotgun interaction.
   * @param index The item index.
   * @param shotgun Pointer to the shotgun.
   */
  void useItem(int index, Shotgun *shotgun);

  /**
   * @brief Gets the number of held items.
   * @return Item count.
   */
  int getItemCount() const;

  /**
   * @brief Uses an item by name.
   * @param itemName The item name.
   * @param shotgun Optional shotgun pointer.
   * @return True if used.
   */
  bool useItemByName(const std::string &itemName, Shotgun *shotgun = nullptr);

  /**
   * @brief Removes an item by name.
   * @param itemName The item name.
   */
  void removeItemByName(const std::string &itemName);

  /**
   * @brief Checks if the player has a specific item.
   * @param itemName The item name.
   * @return True if found.
   */
  bool hasItem(const std::string &itemName) const;

  /**
   * @brief Returns the number of items matching a given name.
   * @param itemName The item name.
   * @return Number of matching items.
   */
  int countItem(const std::string &itemName) const;

  /**
   * @brief Gets the player's inventory as a vector of item pointers.
   * @return A vector containing pointers to the items in the player's
   * inventory.
   */
  std::vector<Item *> getItems() const;

  /**
   * @brief Prints the player's inventory.
   */
  void printItems() const;

  /**
   * @brief Resets the flag allowing the player to use handcuffs again.
   */
  void resetHandcuffUsage();

  /**
   * @brief Prevents the player from using handcuffs again in the current turn.
   */
  void useHandcuffsThisTurn();

  /**
   * @brief Determines whether the player has already used handcuffs this turn.
   * @return True if handcuffs have been used this turn, false otherwise.
   */
  bool hasUsedHandcuffsThisTurn() const;
};

/**
 * @brief Overloads the << operator to provide formatted output for a Player.
 *
 * This operator outputs the player's name (left-justified, with a field width
 * of 15) followed by the player's current health (with a field width of 3) in a
 * formatted style.
 *
 * @param os The output stream.
 * @param player The Player object to output.
 * @return std::ostream& The output stream.
 */
std::ostream &operator<<(std::ostream &os, const Player &player);

#endif // BUCKSHOT_ROULETTE_BOT_PLAYER_H
