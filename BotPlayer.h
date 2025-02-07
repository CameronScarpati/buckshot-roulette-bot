#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H


#include "Player.h"

class BotPlayer : public Player {
public:
    /**
     * @brief This constructor creates a bot player object with name and health.
     * @param name String (Human player identifier).
     * @param health Integer (Health amount).
     */
    BotPlayer(const std::string& name, int health);

    /**
     * @brief This function allows the bot to choose an action to perform.
     * @param currentShotgun Shotgun (Shotgun state)
     * @return Action that they performed.
     */
    Action chooseAction(const Shotgun& currentShotgun) override;
};


#endif //BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
