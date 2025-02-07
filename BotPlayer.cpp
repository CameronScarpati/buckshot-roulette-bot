#include "BotPlayer.h"

BotPlayer::BotPlayer(const std::string& name, int health)
        : Player(name, health) {}

Action BotPlayer::chooseAction(const Shotgun& currentShotgun) {
    int liveShells = currentShotgun.liveShellCount();
    if (liveShells == 0) return Action::SHOOT_SELF;
    float probabilityOfLive = static_cast<float>(liveShells) /
                              static_cast<float>(currentShotgun.totalShellCount());
    return (probabilityOfLive <= 0.25) ? Action::SHOOT_SELF : Action::SHOOT_OPPONENT;
}