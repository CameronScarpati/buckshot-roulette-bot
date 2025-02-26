#include "Handcuffs.h"
#include "Player.h"
#include <iostream>

void Handcuffs::use(Player *user, Player *target, Shotgun * /*shotgun*/) {
  if (!user || !target)
    return;

  if (!user->hasUsedHandcuffsThisTurn()) {
    std::cout << user->getName() << " uses Handcuffs on " << target->getName()
              << ", causing them to miss their next turn."
              << "\n";
    target->applyHandcuffs();
    user->useHandcuffsThisTurn();
  } else
    std::cout << user->getName() << " has already applied handcuffs this turn."
              << "\n";
}

std::string_view Handcuffs::getName() const {
  static const std::string name = "Handcuffs";
  return name;
}

std::unique_ptr<Item> Handcuffs::clone() const {
  return std::make_unique<Handcuffs>(*this);
}