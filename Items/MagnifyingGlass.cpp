#include "MagnifyingGlass.h"
#include "BotPlayer.h"
#include <iostream>

void MagnifyingGlass::use(Player *user, Player * /*target*/, Shotgun *shotgun) {
  std::cout << user->getName()
            << " uses a Magnifying Glass to inspect the shotgun's chamber."
            << "\n";
  ShellType revealedShell = shotgun->revealNextShell();

  auto *maybeBot = dynamic_cast<BotPlayer *>(user);
  if (maybeBot)
    maybeBot->setKnownNextShell(revealedShell);
  else
    std::cout << "The shell in the chamber is " << revealedShell << "\n";
}

std::string MagnifyingGlass::getName() const { return "Magnifying Glass"; }
