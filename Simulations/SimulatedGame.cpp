#include "SimulatedGame.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"

SimulatedGame::SimulatedGame(SimulatedPlayer *p1, SimulatedPlayer *p2,
                             SimulatedShotgun *shotgun, bool isPlayerOneTurn)
    : Game(p1, p2, isPlayerOneTurn) {
  this->shotgun = shotgun;
}

SimulatedGame::SimulatedGame(const SimulatedGame &other)
    : Game(nullptr, nullptr, false) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(other.shotgun);
  if (!simShotgun)
    throw std::logic_error("Trying to copy a SimulatedGame whose shotgun is "
                           "not SimulatedShotgun!");

  this->shotgun = new SimulatedShotgun(
      simShotgun->getTotalShellCount(), simShotgun->getLiveShellCount(),
      simShotgun->getBlankShellCount(), simShotgun->getSawUsed());

  this->playerOne = new SimulatedPlayer(*other.playerOne);
  this->playerTwo = new SimulatedPlayer(*other.playerTwo);

  this->isPlayerOneTurn = other.isPlayerOneTurn;

  if (this->playerOne && this->playerTwo) {
    this->playerOne->setOpponent(this->playerTwo);
    this->playerTwo->setOpponent(this->playerOne);
  }

  this->isPlayerOneTurn = isPlayerOneTurn;
}

void SimulatedGame::printShells() {
  throw std::logic_error(
      "SimulatedGame::printShells() should not be called in simulation.");
}

void SimulatedGame::runGame() {
  throw std::logic_error(
      "SimulatedGame::runGame() should not be called in simulation.");
}
