#include "Shotgun.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

std::ostream &operator<<(std::ostream &os, const ShellType &shell) {
  switch (shell) {
  case ShellType::BLANK_SHELL:
    os << "BLANK_SHELL";
    break;
  case ShellType::LIVE_SHELL:
    os << "LIVE_SHELL";
    break;
  default:
    os << "UNKNOWN_SHELL";
    break;
  }
  return os;
}

void Shotgun::loadShells() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(2, 8);

  totalShells = dist(gen);

  // Distribute shells evenly, with one extra live shell if odd number
  if (totalShells % 2 == 0) {
    liveShells = totalShells / 2;
    blankShells = totalShells / 2;
  } else {
    liveShells = (totalShells / 2) + 1;
    blankShells = totalShells / 2;
  }

  // Create and shuffle the shells
  loadedShells.clear();
  loadedShells.resize(liveShells, ShellType::LIVE_SHELL);
  loadedShells.resize(totalShells, ShellType::BLANK_SHELL);
  std::shuffle(loadedShells.begin(), loadedShells.end(), gen);
}

ShellType Shotgun::getNextShell() {
  if (isEmpty()) {
    throw std::runtime_error("The Shotgun is empty.");
  }

  ShellType nextShell = loadedShells.front();
  loadedShells.pop_front();

  if (nextShell == ShellType::LIVE_SHELL) {
    --liveShells;
  } else {
    --blankShells;
  }
  --totalShells;

  return nextShell;
}

ShellType Shotgun::revealNextShell() const {
  if (isEmpty()) {
    throw std::runtime_error("The Shotgun is empty.");
  }
  return loadedShells.front();
}

void Shotgun::rackShell() {
  if (isEmpty()) {
    throw std::runtime_error("The Shotgun is empty.");
  }

  ShellType nextShell = loadedShells.front();
  loadedShells.pop_front();

  std::cout << "The racked shell is a " << nextShell << "." << std::endl;

  if (nextShell == ShellType::LIVE_SHELL) {
    --liveShells;
  } else {
    --blankShells;
  }
  --totalShells;
}

void Shotgun::useHandsaw() noexcept { sawUsed = true; }

void Shotgun::resetSawUsed() noexcept { sawUsed = false; }

bool Shotgun::getSawUsed() const noexcept { return sawUsed; }

bool Shotgun::isEmpty() const noexcept { return totalShells == 0; }

int Shotgun::getLiveShellCount() const noexcept { return liveShells; }

int Shotgun::getBlankShellCount() const noexcept { return blankShells; }

int Shotgun::getTotalShellCount() const noexcept { return totalShells; }

float Shotgun::getLiveShellProbability() const noexcept {
  return (totalShells > 0)
             ? static_cast<float>(liveShells) / static_cast<float>(totalShells)
             : 0.0f;
}

float Shotgun::getBlankShellProbability() const noexcept {
  return (totalShells > 0)
             ? static_cast<float>(blankShells) / static_cast<float>(totalShells)
             : 0.0f;
}