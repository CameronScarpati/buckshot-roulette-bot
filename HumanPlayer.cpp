#include "HumanPlayer.h"
#include <iostream>
#include <limits>
#include <string>

// Lowest valid action index (Action::SHOOT_SELF).
static constexpr int MIN_ACTION_ID = 0;
// Highest valid action index (Action::USE_HANDSAW).
static constexpr int MAX_ACTION_ID = 6;

HumanPlayer::HumanPlayer(std::string name, int health)
    : Player(std::move(name), health) {}

HumanPlayer::HumanPlayer(std::string name, int health, Player *opponent)
    : Player(std::move(name), health, opponent) {}

Action HumanPlayer::chooseAction(Shotgun *currentShotgun) {
  int input;

  while (true) {
    displayActions();
    std::cout << "Choice: ";

    if (!(std::cin >> input)) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid input. Please enter a number from " << MIN_ACTION_ID
                << " to " << MAX_ACTION_ID << ".\n";
      continue;
    }

    if (input < MIN_ACTION_ID || input > MAX_ACTION_ID) {
      std::cout << "Invalid input. Please enter a number from " << MIN_ACTION_ID
                << " to " << MAX_ACTION_ID << ".\n";
      continue;
    }

    auto chosen = static_cast<Action>(input);

    // Validate the action based on current inventory and status
    if (!isValidAction(chosen))
      continue;

    return chosen;
  }
}

void HumanPlayer::displayActions() {
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
}

bool HumanPlayer::isValidAction(Action action) const {
  switch (action) {
  case Action::SHOOT_SELF:
  case Action::SHOOT_OPPONENT:
    return true;

  case Action::SMOKE_CIGARETTE:
    if (!hasItem("Cigarette")) {
      std::cout << "You do not have any Cigarette to smoke. Please choose a "
                << "different action."
                << "\n";
      return false;
    }
    return true;

  case Action::USE_HANDCUFFS:
    if (!hasItem("Handcuffs")) {
      std::cout << "You do not have any Handcuffs. Please choose a different "
                << "action."
                << "\n";
      return false;
    }
    if (hasUsedHandcuffsThisTurn()) {
      std::cout << "You have already applied handcuffs this turn. Please "
                << "choose a different action."
                << "\n";
      return false;
    }
    return true;

  case Action::USE_MAGNIFYING_GLASS:
    if (!hasItem("Magnifying Glass")) {
      std::cout << "You do not have a Magnifying Glass. Please choose a "
                << "different action."
                << "\n";
      return false;
    }
    return true;

  case Action::DRINK_BEER:
    if (!hasItem("Beer")) {
      std::cout << "You do not have any Beer. Please choose a different action."
                << "\n";
      return false;
    }
    return true;

  case Action::USE_HANDSAW:
    if (!hasItem("Handsaw")) {
      std::cout
          << "You do not have a Handsaw. Please choose a different action."
          << "\n";
      return false;
    }
    return true;

  default:
    return false;
  }
}