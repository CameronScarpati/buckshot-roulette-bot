#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Items/Item.h"
#include "Player.h"
#include "Simulations/SimulatedGame.h"
#include "Simulations/SimulatedPlayer.h"
#include "Simulations/SimulatedShotgun.h"
#include <utility>
#include <vector>

/**
 * @class BotPlayer
 * @brief AI-controlled player using expectiminimax for decision-making.
 */
class BotPlayer : public Player {
private:
  // Returns a numerical value for an item (for evaluation purposes)
  static float valueOfItem(const Item *item);

  /**
   * @brief Evaluates the favorability of a game state.
   * @param state The game state to evaluate.
   * @return A score representing how advantageous the state is for the bot.
   */
  static float evaluateState(SimulatedGame *state);

  /**
   * @brief Sums up the evaluation values of a collection of items.
   * @param items A vector of item pointers.
   * @return The total evaluation score for the items.
   */
  static float evaluateItems(const std::vector<Item *> &items);

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
   * @brief Computes the expected value for a given action.
   * @param state The current game state.
   * @param action The action to evaluate.
   * @param depth The remaining search depth.
   * @return The expected value of performing the action on the state.
   */
  float expectedValueForAction(SimulatedGame *state, Action action, int depth);

  /**
   * @brief Expectiminimax search algorithm.
   * @param state The current game state.
   * @param depth Search depth.
   * @return The expected value of the state.
   */
  float expectiMiniMax(SimulatedGame *state, int depth);

  static constexpr int SEARCH_DEPTH = 10;

  // State evaluation weights
  static constexpr float HEALTH_WEIGHT = 120.0;
  static constexpr float ITEM_WEIGHT = 5.0;
  static constexpr float SHELL_WEIGHT = 45.0;
  static constexpr float TURN_WEIGHT = 85.0;
  static constexpr float HANDCUFF_WEIGHT = 90.0;
  static constexpr float MAGNIFYING_GLASS_WEIGHT = 115.0;
  static constexpr float HANDSAW_WEIGHT = 70.0;

  // State item values
  static constexpr float BEER_VALUE = 2.5f;
  static constexpr float CIGARETTE_VALUE = 7.5f;
  static constexpr float HANDCUFFS_VALUE = 15.0f;
  static constexpr float MAGNIFYING_GLASS_VALUE = 30.0f;
  static constexpr float HANDSAW_VALUE = 15.0f;

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
   * @brief Guaranteed live shell choices.
   * @return Optimal action given guaranteed live shell.
   */
  Action liveShellAction(Shotgun *currentShotgun);

  /**
   * @brief Chooses an action using expectiminimax search.
   * @param currentShotgun The shotgun state.
   * @return The chosen action.
   */
  Action chooseAction(Shotgun *currentShotgun) override;

  /**
   * @brief Determines feasible actions based on the current game state.
   * @param state The simulated game state.
   * @return A list of possible actions.
   */
  static std::vector<Action> determineFeasibleActions(SimulatedGame *state);
};

#endif // BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
