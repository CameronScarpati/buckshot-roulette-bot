#ifndef BUCKSHOT_ROULETTE_BOT_PLAYER_H
#define BUCKSHOT_ROULETTE_BOT_PLAYER_H

#include "Items/Item.h"
#include "Shotgun.h"
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * @enum class Action
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

static constexpr int MAX_ITEMS = 8;

/**
 * @class Player
 * @brief Represents a player with health, inventory, and game actions.
 */
class Player {
protected:
  std::string name;     ///< Player's name.
  int health;           ///< Current health.
  static int maxHealth; ///< Maximum player health.
  Player *opponent;     ///< Pointer to opponent (non-owning).
  std::vector<std::unique_ptr<Item>> items; ///< Inventory (owned).
  bool handcuffsApplied = false;            ///< Whether handcuffs are applied.
  bool nextShellRevealed =
      false; ///< Indicates if the next shell has been revealed.
  ShellType knownNextShell =
      ShellType::BLANK_SHELL; ///< Stores the revealed shell type.
  bool handcuffsUsedThisTurn =
      false; ///< Tracks if handcuffs were used this turn.

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
   * @param opponent Pointer to the opponent (non-owning).
   */
  Player(std::string name, int health, Player *opponent);

  /**
   * @brief Virtual destructor for proper polymorphic destruction.
   */
  virtual ~Player() = default;

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
   * @brief Move constructor.
   * @param other The player to move from.
   */
  Player(Player &&other) noexcept;

  /**
   * @brief Move assignment operator.
   * @param other The player to move from.
   * @return A reference to this instance.
   */
  Player &operator=(Player &&other) noexcept;

  /**
   * @brief Sets the player's opponent.
   * @param opp Pointer to the opponent (non-owning).
   */
  void setOpponent(Player *opp) noexcept;

  /**
   * @brief Determines the player's next action.
   * @param currentShotgun Pointer to the current shotgun.
   * @return The chosen action.
   */
  [[nodiscard]] virtual Action chooseAction(Shotgun *currentShotgun) = 0;

  /**
   * @brief Reduces player health.
   * @param sawUsed If true, deals extra damage.
   * @throws std::runtime_error If the player is already dead.
   */
  void loseHealth(bool sawUsed);

  /**
   * @brief Restores 1 HP if below maximum.
   */
  void smokeCigarette() noexcept;

  /**
   * @brief Resets player health to a new value.
   * @param newHealth The new health value.
   */
  static void resetMaxHealth(int newHealth) noexcept;

  /**
   * @brief Resets player health to the default max.
   */
  void resetHealth() noexcept;

  /**
   * @brief Checks if the player is alive.
   * @return True if health is above 0.
   */
  [[nodiscard]] bool isAlive() const noexcept;

  /**
   * @brief Gets the player's name.
   * @return Player's name.
   */
  [[nodiscard]] std::string_view getName() const noexcept;

  /**
   * @brief Gets the player's current health.
   * @return Player's health.
   */
  [[nodiscard]] int getHealth() const noexcept;

  /**
   * @brief Gets the maximum possible health.
   * @return Maximum health.
   */
  [[nodiscard]] static int getMaxHealth() noexcept;

  /**
   * @brief Applies handcuffs to the player.
   */
  void applyHandcuffs() noexcept;

  /**
   * @brief Removes handcuffs.
   */
  void removeHandcuffs() noexcept;

  /**
   * @brief Checks if handcuffs are applied.
   * @return True if active.
   */
  [[nodiscard]] bool areHandcuffsApplied() const noexcept;

  /**
   * @brief Sets the known next shell type.
   * @param nextShell The shell type revealed.
   */
  void setKnownNextShell(ShellType nextShell) noexcept;

  /**
   * @brief Checks if the next shell has been revealed.
   * @return True if revealed, false otherwise.
   */
  [[nodiscard]] bool isNextShellRevealed() const noexcept;

  /**
   * @brief Returns the known next shell type.
   * @return The revealed shell type.
   */
  [[nodiscard]] ShellType returnKnownNextShell() const noexcept;

  /**
   * @brief Resets the known next shell type.
   */
  void resetKnownNextShell() noexcept;

  /**
   * @brief Adds an item to the inventory.
   * @param newItem Pointer to the item (takes ownership).
   * @return True if added, false if inventory is full.
   */
  bool addItem(std::unique_ptr<Item> newItem);

  /**
   * @brief Uses an item by index.
   * @param index The item index.
   * @return True if used successfully.
   */
  bool useItem(int index);

  /**
   * @brief Uses an item with shotgun interaction.
   * @param index The item index.
   * @param shotgun Pointer to the shotgun.
   * @return True if used successfully.
   */
  bool useItem(int index, Shotgun *shotgun);

  /**
   * @brief Gets the number of held items.
   * @return Item count.
   */
  [[nodiscard]] int getItemCount() const noexcept;

  /**
   * @brief Uses an item by name.
   * @param itemName The item name.
   * @param shotgun Optional shotgun pointer.
   * @return True if used.
   */
  bool useItemByName(std::string_view itemName, Shotgun *shotgun = nullptr);

  /**
   * @brief Removes an item by name.
   * @param itemName The item name.
   * @return True if removed.
   */
  bool removeItemByName(std::string_view itemName);

  /**
   * @brief Checks if the player has a specific item.
   * @param itemName The item name.
   * @return True if found.
   */
  [[nodiscard]] bool hasItem(std::string_view itemName) const;

  /**
   * @brief Returns the number of items matching a given name.
   * @param itemName The item name.
   * @return Number of matching items.
   */
  [[nodiscard]] int countItem(std::string_view itemName) const;

  /**
   * @brief Gets a copy of the player's items as raw pointers for read-only
   * operations.
   * @return A vector of raw pointers to the items.
   */
  [[nodiscard]] std::vector<Item *> getItemsView() const;

  /**
   * @brief Prints the player's inventory.
   */
  void printItems() const;

  /**
   * @brief Resets the flag allowing the player to use handcuffs again.
   */
  void resetHandcuffUsage() noexcept;

  /**
   * @brief Prevents the player from using handcuffs again in the current turn.
   */
  void useHandcuffsThisTurn() noexcept;

  /**
   * @brief Determines whether the player has already used handcuffs this turn.
   * @return True if handcuffs have been used this turn, false otherwise.
   */
  [[nodiscard]] bool hasUsedHandcuffsThisTurn() const noexcept;
};

/**
 * @brief Overloads the << operator to provide formatted output for a Player.
 * @param os The output stream.
 * @param player The Player object to output.
 * @return std::ostream& The output stream.
 */
std::ostream &operator<<(std::ostream &os, const Player &player);

#endif // BUCKSHOT_ROULETTE_BOT_PLAYER_H