#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Player.h"
#include "Simulations/SimulatedGame.h"
#include "Simulations/SimulatedPlayer.h"
#include "Simulations/SimulatedShotgun.h"

/**
 * @class BotPlayer
 * @brief AI-controlled player using expectiminimax for decision-making.
 */
class BotPlayer : public Player {
private:
  /**
   * @brief Evaluates the favorability of a game state.
   * @param state The game state to evaluate.
   * @return A score representing how advantageous the state is for the bot.
   */
  static float evaluateState(SimulatedGame *state);

  /**
   * @brief Simulates the result of an action.
   * @param action The action to perform.
   * @param state The current game state.
   * @param shell The drawn shell type.
   * @return Whether the turn switches.
   */
  static bool performAction(Action action, SimulatedGame *state,
                            ShellType shell);

  /**
   * @brief Simulates an action with a live shell outcome.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return The resulting game state.
   */
  static SimulatedGame *simulateLiveAction(SimulatedGame *state, Action action);

  /**
   * @brief Simulates an action with a blank shell outcome.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return The resulting game state.
   */
  static SimulatedGame *simulateBlankAction(SimulatedGame *state,
                                            Action action);

  /**
   * @brief Simulates an action with both possible shell outcomes.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return A pair of game states: { stateIfLive, stateIfBlank }.
   */
  static std::pair<SimulatedGame *, SimulatedGame *>
  simulateAction(SimulatedGame *state, Action action);

  /**
   * @brief Expectiminimax search algorithm.
   * @param state The current game state.
   * @param depth Search depth.
   * @param maximizingPlayer True if the bot is maximizing.
   * @return The expected value of the state.
   */
  float expectiMiniMax(SimulatedGame *state, int depth, bool maximizingPlayer);

public:
  /**
   * @brief Constructs a bot player.
   * @param name The bot's name.
   * @param health Initial health.
   */
  BotPlayer(const std::string &name, int health);

  /**
   * @brief Constructs a bot player with an opponent.
   * @param name The bot's name.
   * @param health Initial health.
   * @param opponent Pointer to the opponent.
   */
  BotPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief Chooses an action using expectiminimax search.
   * @param currentShotgun The shotgun state.
   * @return The chosen action.
   */
  Action chooseAction(const Shotgun *currentShotgun) override;

  /**
   * @brief Determines feasible actions based on the current game state.
   * @param state The simulated game state.
   * @return A list of possible actions.
   */
  std::vector<Action> determineFeasibleActions(SimulatedGame *state);
};

#endif // BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
