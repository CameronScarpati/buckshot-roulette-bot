#ifndef BUCKSHOT_ROULETTE_BOT_PLAYER_H
#define BUCKSHOT_ROULETTE_BOT_PLAYER_H


#include "Shotgun.h"
#include <string>

/**
 * @brief This enum defines the actions you can perform.
 */
enum class Action : int {
    ShootSelf = 0,
    ShootOpponent = 1
};

/**
 * @brief This class defines a player's actions.
 */
class Player {
public:
    /**
     * @brief This constructor creates a player object with name and health.
     * @param name String (Player identifier).
     * @param health Integer (Health amount).
     */
    Player(std::string name, int health);

    /**
     * @brief This function allows the player to choose an action to perform.
     * @param currentShotgun Shotgun (Shotgun state)
     * @return Action that they performed.
     */
    virtual Action chooseAction(const Shotgun& currentShotgun) = 0;

    /**
     * @brief This function changes how much health I have when I get shot.
     * @param sawUsed Boolean (Saw item used for double damage).
     */
    void loseHealth(bool sawUsed);

    /**
     * @brief Heal one health.
     */
    void useStimulant();

    /**
     * @brief Changes the new max health and resets current.
     * @param newHealth Integer (New maximum health).
     */
    void resetHealth(int newHealth);

    /**
     * @brief Checks if a player is alive.
     * @return Boolean (Player alive/dead).
     */
    bool isAlive() const;

    /**
     * @brief Getter.
     * @return String (Player identifier).
     */
    std::string getName() const;

protected:
    std::string name;
    int health;
    static int maxHealth;
};


#endif //BUCKSHOT_ROULETTE_BOT_PLAYER_H
