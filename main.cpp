#include "Game.h"
#include "HumanPlayer.h"
#include "BotPlayer.h"

int main() {
    // Choose the game mode: HUMAN_VS_BOT or BOT_VS_BOT
    // Example: Human vs. Bot
    const int initialHealth = 3; // Starting health for both players

    // For human vs. bot:
    HumanPlayer human("Player", initialHealth);
    BotPlayer dealer("Dealer", initialHealth);

    // For self-play (bot vs. bot), uncomment the following lines:
    // BotPlayer bot1("Bot1", initialHealth);
    // BotPlayer bot2("Bot2", initialHealth);

    // Create the game object in HUMAN_VS_BOT mode.
    Game game(&human, &dealer, Game::Mode::HUMAN_VS_BOT);
    game.runGame();

    return 0;
}
