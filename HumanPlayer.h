#ifndef BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H


#include "Player.h"

class HumanPlayer : public Player {
public:
    HumanPlayer(const std::string& name, int health);
    virtual Action chooseAction(const Shotgun& currentShotgun) override;
};


#endif //BUCKSHOT_ROULETTE_BOT_HUMANPLAYER_H
