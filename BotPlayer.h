#ifndef BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
#define BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H

#include "Player.h"
#include "Simulations/SimulatedGame.h"

/**
 * @brief A BotPlayer uses an expectiminimax-based search to decide which action
 *        to take in Buckshot Roulette.
 */
class BotPlayer : public Player {
private:
  /**
   * @brief Evaluate the 'goodness' of a given simulated state from the bot's
   * perspective.
   * @param state The game state to evaluate.
   * @return A float rating of how favorable the state is for the bot.
   */
  static float evaluateState(const SimulatedGame &state);

  /**
   * @brief Execute an action on the given state references (playerOne,
   * playerTwo, shotgun). This mutates those references accordingly.
   * @param action The action to perform.
   * @param isPlayerOneTurn Whether it is currently Player One's turn.
   * @param playerOne Reference to Player One's state.
   * @param playerTwo Reference to Player Two's state.
   * @param shotgun Reference to the shotgun state.
   * @param shell The type of shell that is drawn (live or blank).
   * @return bool - the new value of 'isPlayerOneTurn' after this action
   * completes.
   */
  static bool performAction(Action action, bool isPlayerOneTurn,
                            SimulatedPlayer &playerOne,
                            SimulatedPlayer &playerTwo,
                            SimulatedShotgun &shotgun, ShellType shell);

  static SimulatedGame simulateLiveAction(const SimulatedGame &state,
                                          Action action);

  static SimulatedGame simulateBlankAction(const SimulatedGame &state,
                                           Action action);

  /**
   * @brief Create two new states corresponding to the outcome of drawing a live
   * shell and drawing a blank shell, respectively, for a chosen action.
   * @param state The current state to simulate from.
   * @param action The action to simulate.
   * @return A pair of states: { stateIfLive, stateIfBlank }.
   */
  static std::pair<SimulatedGame, SimulatedGame>
  simulateAction(const SimulatedGame &state, Action action);

  /**
   * @brief The core expectiminimax function, which explores possible sequences
   * of actions probabilistically and returns the evaluated outcome.
   * @param state The current recursive game state.
   * @param depth The depth to which we search.
   * @param maximizingPlayer True if the current turn is the maximizing player’s
   * turn.
   * @return A float indicating the expected value of this state (from the bot’s
   * perspective).
   */
  float expectiMiniMax(const SimulatedGame &state, int depth,
                       bool maximizingPlayer);

public:
  BotPlayer(const std::string &name, int health);
  BotPlayer(const std::string &name, int health, Player *opponent);

  /**
   * @brief Chooses an action to perform based on an expectiminimax search.
   * @param currentShotgun The real (current) shotgun state (used to create a
   * simulated copy).
   * @return The chosen Action (SHOOT_SELF or SHOOT_OPPONENT).
   */
  Action chooseAction(const Shotgun *currentShotgun) override;
};

#endif // BUCKSHOT_ROULETTE_BOT_BOTPLAYER_H
