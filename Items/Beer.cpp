#include "Beer.h"
#include "Player.h"
#include <iostream>

void Beer::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  std::cout << user->getName()
            << " uses Beer to eject the current shell from the shotgun."
            << "\n";
  shotgun->rackShell();
}

std::string Beer::getName() const { return "Beer"; }
