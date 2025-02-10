#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Player.h"
#include "Simulations/SimulatedGame.h"

/**
 * @brief A BotPlayer uses expectiminimax search to decide its action in
 * Buckshot Roulette.
 */
class BotPlayer : public Player {
private:
  /**
   * @brief Evaluates the value of a simulated game state from the bot's
   * perspective.
   * @param state The game state to evaluate.
   * @return A float representing how favorable the state is for the bot.
   */
  static float evaluateState(const SimulatedGame &state);

  /**
   * @brief Executes an action on the provided game state components.
   *
   * Mutates playerOne, playerTwo, and shotgun based on the action and shell
   * type.
   *
   * @param action The action to perform.
   * @param isPlayerOneTurn Indicates if it is currently Player One's turn.
   * @param playerOne Reference to Player One's state.
   * @param playerTwo Reference to Player Two's state.
   * @param shotgun Reference to the shotgun state.
   * @param shell The type of shell drawn (live or blank).
   * @return The updated turn indicator after performing the action.
   */
  static bool performAction(Action action, bool isPlayerOneTurn,
                            SimulatedPlayer &playerOne,
                            SimulatedPlayer &playerTwo,
                            SimulatedShotgun &shotgun, ShellType shell);

  /**
   * @brief Simulates the outcome of an action when a live shell is drawn.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return The new simulated game state after the live shell outcome.
   */
  static SimulatedGame simulateLiveAction(const SimulatedGame &state,
                                          Action action);

  /**
   * @brief Simulates the outcome of an action when a blank shell is drawn.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return The new simulated game state after the blank shell outcome.
   */
  static SimulatedGame simulateBlankAction(const SimulatedGame &state,
                                           Action action);

  /**
   * @brief Simulates an action by generating both possible outcomes: live and
   * blank shell.
   * @param state The current game state.
   * @param action The action to simulate.
   * @return A pair of simulated game states: { stateIfLive, stateIfBlank }.
   */
  static std::pair<SimulatedGame, SimulatedGame>
  simulateAction(const SimulatedGame &state, Action action);

  /**
   * @brief The expectiminimax search algorithm.
   *
   * Recursively explores game states to compute the expected value for the bot.
   *
   * @param state The current game state.
   * @param depth The depth limit for the search.
   * @param maximizingPlayer True if the current turn is for the maximizing
   * player.
   * @return The expected value of the state from the bot's perspective.
   */
  float expectiMiniMax(const SimulatedGame &state, int depth,
                       bool maximizingPlayer);

public:
  /**
   * @brief Constructs a BotPlayer with a given name and health.
   * @param name The bot's name.
   * @param health The bot's health.
   */
  BotPlayer(const std::string &name, int health);

  /**
   * @brief Constructs a BotPlayer with a given name, health, and opponent.
   * @param name The bot's name.
   * @param health The bot's health.
   * @param opponent Pointer to the opponent player.
   */
  BotPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief Chooses an action based on expectiminimax search using the current
   * shotgun state.
   * @param currentShotgun The real shotgun state to simulate from.
   * @return The selected action (e.g., SHOOT_SELF or SHOOT_OPPONENT).
   */
  Action chooseAction(const Shotgun *currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
