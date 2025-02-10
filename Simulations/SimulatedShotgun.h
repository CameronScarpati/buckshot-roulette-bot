#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H

#include "Shotgun.h"

/**
 * @class SimulatedShotgun
 * @brief Simulates a shotgun containing live and blank shells.
 *
 * This class manages the state of a simulated shotgun, including the counts of
 * total, live, and blank shells. It provides methods to retrieve shells, check
 * the remaining shell counts, and compute the probabilities of drawing a live
 * or blank shell.
 */
class SimulatedShotgun {
private:
  int totalShells; ///< The total number of shells in the shotgun.
  int liveShells;  ///< The current count of live shells.
  int blankShells; ///< The current count of blank shells.

public:
  /**
   * @brief Constructs a new SimulatedShotgun object.
   *
   * @param shells The total number of shells.
   * @param lives The initial number of live shells.
   * @param blanks The initial number of blank shells.
   */
  SimulatedShotgun(int shells, int lives, int blanks);

  /**
   * @brief Retrieves a live shell from the shotgun.
   *
   * This function simulates drawing a live shell, updating the live shell count
   * accordingly.
   */
  void getLiveShell();

  /**
   * @brief Retrieves a blank shell from the shotgun.
   *
   * This function simulates drawing a blank shell, updating the blank shell
   * count accordingly.
   */
  void getBlankShell();

  /**
   * @brief Gets the number of remaining live shells.
   * @return The current count of live shells.
   */
  int getLiveShellCount() const;

  /**
   * @brief Gets the number of remaining blank shells.
   * @return The current count of blank shells.
   */
  int getBlankShellCount() const;

  /**
   * @brief Gets the total number of shells.
   * @return The total shell count.
   */
  int getTotalShellCount() const;

  /**
   * @brief Calculates the probability of drawing a live shell.
   * @return The probability of a live shell as a float.
   */
  float getLiveShellProbability() const;

  /**
   * @brief Calculates the probability of drawing a blank shell.
   * @return The probability of a blank shell as a float.
   */
  float getBlankShellProbability() const;

  /**
   * @brief Checks if the shotgun is empty.
   * @return true if there are no shells left; false otherwise.
   */
  bool isEmpty() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
