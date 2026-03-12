#include "BotPlayer.h"
#include "Game.h"
#include "HumanPlayer.h"

// Starting hit points for each player at the beginning of every round.
static constexpr int INITIAL_HEALTH = 3;

int main() {
  constexpr int initialHealth = INITIAL_HEALTH;

  auto *human = new HumanPlayer("Cameron", initialHealth);
  auto *dealer = new BotPlayer("Dealer", initialHealth, human);
  human->setOpponent(dealer);

  Game game(human, dealer, true);
  game.runGame();

  return 0;
}
