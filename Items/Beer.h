#ifndef BUCKSHOT_ROULETTE_BOT_BEER_H
#define BUCKSHOT_ROULETTE_BOT_BEER_H

#include "Item.h"

/**
 * @brief Represents a Beer item.
 *
 * Removes the current shell from the shotgun when used.
 */
class Beer : public Item {
public:
  /**
   * @brief Uses the beer item.
   * @param user The player using the item.
   * @param target The target player (unused for Beer).
   * @param shotgun Pointer to the shotgun.
   */
  void use(Player *user, Player *target, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Beer".
   */
  std::string getName() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_BEER_H
