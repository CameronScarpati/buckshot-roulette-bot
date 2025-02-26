#ifndef BUCKSHOT_ROULETTE_BOT_HANDCUFFS_H
#define BUCKSHOT_ROULETTE_BOT_HANDCUFFS_H

#include "Item.h"
#include <memory>
#include <string_view>

/**
 * @brief Represents a Handcuffs item.
 *
 * Skips the target player's next turn when used.
 */
class Handcuffs final : public Item {
public:
  /**
   * @brief Uses the handcuffs to restrict the target's next turn.
   * @param user The player using the item.
   * @param target The opponent who will be affected.
   * @param shotgun Unused parameter.
   */
  void use(Player *user, Player *target, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Handcuffs".
   */
  [[nodiscard]] std::string_view getName() const override;

  /**
   * @brief Creates a deep copy of this handcuffs item.
   * @return Unique pointer to a new handcuffs instance.
   */
  [[nodiscard]] std::unique_ptr<Item> clone() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_HANDCUFFS_H