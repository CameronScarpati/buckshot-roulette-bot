#ifndef BUCKSHOT_ROULETTE_BOT_PLAYER_H
#define BUCKSHOT_ROULETTE_BOT_PLAYER_H


#include "Shotgun.h"
#include <string>

enum Action { SHOOT_SELF, SHOOT_OPPONENT };

class Player {
public:
    Player(const std::string& name, int health);
    virtual ~Player();

    // Pure virtual function: each derived class must implement its own decision logic.
    virtual Action chooseAction(const Shotgun& currentShotgun) = 0;

    void loseHealth(int amount);
    bool isAlive() const;
    std::string getName() const;

protected:
    std::string name;
    int health;
};


#endif //BUCKSHOT_ROULETTE_BOT_PLAYER_H
