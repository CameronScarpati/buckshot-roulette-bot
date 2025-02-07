#include "Game.h"

Game::Game(Player* p1, Player* p2, Mode mode)
        : player1(p1), player2(p2), mode(mode), currentRound(1) {
    // TODO: Initialize any additional game state and load the shotgun
}

void Game::runGame() {
    // TODO: Implement the main game loop:
    //   while (both players are alive and round conditions are met)
    //     call processTurn() alternating between players
}

void Game::processTurn(Player* currentPlayer) {
    // TODO: Get the action from currentPlayer and update the game state
    //   e.g., int action = currentPlayer->chooseAction(shotgun);
    //   then, if action == SHOOT_SELF, update health accordingly, etc.
}

bool Game::checkRoundEnd() {
    // TODO: Check if the shotgun is empty or if a player's health has dropped to zero
    return false;
}

void Game::resetRound() {
    // TODO: Reload the shotgun and increment currentRound (if needed)
}
