#include "SimulatedGame.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"

SimulatedGame::SimulatedGame(SimulatedPlayer *p1, SimulatedPlayer *p2,
                             Shotgun *shotgun)
    : Game(p1, p2) {
  this->shotgun = shotgun;
}

SimulatedGame::SimulatedGame(const SimulatedGame &other)
    : Game(nullptr, nullptr) {
  auto *simShotgun = dynamic_cast<SimulatedShotgun *>(other.shotgun);
  if (!simShotgun)
    throw std::logic_error("Trying to copy a SimulatedGame whose shotgun is "
                           "not SimulatedShotgun!");

  this->shotgun = new SimulatedShotgun(simShotgun->getTotalShellCount(),
                                       simShotgun->getLiveShellCount(),
                                       simShotgun->getBlankShellCount());

  this->playerOne = new SimulatedPlayer(*other.playerOne);
  this->playerTwo = new SimulatedPlayer(*other.playerTwo);

  this->isPlayerOneTurn = other.isPlayerOneTurn;

  if (this->playerOne && this->playerTwo) {
    this->playerOne->setOpponent(this->playerTwo);
    this->playerTwo->setOpponent(this->playerOne);
  }

  this->isPlayerOneTurn = other.isPlayerOneTurn;
}

void SimulatedGame::printShells() {
  throw std::logic_error(
      "SimulatedGame::printShells() should not be called in simulation.");
}

void SimulatedGame::runGame() {
  throw std::logic_error(
      "SimulatedGame::runGame() should not be called in simulation.");
}
