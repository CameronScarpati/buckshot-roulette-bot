#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H

#include "Shotgun.h"
#include <stdexcept>

/**
 * @class SimulatedShotgun
 * @brief A non-interactive shotgun for game simulations.
 *
 * Overrides `getNextShell()` to prevent altering real game state.
 */
class SimulatedShotgun : public Shotgun {
private:
  bool nextShellRevealed = false;
  ShellType revealedShell = ShellType::BLANK_SHELL;

public:
  /**
   * @brief Constructs a simulated shotgun.
   * @param total Total shells.
   * @param live Live shells.
   * @param blank Blank shells.
   */
  SimulatedShotgun(int total, int live, int blank);

  /**
   * @brief Prevents real shell retrieval in simulations.
   * @throws std::logic_error Always.
   */
  [[nodiscard]] ShellType getNextShell() override;

  /**
   * @brief Simulates drawing a live shell.
   * @return ShellType::LIVE_SHELL.
   * @throws std::logic_error If no live shells remain.
   */
  ShellType simulateLiveShell();

  /**
   * @brief Simulates drawing a blank shell.
   * @return ShellType::BLANK_SHELL.
   * @throws std::logic_error If no blank shells remain.
   */
  ShellType simulateBlankShell();

  /**
   * @brief Checks if the next shell has been revealed.
   * @return True if revealed, false otherwise.
   */
  bool isNextShellRevealed() const;

  /**
   * @brief Gets the revealed next shell.
   * @return The revealed shell.
   * @throws std::logic_error If the shell hasn't been revealed.
   */
  ShellType getRevealedNextShell() const;

  /**
   * @brief Sets the revealed next shell.
   * @param shell The shell type to reveal.
   */
  void setRevealedNextShell(ShellType shell);

  /**
   * @brief Resets the revealed shell status.
   */
  void resetNextShellRevealed();
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
