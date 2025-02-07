#ifndef BUCKSHOT_ROULETTE_BOT_GAME_H
#define BUCKSHOT_ROULETTE_BOT_GAME_H


#include "Player.h"
#include "Shotgun.h"
#include <iostream>

/**
 * @brief The Game class manages the overall game loop, rounds, and turn order.
 * Inspired by the Steam guide for Buckshot Roulette,
 * this implementation supports both Human vs. Bot and Bot vs. Bot modes.
 */
class Game {
public:
    /**
     * @brief This enum defines the modes of gameplay.
     */
    enum class Mode : int {
        HUMAN_VS_BOT = 0,
        BOT_VS_BOT   = 1
    };

    /**
     * @brief Constructs a new Game instance.
     * @param p1 Pointer to player1 (the human or first bot).
     * @param p2 Pointer to player2 (the Dealer or second bot).
     * @param mode Mode of the game (HUMAN_VS_BOT or BOT_VS_BOT).
     */
    Game(Player* p1, Player* p2, Mode mode);

    /**
     * @brief Prints the status of the game.
     */
    void printStatus() const;

    /**
     * @brief Runs the main game loop until one player loses.
     */
    void runGame();

    /**
     * @brief Getter.
     * @return Integer (Player health).
     */
    int getPlayerOneHealth();

    /**
    * @brief Getter.
    * @return Integer (Player health).
    */
    int getPlayerTwoHealth();

private:
    Player* player1;
    Player* player2;
    Shotgun shotgun;
    int currentRound;
    Mode mode;
    int playerWinStreak;

    /**
     * @brief Processes a single turn for the given player.
     * Retrieves the player's chosen action, fires the next shell,
     * updates health accordingly, and (if applicable) handles extra turns.
     * @param currentPlayer Pointer to the player whose turn is being processed.
     */
    void processTurn(Player* currentPlayer);

    /**
     * @brief Checks if the current round is over.
     * A round ends when the shotgun is empty or if a player's health drops to zero.
     * @return true if the round is over, false otherwise.
     */
    bool checkRoundEnd();
};


#endif // BUCKSHOT_ROULETTE_BOT_GAME_H
