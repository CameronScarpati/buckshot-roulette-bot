#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H


#include "Player.h"
#include "Shotgun.h"

class Game {
public:
    enum Mode { HUMAN_VS_BOT, BOT_VS_BOT };

    Game(Player* p1, Player* p2, Mode mode);
    void runGame();  // Main game loop

private:
    Player* player1;   // e.g., the human or first bot
    Player* player2;   // e.g., the Dealer or second bot
    Shotgun shotgun;   // Manages shell state for the round
    int currentRound;
    Mode mode;

    void processTurn(Player* currentPlayer);  // Process a single turn
    bool checkRoundEnd();                       // Returns true if round is over
    void resetRound();                          // Reloads the shotgun and updates round
};


#endif //BUCKSHOT_ROULETTE_BOT_GAME_H
