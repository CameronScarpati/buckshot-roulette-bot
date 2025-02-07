#include "BotPlayer.h"

BotPlayer::BotPlayer(const std::string& name, int health)
        : Player(name, health) {}

Action BotPlayer::chooseAction(const Shotgun& currentShotgun) {
    // TODO: Implement the bot's decision-making logic.
    // For now, a simple placeholder: always shoot the opponent.
    return SHOOT_OPPONENT;
}