#include "SimulatedShotgun.h"

SimulatedShotgun::SimulatedShotgun(int shells, int lives, int blanks) {
  if (lives + blanks != shells)
    throw std::invalid_argument(
        "Total shells must equal live shells + blank shells");
  totalShells = shells;
  liveShells = lives;
  blankShells = blanks;
}

void SimulatedShotgun::getLiveShell() {
  if (liveShells == 0)
    throw std::logic_error("There are no live shells.");

  --liveShells;
  --totalShells;
}

void SimulatedShotgun::getBlankShell() {
  if (blankShells == 0)
    throw std::logic_error("There are no blank shells.");

  --blankShells;
  --totalShells;
}
int SimulatedShotgun::getLiveShellCount() const { return liveShells; }

int SimulatedShotgun::getBlankShellCount() const { return blankShells; }

int SimulatedShotgun::getTotalShellCount() const { return totalShells; }

float SimulatedShotgun::getLiveShellProbability() const {
  return static_cast<float>(liveShells) / static_cast<float>(totalShells);
}

float SimulatedShotgun::getBlankShellProbability() const {
  return static_cast<float>(blankShells) / static_cast<float>(totalShells);
}

bool SimulatedShotgun::isEmpty() const { return totalShells == 0; }
