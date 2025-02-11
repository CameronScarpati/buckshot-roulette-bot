#include "SimulatedPlayer.h"
#include <iostream>

SimulatedPlayer::SimulatedPlayer(const std::string &name, int health)
    : Player(name, health) {}

SimulatedPlayer::SimulatedPlayer(const std::string &name, int health,
                                 Player *opponent)
    : Player(name, health, opponent) {}

SimulatedPlayer::SimulatedPlayer(const SimulatedPlayer &other)
    : Player(other.getName(), other.getHealth(), nullptr) {
  this->handcuffsApplied = other.handcuffsApplied;
  this->itemCount = other.itemCount;
  this->items.fill(nullptr);

  for (int i = 0; i < itemCount; i++)
    this->items[i] = other.items[i];
}

SimulatedPlayer::SimulatedPlayer(const Player &other) : Player(other) {}

Action SimulatedPlayer::chooseAction(const Shotgun * /*currentShotgun*/) {
  throw std::logic_error(
      "chooseAction() should not be called on a SimulatedPlayer!");
}
