#include "Beer.h"
#include "Player.h"
#include <iostream>

void Beer::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  if (!user || !shotgun)
    return;

  std::cout << user->getName()
            << " uses Beer to eject the current shell from the shotgun."
            << "\n";
  shotgun->rackShell();
}

std::string_view Beer::getName() const {
  static const std::string name = "Beer";
  return name;
}

std::unique_ptr<Item> Beer::clone() const {
  return std::make_unique<Beer>(*this);
}