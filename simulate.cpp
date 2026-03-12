#include "BotPlayer.h"
#include "Game.h"
#include <iostream>
#include <string>

static constexpr int INITIAL_HEALTH = 3;
static constexpr int DEFAULT_NUM_GAMES = 1000;

int main(int argc, char *argv[]) {
  int numGames = DEFAULT_NUM_GAMES;
  bool verbose = false;
  if (argc > 1) {
    numGames = std::stoi(argv[1]);
  }
  if (argc > 2 && std::string(argv[2]) == "-v") {
    verbose = true;
  }

  // Suppress cout during simulation for speed
  std::streambuf *origBuf = std::cout.rdbuf();

  int bot1Wins = 0;
  int bot2Wins = 0;

  for (int i = 0; i < numGames; i++) {
    // Reset maxHealth each game
    Player::resetMaxHealth(0);

    auto *bot1 = new BotPlayer("Bot1", INITIAL_HEALTH);
    auto *bot2 = new BotPlayer("Bot2", INITIAL_HEALTH, bot1);
    bot1->setOpponent(bot2);

    // Suppress output during games unless verbose
    if (!verbose)
      std::cout.rdbuf(nullptr);

    Game game(bot1, bot2, (i % 2 == 0));
    game.runGame();

    // Restore output
    if (!verbose)
      std::cout.rdbuf(origBuf);

    // Determine winner based on round win counts
    if (game.getPlayerOneWins() > game.getPlayerTwoWins()) {
      bot1Wins++;
    } else {
      bot2Wins++;
    }

    if (!verbose) {
      std::cerr << "Game " << (i + 1) << "/" << numGames << " done. "
                << "P1: " << bot1Wins << " P2: " << bot2Wins << "\r";
    }

    delete bot1;
    delete bot2;
  }

  std::cout << "\nResults after " << numGames << " games:\n";
  std::cout << "Bot1 wins: " << bot1Wins << " ("
            << (100.0 * static_cast<double>(bot1Wins) /
                static_cast<double>(numGames))
            << "%)\n";
  std::cout << "Bot2 wins: " << bot2Wins << " ("
            << (100.0 * static_cast<double>(bot2Wins) /
                static_cast<double>(numGames))
            << "%)\n";

  return 0;
}
