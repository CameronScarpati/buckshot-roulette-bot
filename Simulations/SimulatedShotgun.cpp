#include "SimulatedShotgun.h"
#include "Exceptions.h"

SimulatedShotgun::SimulatedShotgun(int total, int live, int blank,
                                   bool isSawUsed) {
  if (live + blank != total) {
    throw InvalidGameArgumentException(
        "Total shells must equal live shells + blank shells");
  }

  totalShells = total;
  liveShells = live;
  blankShells = blank;
  sawUsed = isSawUsed;
}

ShellType SimulatedShotgun::getNextShell() {
  throw SimulationException(
      "SimulatedShotgun::getNextShell() should not be called in simulation.");
}

ShellType SimulatedShotgun::simulateLiveShell() {
  if (liveShells <= 0) {
    throw SimulationException("No live shells available for simulation.");
  }

  --liveShells;
  --totalShells;
  return ShellType::LIVE_SHELL;
}

ShellType SimulatedShotgun::simulateBlankShell() {
  if (blankShells <= 0) {
    throw SimulationException("No blank shells available for simulation.");
  }

  --blankShells;
  --totalShells;
  return ShellType::BLANK_SHELL;
}