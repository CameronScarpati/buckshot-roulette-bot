#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H


#include "Player.h"

class BotPlayer : public Player {
public:
    BotPlayer(const std::string& name, int health);
    virtual Action chooseAction(const Shotgun& currentShotgun) override;
    // Additional instance variables for bot strategy can be added here.
};


#endif //BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
