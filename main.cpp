#include "Game.h"
#include "HumanPlayer.h"
#include "BotPlayer.h"

int main() {
    const int initialHealth = 3;

    auto* human = new HumanPlayer("Cameron", initialHealth);
    auto* dealer = new BotPlayer("Dealer", initialHealth, human);
    human->setOpponent(dealer);

    Game game(human, dealer);
    game.runGame();

    return 0;
}
