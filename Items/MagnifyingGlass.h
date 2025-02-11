#ifndef BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H
#define BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H

#include "Item.h"

/**
 * @class MagnifyingGlass
 * @brief An item that reveals the next shell in the shotgun.
 */
class MagnifyingGlass : public Item {
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
  void use(Player *user, Player *, Shotgun *shotgun) override;

  /**
   * @brief Returns the item's name.
   * @return "Magnifying Glass".
   */
  std::string getName() const override;
};

#endif // BUCKSHOT_ROULETTE_BOT_MAGNIFYINGGLASS_H
