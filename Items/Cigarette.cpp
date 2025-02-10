#include "Cigarette.h"
#include "Player.h"
#include <iostream>

void Cigarette::use(Player *user, Player * /*target*/, Shotgun * /*shotgun*/) {
  std::cout << user->getName() << " uses Cigarette and restores 1 HP."
            << std::endl;
  user->smokeCigarette();
}

std::string Cigarette::getName() const { return "Cigarette"; }