#ifndef BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H
#define BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H

#include "Item.h"

/**
 * @brief Represents a Magnifying Glass item.
 *
 * Reveals the next shell in the shotgun when used.
 */
class MagnifyingGlass : public Item {
public:
  /**
   * @brief Uses the magnifying glass to reveal the next shell.
   * @param user The player using the item.
   * @param target Unused parameter.
   * @param shotgun Pointer to the shotgun.
   */
  void use(Player *user, Player *, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Magnifying Glass".
   */
  std::string getName() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H
