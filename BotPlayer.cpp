#include "BotPlayer.h"
#include <limits>
#include <algorithm>

float BotPlayer::evaluateState(const SimState& state) {
    if (state.player1Health <= 0)
        return 50;
    if (state.player2Health <= 0)
        return -50;

    float healthDiff = static_cast<float>(state.player2Health)
                    - static_cast<float>(state.player1Health);
    float shellBonus = static_cast<float>(state.totalShells) * 0.5f; // More options multiplier.
    return healthDiff + shellBonus;
}

// Simulate an action given a state and return a pair of states:
//   - first: outcome if a live shell is drawn,
//   - second: outcome if a blank shell is drawn.
std::pair<SimState, SimState> BotPlayer::simulateAction(const SimState &state, Action action) {
    SimState liveState = state;
    SimState blankState = state;

    if (action == Action::SHOOT_SELF) {
        if (state.playerTurn == Turn::OPPONENT_TURN)
            liveState.player1Health -= 1;
        else
            liveState.player2Health -= 1;

        // If it was live, swap turns, else, keep your turn.
        liveState.playerTurn = (state.playerTurn == Turn::OPPONENT_TURN)
                               ? Turn::BOT_TURN
                               : Turn::OPPONENT_TURN;
        blankState.playerTurn = state.playerTurn;
    } else { // SHOOT_OPPONENT
        if (state.playerTurn == Turn::OPPONENT_TURN)
            liveState.player2Health -= 1;
        else
            liveState.player1Health -= 1;

        liveState.playerTurn = (state.playerTurn == Turn::OPPONENT_TURN)
                               ? Turn::BOT_TURN
                               : Turn::OPPONENT_TURN;
        blankState.playerTurn = (state.playerTurn == Turn::OPPONENT_TURN)
                                ? Turn::BOT_TURN
                                : Turn::OPPONENT_TURN;
    }

    liveState.totalShells -= 1;
    blankState.totalShells -= 1;

    if (state.liveShells > 0)
        liveState.liveShells = state.liveShells - 1;

    return { liveState, blankState };
}

float BotPlayer::expectiMiniMax(const SimState &state, int depth, bool maximizingPlayer) {
    if (depth == 0 || state.player1Health <= 0 || state.player2Health <= 0 || state.totalShells <= 0)
        return evaluateState(state);

    if (maximizingPlayer) {
        float bestValue = -std::numeric_limits<float>::infinity();

        for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
            auto action = static_cast<Action>(actionIndex);
            const float probabilityOfLiveShell = (state.totalShells > 0)
                                                 ? static_cast<float>(state.liveShells) / state.totalShells
                                                 : 0.0f;
            const float probabilityOfBlankShell = 1.0f - probabilityOfLiveShell;

            // If the probability is deterministic, prune the chance node.
            if (probabilityOfLiveShell == 0.0f) {
                auto outcomes = simulateAction(state, action);
                bool flip = (action == Action::SHOOT_OPPONENT);
                float branchValue = expectiMiniMax(outcomes.second, depth - 1, flip ? !maximizingPlayer : maximizingPlayer);
                bestValue = std::max(bestValue, branchValue);
                continue;
            } else if (probabilityOfLiveShell == 1.0f) {
                auto outcomes = simulateAction(state, action);
                bool flip = true;
                float branchValue = expectiMiniMax(outcomes.first, depth - 1, flip ? !maximizingPlayer : maximizingPlayer);
                bestValue = std::max(bestValue, branchValue);
                continue;
            }

            // Otherwise, consider both live and blank outcomes.
            std::pair<SimState, SimState> outcomes = simulateAction(state, action);
            SimState liveState  = outcomes.first;
            SimState blankState = outcomes.second;
            bool flipBlank = action != Action::SHOOT_SELF;

            float liveValue  = expectiMiniMax(liveState,  depth - 1, !maximizingPlayer);
            float blankValue = expectiMiniMax(blankState, depth - 1, flipBlank ? !maximizingPlayer : maximizingPlayer);
            float actionValue = probabilityOfLiveShell * liveValue + probabilityOfBlankShell * blankValue;

            bestValue = std::max(bestValue, actionValue);
        }
        return bestValue;

    } else { // Minimizing player's turn
        float bestValue = std::numeric_limits<float>::infinity();

        for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
            auto action = static_cast<Action>(actionIndex);
            const float probabilityOfLiveShell = (state.totalShells > 0)
                                                 ? static_cast<float>(state.liveShells) / state.totalShells
                                                 : 0.0f;
            const float probabilityOfBlankShell = 1.0f - probabilityOfLiveShell;

            if (probabilityOfLiveShell == 0.0f) {
                auto outcomes = simulateAction(state, action);
                bool flip = (action == Action::SHOOT_OPPONENT);
                float branchValue = expectiMiniMax(outcomes.second, depth - 1, flip ? !maximizingPlayer : maximizingPlayer);
                bestValue = std::min(bestValue, branchValue);
                continue;
            } else if (probabilityOfLiveShell == 1.0f) {
                auto outcomes = simulateAction(state, action);
                float branchValue = expectiMiniMax(outcomes.first, depth - 1, !maximizingPlayer);
                bestValue = std::min(bestValue, branchValue);
                continue;
            }

            std::pair<SimState, SimState> outcomes = simulateAction(state, action);
            SimState liveState  = outcomes.first;
            SimState blankState = outcomes.second;
            bool flipBlank = action != Action::SHOOT_SELF;

            float liveValue  = expectiMiniMax(liveState,  depth - 1, !maximizingPlayer);
            float blankValue = expectiMiniMax(blankState, depth - 1, flipBlank ? !maximizingPlayer : maximizingPlayer);
            float actionValue = probabilityOfLiveShell * liveValue + probabilityOfBlankShell * blankValue;

            bestValue = std::min(bestValue, actionValue);
        }
        return bestValue;
    }
}

BotPlayer::BotPlayer(const std::string& name, int health)
        : Player(name, health) {}

BotPlayer::BotPlayer(const std::string& name, int health, Player* opponent)
        : Player(name, health, opponent) {}

Action BotPlayer::chooseAction(const Shotgun& currentShotgun) {
    const float liveShellProbability =
            static_cast<float>(currentShotgun.liveShellCount())
            / static_cast<float>(currentShotgun.totalShellCount());
    const float blankShellProbability = 1.0f - liveShellProbability;
    if (liveShellProbability == 0.0f) {
        return Action::SHOOT_SELF;
    } else if (liveShellProbability == 1.0f)
        return Action::SHOOT_OPPONENT;

    SimState initState{};
    initState.player1Health = opponent->getHealth();
    initState.player2Health = this->getHealth();
    initState.liveShells = currentShotgun.liveShellCount();
    initState.totalShells = currentShotgun.totalShellCount();
    initState.playerTurn = Turn::BOT_TURN;

    int searchDepth = 10;
    float bestValue = -std::numeric_limits<float>::infinity();
    Action bestAction = Action::SHOOT_SELF;

    // Evaluate both possible actions (SHOOT_SELF, SHOOT_OPPONENT).
    for (int actionIndex = 0; actionIndex < 2; ++actionIndex) {
        auto action = static_cast<Action>(actionIndex);
        std::pair<SimState, SimState> outcomes = simulateAction(initState, action);
        SimState liveState  = outcomes.first;
        SimState blankState = outcomes.second;

        float liveVal  = expectiMiniMax(liveState,  searchDepth - 1, false);
        float blankVal = expectiMiniMax(blankState, searchDepth - 1, action == Action::SHOOT_SELF);

        float actionValue = liveShellProbability * liveVal + blankShellProbability * blankVal;

        if (actionValue > bestValue) {
            bestValue = actionValue;
            bestAction = action;
        }
    }
    return bestAction;
}
