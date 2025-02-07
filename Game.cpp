#include "Game.h"
#include <stdexcept>
#include <iostream>

Game::Game(Player* p1, Player* p2, Mode mode)
        : player1(p1), player2(p2), mode(mode), currentRound(1), shotgun(Shotgun()), playerWinStreak(0) {
}

void Game::printStatus() const {
    std::cout << "----- Current Game Status -----" << std::endl;
    std::cout << "Player win streak is: " << playerWinStreak << std::endl;
    std::cout << player1->getName() << " Health: " << player1->getHealth() << std::endl;
    std::cout << player2->getName() << " Health: " << player2->getHealth() << std::endl;
    int live = shotgun.liveShellCount();
    int total = shotgun.totalShellCount();
    int blank = total - live;
    std::cout << "Shotgun: " << live << " live shells, "
              << blank << " blank shells, out of " << total << " total shells." << std::endl;
    std::cout << "-------------------------------" << std::endl;
}

void Game::runGame() {
    // Starting with player1 as the first actor.
    Player* currentPlayer = player1;
    Player* opponent = player2;

    std::cout << "Buckshot Roulette game starting. Good luck!" << std::endl;

    // Main game loop: run rounds until win conditions are met.
    while (true) {
        printStatus();

        if (shotgun.isEmpty()) {
            if (!player2->isAlive()) {
                std::cout << player1->getName() << " wins the round!" << std::endl;
                playerWinStreak++;
                currentRound++;
                std::cout << "----- Round reset. Now starting round " << currentRound << ". -----" << std::endl;
            } else if (!player1->isAlive()) {
                std::cout << player2->getName() << " wins the round! Game over." << std::endl;
                break;
            }

            // Check if the player has won 3 consecutive rounds.
            if (playerWinStreak >= 3) {
                std::cout << player1->getName() << " wins the game by beating the Dealer in 3 consecutive rounds!" << std::endl;
                break;
            }

            shotgun = Shotgun();
            currentPlayer = player1;
            opponent = player2;
            continue;
        }

        // Process the current player's turn.
        processTurn(currentPlayer);

        // Check if the round ended because of a player's death.
        if (checkRoundEnd()) {
            if (!player1->isAlive()) {
                std::cout << player2->getName() << " wins the round! Game over." << std::endl;
                break;
            } else if (!player2->isAlive()) {
                std::cout << player1->getName() << " wins the round!" << std::endl;
                playerWinStreak++;
                if (playerWinStreak >= 3) {
                    std::cout << player1->getName() << " wins the game by beating the Dealer in 3 consecutive rounds!" << std::endl;
                    break;
                }
                shotgun = Shotgun();
                player1->resetHealth();
                player2->resetHealth();
                currentPlayer = player1;
                opponent = player2;
                continue;
            }
        }

        // For this implementation:
        // - A turn where the player shoots themselves with a BLANK grants an extra turn (handled recursively).
        // - Otherwise, the turn passes to the opponent.
        if (currentPlayer == player1) {
            currentPlayer = player2;
            opponent = player1;
        } else {
            currentPlayer = player1;
            opponent = player2;
        }
    }

    // End game: declare the winner.
    std::cout << "Game Over!" << std::endl;
    if (player1->isAlive())
        std::cout << player1->getName() << " wins the game!" << std::endl;
    else
        std::cout << player2->getName() << " wins the game!" << std::endl;
}

void Game::processTurn(Player* currentPlayer) {
    Player* opponent = (currentPlayer == player1) ? player2 : player1;

    Action action = currentPlayer->chooseAction(shotgun);
    ShellType shell = shotgun.getNextShell();
    std::cout << currentPlayer->getName() << " chooses to ";

    // Process the action based on the chosen action and the type of shell.
    if (action == Action::SHOOT_SELF) {
        std::cout << "shoot themselves";
        if (shell == ShellType::LIVE_SHELL) {
            std::cout << " with a LIVE shell! Losing 1 health." << std::endl;
            currentPlayer->loseHealth(false);
        } else { // BLANK shell: extra turn granted.
            std::cout << " with a BLANK shell. They gain an extra turn." << std::endl;
            if (!checkRoundEnd()) {
                if (shotgun.isEmpty()) {
                    std::cout << "The shotgun is now empty and will be reloaded." << std::endl;
                    printStatus();
                    shotgun = Shotgun();
                }
                processTurn(currentPlayer);
            } else
                std::cout << "But the round ended after this shot." << std::endl;
        }
    } else if (action == Action::SHOOT_OPPONENT) {
        std::cout << "shoot the opponent";
        if (shell == ShellType::LIVE_SHELL) {
            std::cout << " with a LIVE shell! " << opponent->getName() << " loses 1 health." << std::endl;
            opponent->loseHealth(false);
        } else
            std::cout << " with a BLANK shell. Turn passes to the opponent." << std::endl;
    } else
        throw std::runtime_error("Invalid action chosen by player.");
}

bool Game::checkRoundEnd() {
    return !player1->isAlive() || !player2->isAlive();
}
