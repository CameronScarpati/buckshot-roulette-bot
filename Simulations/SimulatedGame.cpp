#include "SimulatedGame.h"

SimulatedGame::SimulatedGame(SimulatedPlayer playerOne,
                             SimulatedPlayer playerTwo,
                             SimulatedShotgun shotgun, bool isPlayerOneTurn)
    : playerOne(playerOne), playerTwo(playerTwo), shotgun(shotgun),
      isPlayerOneTurn(isPlayerOneTurn) {}
