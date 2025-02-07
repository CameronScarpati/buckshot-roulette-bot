#include "HumanPlayer.h"
#include <iostream>

HumanPlayer::HumanPlayer(const std::string& name, int health)
        : Player(name, health) {}

Action HumanPlayer::chooseAction(const Shotgun& currentShotgun) {
    // TODO: Prompt the user for an action.
    int input;
    std::cout << "Enter action (0 for SHOOT_SELF, 1 for SHOOT_OPPONENT): ";
    std::cin >> input;
    return static_cast<Action>(input);
}