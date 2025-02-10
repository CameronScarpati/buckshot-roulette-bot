#ifndef BUCKSHOT_ROULETTE_BOT_HANDCUFFS_H
#define BUCKSHOT_ROULETTE_BOT_HANDCUFFS_H

#include "Item.h"

/**
 * @brief Represents a Handcuffs item.
 *
 * Skips the target player's next turn when used.
 */
class Handcuffs : public Item {
public:
  /**
   * @brief Uses the handcuffs to restrict the target's next turn.
   * @param user The player using the item.
   * @param target The opponent who will be affected.
   * @param shotgun Unused parameter.
   */
  void use(Player *user, Player *target, Shotgun *) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Handcuffs".
   */
  std::string getName() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_HANDCUFFS_H
