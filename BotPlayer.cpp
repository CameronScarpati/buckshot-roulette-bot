#include "BotPlayer.h"

BotPlayer::BotPlayer(const std::string& name, int health)
        : Player(name, health) {}

Action BotPlayer::chooseAction(const Shotgun& currentShotgun) {
    return Action::ShootOpponent;
}