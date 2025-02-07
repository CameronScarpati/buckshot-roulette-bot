#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H


#include "Player.h"
#include "SimulationState.h"

class BotPlayer : public Player {
private:
    /**
     * @brief This function evaluates the state created in the expectiMiniMax search.
     * @param state Recursive game state.
     * @return Float (How good the state is for the bot).
     */
    static float evaluateState(const SimState& state);

    /**
     * @brief This function changes the state according to an action and branches the outcomes.
     * @param state Recursive game state.
     * @param action The action to perform.
     * @return Pair<SimState, SimState> (Live/Blank).
     */
    static std::pair<SimState, SimState> simulateAction(const SimState &state, Action action);

    /**
     * @brief This function gives a value to the action it believes it should perform.
     * @param state Recursive game state.
     * @param depth Integer (How deep to recurse).
     * @param maximizingPlayer Boolean (Min/Max our outcomes).
     * @return Float (What action to choose).
     */
    float expectiMiniMax(const SimState& state, int depth, bool maximizingPlayer);

public:
    /**
     * @brief This constructor creates a bot player object with name and health
     * @param name String (Bot player identifier).
     * @param health Integer (Health amount).
     */
    BotPlayer(const std::string& name, int health);

    /**
     * @brief This constructor creates a bot player object with name, health and opponent.
     * @param name String (Bot player identifier).
     * @param health Integer (Health amount).
     */
    BotPlayer(const std::string& name, int health, Player* opponent);

    /**
     * @brief This function allows the bot to choose an action to perform.
     * @param currentShotgun Shotgun (Shotgun state)
     * @return Action that they performed.
     */
    Action chooseAction(const Shotgun& currentShotgun) override;
};


#endif //BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
