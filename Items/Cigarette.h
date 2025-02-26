#ifndef BUCKSHOT_ROULETTE_BOT_CIGARETTE_H
#define BUCKSHOT_ROULETTE_BOT_CIGARETTE_H

#include "Item.h"
#include <memory>
#include <string_view>

/**
 * @brief Represents a Cigarette item.
 *
 * Restores 1 HP when used.
 */
class Cigarette final : public Item {
public:
  /**
   * @brief Uses the cigarette to restore health.
   * @param user The player using the item.
   * @param target Unused parameter.
   * @param shotgun Unused parameter.
   */
  void use(Player *user, Player *target, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Cigarette".
   */
  [[nodiscard]] std::string_view getName() const override;

  /**
   * @brief Creates a deep copy of this cigarette.
   * @return Unique pointer to a new cigarette instance.
   */
  [[nodiscard]] std::unique_ptr<Item> clone() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_CIGARETTE_H