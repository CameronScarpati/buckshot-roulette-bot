#include "Shotgun.h"
#include <iostream>
#include <random>

void Shotgun::loadShells() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(2, 8);

  totalShells = dist(gen);

  if (totalShells % 2 == 0) {
    liveShells = totalShells / 2;
    blankShells = totalShells / 2;
  } else {
    liveShells = (totalShells / 2) + 1;
    blankShells = totalShells / 2;
  }

  loadedShells = std::deque<ShellType>(liveShells, ShellType::LIVE_SHELL);
  loadedShells.insert(loadedShells.end(), blankShells, ShellType::BLANK_SHELL);
  std::shuffle(loadedShells.begin(), loadedShells.end(), gen);
}

[[nodiscard]] ShellType Shotgun::getNextShell() {
  if (isEmpty())
    throw std::runtime_error("The Shotgun is empty.");

  ShellType nextShell = loadedShells.front();
  loadedShells.pop_front();

  if (nextShell == ShellType::LIVE_SHELL) {
    --liveShells;
  } else
    --blankShells;
  --totalShells;

  return nextShell;
}

ShellType Shotgun::revealNextShell() const { return loadedShells.front(); }

void Shotgun::rackShell() {
  if (!isEmpty()) {
    ShellType nextShell = loadedShells.front();
    loadedShells.pop_front();

    std::cout << "The racked shell is a " << nextShell << "." << std::endl;

    if (nextShell == ShellType::LIVE_SHELL) {
      --liveShells;
    } else
      --blankShells;
    --totalShells;
  }
}

void Shotgun::useHandsaw() { sawUsed = true; }

void Shotgun::resetSawUsed() { sawUsed = false; }

bool Shotgun::getSawUsed() const { return sawUsed; }

bool Shotgun::isEmpty() const { return totalShells == 0; }

int Shotgun::getLiveShellCount() const { return liveShells; }

int Shotgun::getBlankShellCount() const { return blankShells; }

int Shotgun::getTotalShellCount() const { return totalShells; }

float Shotgun::getLiveShellProbability() const {
  return static_cast<float>(liveShells) / static_cast<float>(totalShells);
}

float Shotgun::getBlankShellProbability() const {
  return 1 - getLiveShellProbability();
}