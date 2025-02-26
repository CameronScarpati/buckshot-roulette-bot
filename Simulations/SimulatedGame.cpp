#include "SimulatedGame.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"
#include <memory>
#include <stdexcept>

SimulatedGame::SimulatedGame(SimulatedPlayer *p1, SimulatedPlayer *p2,
                             SimulatedShotgun *shotgun, bool isPlayerOneTurn)
    : Game(p1, p2, isPlayerOneTurn) {
  // Replace the shotgun with the simulation shotgun
  this->shotgun.reset(shotgun);
}

SimulatedGame::SimulatedGame(const SimulatedGame &other)
    : Game(nullptr, nullptr, false) {
  // Check if the shotgun is of the correct type
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(other.getShotgun());
  if (!simShotgun) {
    throw std::logic_error("Trying to copy a SimulatedGame whose shotgun is "
                           "not SimulatedShotgun!");
  }

  // Create a new shotgun instance
  this->shotgun = std::make_unique<SimulatedShotgun>(
      simShotgun->getTotalShellCount(), simShotgun->getLiveShellCount(),
      simShotgun->getBlankShellCount(), simShotgun->getSawUsed());

  // Create player copies
  auto *p1 = dynamic_cast<SimulatedPlayer *>(other.getPlayerOne());
  auto *p2 = dynamic_cast<SimulatedPlayer *>(other.getPlayerTwo());

  if (!p1 || !p2) {
    throw std::logic_error(
        "Trying to copy SimulatedGame with invalid player types!");
  }

  this->playerOne = new SimulatedPlayer(*p1);
  this->playerTwo = new SimulatedPlayer(*p2);

  // Set opponent relationships
  this->playerOne->setOpponent(this->playerTwo);
  this->playerTwo->setOpponent(this->playerOne);

  // Copy turn status
  this->isPlayerOneTurn = other.isPlayerOneTurnNow();
}

SimulatedGame::SimulatedGame(SimulatedGame &&other) noexcept
    : Game(other.playerOne, other.playerTwo, other.isPlayerOneTurn) {
  // Move shotgun
  this->shotgun = std::move(other.shotgun);

  // Clear other's pointers
  other.playerOne = nullptr;
  other.playerTwo = nullptr;
  other.shotgun = nullptr;
}

SimulatedGame &SimulatedGame::operator=(const SimulatedGame &other) {
  if (this != &other) {
    // Clean up existing objects
    delete playerOne;
    delete playerTwo;

    // Get a shotgun from other
    auto *simShotgun = dynamic_cast<SimulatedShotgun *>(other.getShotgun());
    if (!simShotgun) {
      throw std::logic_error("Trying to copy a SimulatedGame whose shotgun is "
                             "not SimulatedShotgun!");
    }

    // Copy shotgun
    this->shotgun = std::make_unique<SimulatedShotgun>(
        simShotgun->getTotalShellCount(), simShotgun->getLiveShellCount(),
        simShotgun->getBlankShellCount(), simShotgun->getSawUsed());

    // Copy players
    auto *p1 = dynamic_cast<SimulatedPlayer *>(other.getPlayerOne());
    auto *p2 = dynamic_cast<SimulatedPlayer *>(other.getPlayerTwo());

    if (!p1 || !p2) {
      throw std::logic_error(
          "Trying to copy SimulatedGame with invalid player types!");
    }

    this->playerOne = new SimulatedPlayer(*p1);
    this->playerTwo = new SimulatedPlayer(*p2);

    // Set opponent relationships
    this->playerOne->setOpponent(this->playerTwo);
    this->playerTwo->setOpponent(this->playerOne);

    // Copy turn status
    this->isPlayerOneTurn = other.isPlayerOneTurnNow();
  }
  return *this;
}

SimulatedGame &SimulatedGame::operator=(SimulatedGame &&other) noexcept {
  if (this != &other) {
    // Clean up existing objects
    delete playerOne;
    delete playerTwo;

    // Move data
    this->playerOne = other.playerOne;
    this->playerTwo = other.playerTwo;
    this->shotgun = std::move(other.shotgun);
    this->isPlayerOneTurn = other.isPlayerOneTurn;

    // Clear other's pointers
    other.playerOne = nullptr;
    other.playerTwo = nullptr;
  }
  return *this;
}

void SimulatedGame::printShells() {
  throw std::logic_error(
      "SimulatedGame::printShells() should not be called in simulation.");
}

void SimulatedGame::runGame() {
  throw std::logic_error(
      "SimulatedGame::runGame() should not be called in simulation.");
}