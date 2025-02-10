#ifndef BUCKSHOT_ROULETTE_BOT_ITEM_H
#define BUCKSHOT_ROULETTE_BOT_ITEM_H

#include "Shotgun.h"
#include <string>
class Player;

/**
 * @brief Base class for all items.
 *
 * Defines the interface for item usage and retrieval.
 */
class Item {
public:
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
  virtual std::string getName() const = 0;
};

#endif // BUCKSHOT_ROULETTE_BOT_ITEM_H
