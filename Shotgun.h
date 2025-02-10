#ifndef BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SHOTGUN_H

#include <deque>
#include <system_error>

/**
 * @brief Defines the possible types of shotgun shells.
 */
enum class ShellType : int { BLANK_SHELL = 0, LIVE_SHELL = 1 };

/**
 * @brief Represents the behavior of a shotgun in the game.
 *
 * This class manages shell loading, tracking live and blank shell counts,
 * and determining shell probabilities.
 */
class Shotgun {
protected:
  int totalShells{}; ///< Total number of shells in the shotgun.
  int liveShells{};  ///< Number of live shells remaining.
  int blankShells{}; ///< Number of blank shells remaining.
  std::deque<ShellType> loadedShells; ///< Queue representing the loaded shells.

public:
  /**
   * @brief Constructs a Shotgun object and loads the initial shells.
   */
  Shotgun();

  /**
   * @brief Loads shells into the shotgun.
   */
  void loadShells();

  /**
   * @brief Retrieves the next shell from the chamber.
   *
   * @return The next shell (LIVE_SHELL or BLANK_SHELL).
   */
  [[nodiscard("The shell needs to be used.")]] virtual ShellType getNextShell();

  /**
   * @brief Checks if the shotgun is empty.
   *
   * @return True if no shells remain, false otherwise.
   */
  bool isEmpty() const;

  /**
   * @brief Gets the count of live shells remaining.
   *
   * @return The number of live shells.
   */
  int getLiveShellCount() const;

  /**
   * @brief Gets the count of blank shells remaining.
   *
   * @return The number of blank shells.
   */
  int getBlankShellCount() const;

  /**
   * @brief Gets the total number of shells in the shotgun.
   *
   * @return The total shell count.
   */
  int getTotalShellCount() const;

  /**
   * @brief Calculates the probability of drawing a live shell.
   *
   * @return The probability as a float.
   */
  float getLiveShellProbability() const;

  /**
   * @brief Calculates the probability of drawing a blank shell.
   *
   * @return The probability as a float.
   */
  float getBlankShellProbability() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
