#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H

#include "Shotgun.h"

class SimulatedShotgun {
private:
  int totalShells;
  int liveShells;
  int blankShells;

public:
  SimulatedShotgun(int shells, int lives, int blanks);

  void getLiveShell();

  void getBlankShell();

  int getLiveShellCount() const;

  int getBlankShellCount() const;

  int getTotalShellCount() const;

  float getLiveShellProbability() const;

  float getBlankShellProbability() const;

  bool isEmpty() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
