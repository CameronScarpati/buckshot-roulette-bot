#ifndef BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H
#define BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H

#include "Item.h"
#include <memory>
#include <string_view>

/**
 * @class MagnifyingGlass
 * @brief An item that reveals the next shell in the shotgun.
 */
class MagnifyingGlass final : public Item {
public:
  /**
   * @brief Reveals the next shell in the shotgun.
   *
   * Displays the shell type to all players and updates bot players' internal
   * knowledge.
   *
   * @param user The player using the item.
   * @param target Unused parameter.
   * @param shotgun The shotgun to inspect.
   */
  void use(Player *user, Player *target, Shotgun *shotgun) override;

  /**
   * @brief Returns the item's name.
   * @return "Magnifying Glass".
   */
  [[nodiscard]] std::string_view getName() const override;

  /**
   * @brief Creates a deep copy of this magnifying glass item.
   * @return Unique pointer to a new magnifying glass instance.
   */
  [[nodiscard]] std::unique_ptr<Item> clone() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H