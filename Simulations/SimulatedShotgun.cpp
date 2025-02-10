#include "SimulatedShotgun.h"
#include <stdexcept>

SimulatedShotgun::SimulatedShotgun(int total, int live, int blank) {
  if (live + blank != total)
    throw std::invalid_argument(
        "Total shells must equal live shells + blank shells");
  totalShells = total;
  liveShells = live;
  blankShells = blank;
  sawUsed = false;
}

ShellType SimulatedShotgun::getNextShell() {
  throw std::logic_error(
      "SimulatedShotgun::getNextShell() should not be called in simulation.");
}

ShellType SimulatedShotgun::simulateLiveShell() {
  if (liveShells <= 0)
    throw std::logic_error("No live shells available for simulation.");
  --liveShells;
  --totalShells;
  return ShellType::LIVE_SHELL;
}

ShellType SimulatedShotgun::simulateBlankShell() {
  if (blankShells <= 0)
    throw std::logic_error("No blank shells available for simulation.");
  --blankShells;
  --totalShells;
  return ShellType::BLANK_SHELL;
}

bool SimulatedShotgun::isNextShellRevealed() const { return nextShellRevealed; }

ShellType SimulatedShotgun::getRevealedNextShell() const {
  if (!nextShellRevealed)
    throw std::logic_error("Next shell has not been revealed yet!");
  return revealedShell;
}

void SimulatedShotgun::setRevealedNextShell(ShellType shell) {
  nextShellRevealed = true;
  revealedShell = shell;
}
