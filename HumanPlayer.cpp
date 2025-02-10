#include "HumanPlayer.h"
#include <iostream>

HumanPlayer::HumanPlayer(const std::string &name, int health)
    : Player(name, health) {}

HumanPlayer::HumanPlayer(const std::string &name, int health, Player *opponent)
    : Player(name, health, opponent) {}

Action HumanPlayer::chooseAction(const Shotgun &currentShotgun) {
  int input;

  std::cout << "Enter action (0 for SHOOT_SELF, 1 for SHOOT_OPPONENT): ";
  std::cin >> input;
  while (input < 0 || input > 1) {
    std::cout << "Input was invalid.";
    std::cout << "Enter action (0 for SHOOT_SELF, 1 for SHOOT_OPPONENT): ";
    std::cin >> input;
  }
  return (input == 1) ? Action::SHOOT_OPPONENT : Action::SHOOT_SELF;
}