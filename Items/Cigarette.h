#ifndef BUCKSHOT_ROULETTE_BOT_CIGARETTE_H
#define BUCKSHOT_ROULETTE_BOT_CIGARETTE_H

#include "Item.h"

/**
 * @brief Represents a Cigarette item.
 *
 * Restores 1 HP when used.
 */
class Cigarette : public Item {
public:
  /**
   * @brief Uses the cigarette to restore health.
   * @param user The player using the item.
   * @param target Unused parameter.
   * @param shotgun Unused parameter.
   */
  void use(Player *user, Player *, Shotgun *) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Cigarette".
   */
  std::string getName() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_CIGARETTE_H
