#ifndef BUCKSHOT_ROULETTE_BOT_BEER_H
#define BUCKSHOT_ROULETTE_BOT_BEER_H

#include "Item.h"
#include <memory>
#include <string_view>

/**
 * @brief Represents a Beer item.
 *
 * Removes the current shell from the shotgun when used.
 */
class Beer final : public Item {
public:
  /**
   * @brief Uses the beer item to eject a shell.
   * @param user The player using the item.
   * @param target The target player (unused for Beer).
   * @param shotgun Pointer to the shotgun.
   */
  void use(Player *user, Player *target, Shotgun *shotgun) override;

  /**
   * @brief Retrieves the item's name.
   * @return The string "Beer".
   */
  [[nodiscard]] std::string_view getName() const override;

  /**
   * @brief Creates a deep copy of this beer.
   * @return Unique pointer to a new beer instance.
   */
  [[nodiscard]] std::unique_ptr<Item> clone() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_BEER_H