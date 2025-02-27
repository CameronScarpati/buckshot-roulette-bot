#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Player.h"
#include "Simulations/SimulatedGame.h"
#include "Simulations/SimulatedPlayer.h"
#include "Simulations/SimulatedShotgun.h"
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

/**
 * @class BotPlayer
 * @brief AI-controlled player using expectiminimax for decision-making.
 */
class BotPlayer final : public Player {
private:
  // State evaluation weights
  static constexpr float HEALTH_WEIGHT = 150.0f;
  static constexpr float ITEM_WEIGHT = 15.0f;
  static constexpr float SHELL_WEIGHT = 60.0f;
  static constexpr float TURN_WEIGHT = 85.0f;
  static constexpr float HANDCUFF_WEIGHT = 90.0f;
  static constexpr float MAGNIFYING_GLASS_WEIGHT = 180.0f;
  static constexpr float HANDSAW_WEIGHT = 70.0f;
  static constexpr float ITEM_COUNT_WEIGHT = 5.0f;
  static constexpr float DANGEROUS_TURN_PENALTY = 80.0f;

  // State item values - Adjusted
  static constexpr float BEER_VALUE = 5.0f;
  static constexpr float CIGARETTE_VALUE = 20.0f;
  static constexpr float HANDCUFFS_VALUE = 25.0f;
  static constexpr float MAGNIFYING_GLASS_VALUE = 50.0f;
  static constexpr float HANDSAW_VALUE = 20.0f;

  // Search parameters
  static constexpr int MAX_SEARCH_DEPTH = 10;
  static constexpr int MIN_SEARCH_DEPTH = 5;
  static constexpr float EPSILON = 0.0001f;
  static constexpr std::chrono::milliseconds TIME_LIMIT{
      1000}; // 1000ms time limit

  /**
   * @brief Returns a numerical value for an item (for evaluation purposes)
   * @param item The item to evaluate.
   * @return A float value representing the item's worth.
   */
  [[nodiscard]] static float valueOfItem(const Item *item);

  /**
   * @brief Evaluates the favorability of a game state.
   * @param state The game state to evaluate.
   * @return A score representing how advantageous the state is for the bot.
   */
  [[nodiscard]] static float evaluateState(SimulatedGame *state);

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
  [[nodiscard]] static std::unique_ptr<SimulatedGame>
  simulateLiveAction(SimulatedGame *state, Action action);

  /**
   * @brief Simulates an action with a blank shell outcome.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return The resulting game state.
   */
  [[nodiscard]] static std::unique_ptr<SimulatedGame>
  simulateBlankAction(SimulatedGame *state, Action action);

  /**
   * @brief Simulates an action with both possible shell outcomes.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return A pair of game states: { stateIfLive, stateIfBlank }.
   */
  [[nodiscard]] static std::pair<std::unique_ptr<SimulatedGame>,
                                 std::unique_ptr<SimulatedGame>>
  simulateAction(SimulatedGame *state, Action action);

  /**
   * @brief Computes the expected value for a given action.
   * @param state The current game state.
   * @param action The action to evaluate.
   * @param depth The remaining search depth.
   * @return The expected value of performing the action on the state.
   */
  [[nodiscard]] float expectedValueForAction(SimulatedGame *state,
                                             Action action, int depth);

  /**
   * @brief Expectiminimax search algorithm.
   * @param state The current game state.
   * @param depth Search depth.
   * @param alpha Alpha value for pruning (best value for MAX)
   * @param beta Beta value for pruning (best value for MIN)
   * @param startTime Start time of the search for time-limited search
   * @return The expected value of the state.
   */
  [[nodiscard]] float
  expectiMiniMax(SimulatedGame *state, int depth,
                 float alpha = -std::numeric_limits<float>::infinity(),
                 float beta = std::numeric_limits<float>::infinity(),
                 std::chrono::steady_clock::time_point startTime =
                     std::chrono::steady_clock::now());

  /**
   * @brief Directly simulates actions that don't involve probabilistic shell
   * outcomes.
   * @param state The current game state.
   * @param action The non-probabilistic action (item usage).
   * @return The resulting game state.
   */
  [[nodiscard]] static std::unique_ptr<SimulatedGame>
  simulateNonProbabilisticAction(SimulatedGame *state, Action action);

  /**
   * @brief Checks if the current search should be aborted due to time
   * constraints.
   * @param startTime The start time of the search.
   * @return True if time limit has been reached.
   */
  [[nodiscard]] static bool
  timeExpired(const std::chrono::steady_clock::time_point &startTime) {
    auto elapsedTime = std::chrono::steady_clock::now() - startTime;
    return elapsedTime > TIME_LIMIT;
  }

public:
  /**
   * @brief Constructs a bot player.
   * @param name The bot's name.
   * @param health Initial health.
   */
  BotPlayer(std::string name, int health);

  /**
   * @brief Constructs a bot player with an opponent.
   * @param name The bot's name.
   * @param health Initial health.
   * @param opponent Pointer to the opponent.
   */
  BotPlayer(std::string name, int health, Player *opponent);

  /**
   * @brief Guaranteed live shell choices.
   * @param currentShotgun The current shotgun state.
   * @return Optimal action given guaranteed live shell.
   */
  [[nodiscard]] Action liveShellAction(Shotgun *currentShotgun);

  /**
   * @brief Chooses an action using expectiminimax search.
   * @param currentShotgun The shotgun state.
   * @return The chosen action.
   */
  [[nodiscard]] Action chooseAction(Shotgun *currentShotgun) override;

  /**
   * @brief Determines feasible actions based on the current game state.
   * @param state The simulated game state.
   * @return A list of possible actions.
   */
  [[nodiscard]] static std::vector<Action>
  determineFeasibleActions(SimulatedGame *state);

  /**
   * @brief Prioritizes certain strategic actions for more effective play.
   * @param actions Vector of feasible actions.
   * @param state The current game state.
   * @return The prioritized list of actions.
   */
  [[nodiscard]] static std::vector<Action>
  prioritizeStrategicActions(const std::vector<Action> &actions,
                             SimulatedGame *state);
};

#endif // BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H