#include "Handsaw.h"
#include "Player.h"
#include <iostream>

void Handsaw::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  std::cout << user->getName()
            << " uses a Handsaw to modify the shotgun, doubling the damage of "
               "the next live round."
            << std::endl;
  shotgun->useHandsaw();
}

std::string Handsaw::getName() const { return "Handsaw"; }
