#include "SimulatedPlayer.h"
#include <iostream>

SimulatedPlayer::SimulatedPlayer(const std::string &name, int health)
    : Player(name, health) {}

SimulatedPlayer::SimulatedPlayer(const std::string &name, int health,
                                 Player *opponent)
    : Player(name, health, opponent) {}

Action SimulatedPlayer::chooseAction(const Shotgun * /*currentShotgun*/) {
  throw std::logic_error(
      "chooseAction() should not be called on a SimulatedPlayer!");
}