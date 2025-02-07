#include <iostream>
#include "Game.h"
#include "HumanPlayer.h"
#include "BotPlayer.h"

int main() {
    // TODO: Choose the game mode (e.g., HUMAN_VS_BOT or BOT_VS_BOT)
    // Example: Human vs. Bot
    const int initialHealth = 3; // or any appropriate starting value

    // For human vs. bot:
    HumanPlayer human("Player", initialHealth);
    BotPlayer dealer("Dealer", initialHealth);

    // For self-play (bot vs. bot), you would create two BotPlayer objects:
    // BotPlayer bot1("Bot1", initialHealth);
    // BotPlayer bot2("Bot2", initialHealth);

    // Create the game object in HUMAN_VS_BOT mode (see Game::Mode in Game.h)
    Game game(&human, &dealer, Game::HUMAN_VS_BOT);
    game.runGame();

    return 0;
}
