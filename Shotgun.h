#ifndef BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SHOTGUN_H

#include <deque>
#include <ostream>
#include <random>
#include <string_view>

/**
 * @enum class ShellType
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
std::ostream &operator<<(std::ostream &os, const ShellType &shell);

/**
 * @class Shotgun
 * @brief Manages shell loading, tracking, and probability calculations.
 */
class Shotgun {
protected:
  int totalShells = 0;                ///< Total number of shells.
  int liveShells = 0;                 ///< Live shells remaining.
  int blankShells = 0;                ///< Blank shells remaining.
  std::deque<ShellType> loadedShells; ///< Queue of loaded shells.
  bool sawUsed = false;               ///< Indicates if the handsaw was used.

public:
  /**
   * @brief Default constructor.
   */
  Shotgun() = default;

  /**
   * @brief Virtual destructor for proper polymorphic destruction.
   */
  virtual ~Shotgun() = default;

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
   * @throws std::runtime_error if the shotgun is empty.
   */
  [[nodiscard]] ShellType revealNextShell() const;

  /**
   * @brief Moves the current shell to the back.
   * @throws std::runtime_error if the shotgun is empty.
   */
  void rackShell();

  /**
   * @brief Uses the handsaw, modifying shotgun behavior.
   */
  void useHandsaw() noexcept;

  /**
   * @brief Resets the saw usage flag.
   */
  void resetSawUsed() noexcept;

  /**
   * @brief Checks if the handsaw was used.
   * @return True if used, false otherwise.
   */
  [[nodiscard]] bool getSawUsed() const noexcept;

  /**
   * @brief Checks if the shotgun is empty.
   * @return True if no shells remain.
   */
  [[nodiscard]] bool isEmpty() const noexcept;

  /**
   * @brief Gets the count of live shells.
   * @return Number of live shells.
   */
  [[nodiscard]] int getLiveShellCount() const noexcept;

  /**
   * @brief Gets the count of blank shells.
   * @return Number of blank shells.
   */
  [[nodiscard]] int getBlankShellCount() const noexcept;

  /**
   * @brief Gets the total shell count.
   * @return Total number of shells.
   */
  [[nodiscard]] int getTotalShellCount() const noexcept;

  /**
   * @brief Calculates the probability of drawing a live shell.
   * @return Probability as a float between 0 and 1.
   */
  [[nodiscard]] float getLiveShellProbability() const noexcept;

  /**
   * @brief Calculates the probability of drawing a blank shell.
   * @return Probability as a float between 0 and 1.
   */
  [[nodiscard]] float getBlankShellProbability() const noexcept;
};

#endif // BUCKSHOT_ROULETTE_BOT_SHOTGUN_H