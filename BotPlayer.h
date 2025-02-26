#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Player.h"
#include "Simulations/SimulatedGame.h"
#include "Simulations/SimulatedPlayer.h"
#include "Simulations/SimulatedShotgun.h"
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
  static constexpr float HEALTH_WEIGHT = 120.0f;
  static constexpr float ITEM_WEIGHT = 5.0f;
  static constexpr float SHELL_WEIGHT = 45.0f;
  static constexpr float TURN_WEIGHT = 85.0f;
  static constexpr float HANDCUFF_WEIGHT = 90.0f;
  static constexpr float MAGNIFYING_GLASS_WEIGHT = 115.0f;
  static constexpr float HANDSAW_WEIGHT = 70.0f;

  // State item values
  static constexpr float BEER_VALUE = 2.5f;
  static constexpr float CIGARETTE_VALUE = 7.5f;
  static constexpr float HANDCUFFS_VALUE = 15.0f;
  static constexpr float MAGNIFYING_GLASS_VALUE = 30.0f;
  static constexpr float HANDSAW_VALUE = 15.0f;

  // Search depth for expectiminimax
  static constexpr int SEARCH_DEPTH = 10;

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
   * @brief Sums up the evaluation values of a collection of items.
   * @param items A vector of item pointers.
   * @return The total evaluation score for the items.
   */
  [[nodiscard]] static float evaluateItems(const std::vector<Item *> &items);

  /**
   * @brief Simulates the result of an action.
   * @param action The action to perform.
   * @param state The current game state.
   * @param shell The drawn shell type.
   * @return Whether the turn switches.
   */
  [[nodiscard]] static bool performAction(Action action, SimulatedGame *state,
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
   * @return The expected value of the state.
   */
  [[nodiscard]] float expectiMiniMax(SimulatedGame *state, int depth);

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
};

#endif // BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H