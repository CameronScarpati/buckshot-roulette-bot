#include "MagnifyingGlass.h"
#include "BotPlayer.h"
#include <iostream>

void MagnifyingGlass::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  if (!user || !shotgun)
    return;

  std::cout << user->getName()
            << " uses a Magnifying Glass to inspect the shotgun's chamber."
            << "\n";

  ShellType revealedShell = shotgun->revealNextShell();

  // Dynamic cast to check if a user is a BotPlayer
  auto *maybeBot = dynamic_cast<BotPlayer *>(user);
  if (maybeBot)
    maybeBot->setKnownNextShell(revealedShell);
  else
    std::cout << "The shell in the chamber is " << revealedShell << "\n";
}

std::string_view MagnifyingGlass::getName() const {
  static const std::string name = "Magnifying Glass";
  return name;
}

std::unique_ptr<Item> MagnifyingGlass::clone() const {
  return std::make_unique<MagnifyingGlass>(*this);
}