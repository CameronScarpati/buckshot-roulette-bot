#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Player.h"
#include "Simulations/SimulatedGame.h"
#include "Simulations/SimulatedPlayer.h"
#include "Simulations/SimulatedShotgun.h"
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @class BotPlayer
 * @brief AI-controlled player using expectiminimax for decision-making.
 */
class BotPlayer final : public Player {
private:
  // -- Terminal state evaluation scores --
  // Must exceed the maximum possible heuristic evaluation (~8665) so that
  // winning is always preferred over any non-terminal "good position".
  static constexpr float TERMINAL_LOSS_SCORE = -10000.0f;
  static constexpr float TERMINAL_WIN_SCORE = 10000.0f;

  // -- Heuristic evaluation weights --
  // Scales the normalized health differential (our HP vs. opponent HP).
  static constexpr float HEALTH_WEIGHT = 200.0f;
  // Scales the difference in total held item values between players.
  // Kept low so the bot prefers using items for strategic advantage over
  // hoarding them.  Max item score at 8: 8 * 8 * 40 = 2560 (well below
  // terminal 10000).
  static constexpr float ITEM_WEIGHT = 8.0f;
  // Bonus/penalty for whose turn it is (tempo advantage).
  static constexpr float TURN_WEIGHT = 50.0f;
  // Bonus when the opponent is handcuffed (they lose a turn).
  static constexpr float HANDCUFF_WEIGHT = 100.0f;
  // Bonus when we know the next shell (information advantage).
  // High value because knowing the shell enables perfect play: shoot self on
  // blank (free turn), shoot opponent on live (guaranteed damage).
  static constexpr float MAGNIFYING_GLASS_WEIGHT = 300.0f;
  // Bonus when handsaw is active on our turn (double damage potential).
  static constexpr float HANDSAW_WEIGHT = 80.0f;
  // Bonus when shell distribution is extreme (active player can choose optimally).
  static constexpr float SHELL_DISTRIBUTION_WEIGHT = 40.0f;
  // Bonus for holding complementary item combos.
  static constexpr float ITEM_SYNERGY_WEIGHT = 15.0f;

  // -- Individual item values for the evaluation heuristic --
  // Beer: ejects a shell (information/tempo gain).
  static constexpr float BEER_VALUE = 20.0f;
  // Cigarette: restores 1 HP.
  static constexpr float CIGARETTE_VALUE = 15.0f;
  // Handcuffs: denies the opponent a full turn.
  static constexpr float HANDCUFFS_VALUE = 40.0f;
  // Magnifying Glass: reveals shell type for optimal play.
  static constexpr float MAGNIFYING_GLASS_VALUE = 40.0f;
  // Handsaw: doubles next shot damage.
  static constexpr float HANDSAW_VALUE = 35.0f;

  // -- Search parameters --
  // Upper bound on iterative-deepening search depth.
  static constexpr int MAX_SEARCH_DEPTH = 12;
  // Starting depth for iterative-deepening search.
  static constexpr int MIN_SEARCH_DEPTH = 5;
  // Tolerance for floating-point probability comparisons.
  static constexpr float EPSILON = 0.0001f;
  // Maximum wall-clock time allowed for the search.
  // 3 seconds allows depth 7-8+ with most game states, enough to see
  // multi-item combos (e.g., MG → handcuffs → shoot) pay off.
  static constexpr std::chrono::milliseconds TIME_LIMIT{3000};

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