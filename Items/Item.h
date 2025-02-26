#ifndef BUCKSHOT_ROULETTE_BOT_ITEM_H
#define BUCKSHOT_ROULETTE_BOT_ITEM_H

#include "Shotgun.h"
#include <memory>
#include <string>
#include <string_view>

class Player;

/**
 * @brief Base class for all game items.
 *
 * Defines the interface for item usage and retrieval.
 */
class Item {
public:
  /**
   * @brief Virtual destructor for proper polymorphic destruction.
   */
  virtual ~Item() = default;

  /**
   * @brief Uses the item, affecting the user and target.
   * @param user The player using the item.
   * @param target The target player affected by the item.
   * @param shotgun Optional pointer to the shotgun.
   */
  virtual void use(Player *user, Player *target,
                   Shotgun *shotgun = nullptr) = 0;

  /**
   * @brief Retrieves the item's name.
   * @return The item name as a string.
   */
  [[nodiscard]] virtual std::string_view getName() const = 0;

  /**
   * @brief Creates a deep copy of the item.
   * @return Unique pointer to a new item instance.
   */
  [[nodiscard]] virtual std::unique_ptr<Item> clone() const = 0;

  /**
   * @brief Factory method to create items by name.
   * @param name The name of the item to create.
   * @return A unique pointer to the newly created item, or nullptr if name is
   * invalid.
   */
  [[nodiscard]] static std::unique_ptr<Item>
  createByName(std::string_view name);
};

#endif // BUCKSHOT_ROULETTE_BOT_ITEM_H