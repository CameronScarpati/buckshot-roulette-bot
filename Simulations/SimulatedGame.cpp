#include "SimulatedGame.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"

SimulatedGame::SimulatedGame(Player *p1, Player *p2, Shotgun *shotgun)
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

  auto *p1 = dynamic_cast<SimulatedPlayer *>(other.playerOne);
  auto *p2 = dynamic_cast<SimulatedPlayer *>(other.playerTwo);

  if (!p1 || !p2)
    throw std::logic_error(
        "SimulatedGame copy: players must be SimulatedPlayers!");

  auto *newP1 = new SimulatedPlayer(p1->getName(), p1->getHealth());
  auto *newP2 = new SimulatedPlayer(p2->getName(), p2->getHealth(), newP1);
  newP1->setOpponent(newP2);

  this->playerOne = newP1;
  this->playerTwo = newP2;

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
