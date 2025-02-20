#include "HumanPlayer.h"
#include <iostream>

HumanPlayer::HumanPlayer(const std::string &name, int health)
    : Player(name, health) {}

HumanPlayer::HumanPlayer(const std::string &name, int health, Player *opponent)
    : Player(name, health, opponent) {}

Action HumanPlayer::chooseAction(Shotgun *currentShotgun) {
  int input;
  while (true) {
    std::cout << "Enter action:"
              << "\n";
    std::cout << "  0: SHOOT_SELF"
              << "\n";
    std::cout << "  1: SHOOT_OPPONENT"
              << "\n";
    std::cout << "  2: SMOKE_CIGARETTE"
              << "\n";
    std::cout << "  3: USE_HANDCUFFS"
              << "\n";
    std::cout << "  4: USE_MAGNIFYING_GLASS"
              << "\n";
    std::cout << "  5: DRINK_BEER"
              << "\n";
    std::cout << "  6: USE_HANDSAW"
              << "\n";
    std::cout << "Choice: ";
    std::cin >> input;
    if (input < 0 || input > 6) {
      std::cout << "Invalid input. Please enter a number from 0 to 6."
                << "\n";
      continue;
    }
    auto chosen = static_cast<Action>(input);

    if (chosen == Action::SMOKE_CIGARETTE && !hasItem("Cigarette")) {
      std::cout << "You do not have any Cigarette to smoke. Please choose a "
                   "different action."
                << "\n";
      continue;
    }
    if (chosen == Action::USE_HANDCUFFS) {
      if (!hasItem("Handcuffs")) {
        std::cout << "You do not have any Handcuffs. Please choose a different "
                     "action."
                  << "\n";
        continue;
      } else if (handcuffsUsedThisTurn) {
        std::cout << "You have already applied handcuffs this turn. Please "
                     "choose a different action."
                  << "\n";
        continue;
      }
    }
    if (chosen == Action::USE_MAGNIFYING_GLASS &&
        !hasItem("Magnifying Glass")) {
      std::cout << "You do not have a Magnifying Glass. Please choose a "
                   "different action."
                << "\n";
      continue;
    }
    if (chosen == Action::DRINK_BEER && !hasItem("Beer")) {
      std::cout << "You do not have any Beer. Please choose a different action."
                << "\n";
      continue;
    }
    if (chosen == Action::USE_HANDSAW && !hasItem("Handsaw")) {
      std::cout
          << "You do not have a Handsaw. Please choose a different action."
          << "\n";
      continue;
    }
    return chosen;
  }
}
