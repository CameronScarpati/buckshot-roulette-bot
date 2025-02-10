#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATIONSTATE_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATIONSTATE_H

enum class Turn : int { BOT_TURN = 0, OPPONENT_TURN = 1 };

struct SimState {
  int player1Health;
  int player2Health;
  int liveShells;
  int totalShells;
  Turn playerTurn;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATIONSTATE_H
