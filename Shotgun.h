#ifndef BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SHOTGUN_H

#include <deque>
#include <ostream>
#include <system_error>

/**
 * @enum ShellType
 * @brief Defines the possible types of shotgun shells.
 */
enum class ShellType : int {
  BLANK_SHELL = 0, ///< A non-lethal shell.
  LIVE_SHELL = 1   ///< A lethal shell.
};

/**
 * @brief Overloads the stream output operator for ShellType.
 * @param os Output stream.
 * @param shell The shell type to print.
 * @return The modified output stream.
 */
inline std::ostream &operator<<(std::ostream &os, const ShellType &shell) {
  switch (shell) {
  case ShellType::BLANK_SHELL:
    os << "BLANK_SHELL";
    break;
  case ShellType::LIVE_SHELL:
    os << "LIVE_SHELL";
    break;
  default:
    os << "UNKNOWN_SHELL";
    break;
  }
  return os;
}

/**
 * @class Shotgun
 * @brief Manages shell loading, tracking, and probability calculations.
 */
class Shotgun {
protected:
  int totalShells{};                  ///< Total number of shells.
  int liveShells{};                   ///< Live shells remaining.
  int blankShells{};                  ///< Blank shells remaining.
  std::deque<ShellType> loadedShells; ///< Queue of loaded shells.
  bool sawUsed{};                     ///< Indicates if the handsaw was used.

public:
  /**
   * @brief Loads shells into the shotgun.
   */
  void loadShells();

  /**
   * @brief Retrieves and removes the next shell.
   * @return The next shell type.
   * @throws std::runtime_error if the shotgun is empty.
   */
  [[nodiscard("The shell needs to be used.")]] virtual ShellType getNextShell();

  /**
   * @brief Reveals the next shell without removing it.
   * @return The next shell type.
   */
  ShellType revealNextShell() const;

  /**
   * @brief Moves the current shell to the back.
   *
   * Decrements the corresponding shell count.
   */
  void rackShell();

  /**
   * @brief Uses the handsaw, modifying shotgun behavior.
   */
  void useHandsaw();

  /**
   * @brief Resets the saw usage flag.
   */
  void resetSawUsed();

  /**
   * @brief Checks if the handsaw was used.
   * @return True if used, false otherwise.
   */
  bool getSawUsed() const;

  /**
   * @brief Checks if the shotgun is empty.
   * @return True if no shells remain.
   */
  bool isEmpty() const;

  /**
   * @brief Gets the count of live shells.
   * @return Number of live shells.
   */
  int getLiveShellCount() const;

  /**
   * @brief Gets the count of blank shells.
   * @return Number of blank shells.
   */
  int getBlankShellCount() const;

  /**
   * @brief Gets the total shell count.
   * @return Total number of shells.
   */
  int getTotalShellCount() const;

  /**
   * @brief Calculates the probability of drawing a live shell.
   * @return Probability as a float.
   */
  float getLiveShellProbability() const;

  /**
   * @brief Calculates the probability of drawing a blank shell.
   * @return Probability as a float.
   */
  float getBlankShellProbability() const;
};

#endif // BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
