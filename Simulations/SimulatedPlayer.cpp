#include "SimulatedPlayer.h"
#include "Exceptions.h"
#include <iostream>

SimulatedPlayer::SimulatedPlayer(std::string playerName, int playerHealth)
    : Player(std::move(playerName), playerHealth) {}

SimulatedPlayer::SimulatedPlayer(std::string playerName, int playerHealth,
                                 Player *playerOpponent)
    : Player(std::move(playerName), playerHealth, playerOpponent) {}

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
  throw SimulationException(
      "chooseAction() should not be called on a SimulatedPlayer!");
}