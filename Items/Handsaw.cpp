#include "Handsaw.h"
#include "Player.h"
#include <iostream>

void Handsaw::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  if (!user || !shotgun)
    return;

  std::cout << user->getName()
            << " uses a Handsaw to modify the shotgun, doubling the damage of "
               "the next live round."
            << "\n";
  shotgun->useHandsaw();
}

std::string_view Handsaw::getName() const {
  static const std::string name = "Handsaw";
  return name;
}

std::unique_ptr<Item> Handsaw::clone() const {
  return std::make_unique<Handsaw>(*this);
}