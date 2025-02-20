#include "Handcuffs.h"
#include "Player.h"
#include <iostream>

void Handcuffs::use(Player *user, Player *target, Shotgun * /*shotgun*/) {
  if (!user->hasUsedHandcuffsThisTurn()) {
    std::cout << user->getName() << " uses Handcuffs on " << target->getName()
              << ", causing them to miss their next turn." << std::endl;
    target->applyHandcuffs();
    user->useHandcuffsThisTurn();
  } else {
    std::cout << user->getName() << " has already applied handcuffs this turn."
              << std::endl;
  }
}

std::string Handcuffs::getName() const { return "Handcuffs"; }
