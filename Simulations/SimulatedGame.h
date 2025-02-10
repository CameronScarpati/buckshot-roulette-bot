#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H

#include "Game.h"
#include "SimulatedPlayer.h"
#include "SimulatedShotgun.h"

class SimulatedGame {
public:
  SimulatedPlayer playerOne;
  SimulatedPlayer playerTwo;
  SimulatedShotgun shotgun;
  bool isPlayerOneTurn;

public:
  SimulatedGame(SimulatedPlayer playerOne, SimulatedPlayer playerTwo,
                SimulatedShotgun shotgun, bool isPlayerTurn);
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDGAME_H
