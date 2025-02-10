#include "SimulatedPlayer.h"

SimulatedPlayer::SimulatedPlayer() : SimulatedPlayer(0) {}

SimulatedPlayer::SimulatedPlayer(int health) : health(health) {}

void SimulatedPlayer::loseHealth(bool sawUsed) {
  if (sawUsed)
    --health;
  --health;
}

int SimulatedPlayer::getHealth() const { return health; }

bool SimulatedPlayer::isDead() const { return health <= 0; }
