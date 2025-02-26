#include "SimulatedPlayer.h"
#include <iostream>
#include <stdexcept>

SimulatedPlayer::SimulatedPlayer(std::string name, int health)
    : Player(std::move(name), health) {}

SimulatedPlayer::SimulatedPlayer(std::string name, int health, Player *opponent)
    : Player(std::move(name), health, opponent) {}

SimulatedPlayer::SimulatedPlayer(const SimulatedPlayer &other)
    : Player(other) {}

SimulatedPlayer::SimulatedPlayer(SimulatedPlayer &&other) noexcept
    : Player(std::move(other)) {}

SimulatedPlayer &SimulatedPlayer::operator=(const SimulatedPlayer &other) {
  if (this != &other) {
    Player::operator=(other);
  }
  return *this;
}

SimulatedPlayer &SimulatedPlayer::operator=(SimulatedPlayer &&other) noexcept {
  if (this != &other) {
    Player::operator=(std::move(other));
  }
  return *this;
}

SimulatedPlayer::SimulatedPlayer(const Player &other) : Player(other) {}

Action SimulatedPlayer::chooseAction(Shotgun * /*currentShotgun*/) {
  throw std::logic_error(
      "chooseAction() should not be called on a SimulatedPlayer!");
}