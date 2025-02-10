#ifndef BUCKSHOT_ROULETTE_BOT_HANDSAW_H
#define BUCKSHOT_ROULETTE_BOT_HANDSAW_H

#include "Item.h"

/**
 * @brief Represents a Handsaw item.
 *
 * Doubles the damage of live rounds when used.
 */
class Handsaw : public Item {
public:
  /**
   * @brief Uses the handsaw to increase live round damage.
   * @param user The player using the item.
   * @param target Unused parameter.
   * @param shotgun Pointer to the shotgun.
   */
  void use(Player *user, Player *, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Handsaw".
   */
  std::string getName() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_HANDSAW_H
