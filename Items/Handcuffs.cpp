#include "Handcuffs.h"
#include "Player.h"
#include <iostream>

void Handcuffs::use(Player *user, Player *target, Shotgun * /*shotgun*/) {
  std::cout << user->getName() << " uses Handcuffs on " << target->getName()
            << ", causing them to miss their next turn." << std::endl;
  target->applyHandcuffs();
}

std::string Handcuffs::getName() const { return "Handcuffs"; }
