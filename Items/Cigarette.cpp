#include "Cigarette.h"
#include "Player.h"
#include <iostream>

void Cigarette::use(Player *user, Player * /*target*/, Shotgun * /*shotgun*/) {
  if (user) {
    std::cout << user->getName() << " uses Cigarette and restores 1 HP."
              << "\n";
    user->smokeCigarette();
  }
}

std::string_view Cigarette::getName() const {
  static const std::string name = "Cigarette";
  return name;
}

std::unique_ptr<Item> Cigarette::clone() const {
  return std::make_unique<Cigarette>(*this);
}