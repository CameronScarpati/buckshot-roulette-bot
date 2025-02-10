#include "MagnifyingGlass.h"
#include "Player.h"
#include <iostream>

void MagnifyingGlass::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  std::cout << user->getName()
            << " uses a Magnifying Glass to inspect the shotgun's chamber."
            << std::endl;
  shotgun->revealNextShell();
}

std::string MagnifyingGlass::getName() const { return "Magnifying Glass"; }
