#ifndef BUCKSHOT_ROULETTE_BOT_HANDSAW_H
#define BUCKSHOT_ROULETTE_BOT_HANDSAW_H

#include "Item.h"
#include <memory>
#include <string_view>

/**
 * @brief Represents a Handsaw item.
 *
 * Doubles the damage of live rounds when used.
 */
class Handsaw final : public Item {
public:
  /**
   * @brief Uses the handsaw to increase live round damage.
   * @param user The player using the item.
   * @param target Unused parameter.
   * @param shotgun Pointer to the shotgun.
   */
  void use(Player *user, Player *target, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Handsaw".
   */
  [[nodiscard]] std::string_view getName() const override;

  /**
   * @brief Creates a deep copy of this handsaw item.
   * @return Unique pointer to a new handsaw instance.
   */
  [[nodiscard]] std::unique_ptr<Item> clone() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_HANDSAW_H