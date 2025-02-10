#ifndef BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H

#include "Shotgun.h"
#include <stdexcept>

/**
 * @brief Represents a simulated shotgun for use in expectiminimax search.
 *
 * This class extends the Shotgun class but overrides getNextShell() to throw
 * an error, since the simulation should use simulateLiveShell() or
 * simulateBlankShell() to branch the game state without altering a real queue.
 */
class SimulatedShotgun : public Shotgun {
private:
  bool nextShellRevealed = false;
  ShellType revealedShell = ShellType::BLANK_SHELL;

public:
  /**
   * @brief Constructs a simulated shotgun from the given state.
   *
   * @param total The total number of shells.
   * @param live The number of live shells.
   * @param blank The number of blank shells.
   */
  SimulatedShotgun(int total, int live, int blank);

  /**
   * @brief Overrides getNextShell() to prevent its use in simulation.
   *
   * @throws std::logic_error always.
   */
  [[nodiscard("SimulatedShotgun::getNextShell() should not be called in "
              "simulation.")]] ShellType
  getNextShell() override;

  /**
   * @brief Simulates drawing a live shell.
   *
   * Decrements the live shell count and the total shell count.
   *
   * @return ShellType::LIVE_SHELL.
   * @throws std::logic_error if no live shells remain.
   */
  ShellType simulateLiveShell();

  /**
   * @brief Simulates drawing a blank shell.
   *
   * Decrements the blank shell count and the total shell count.
   *
   * @return ShellType::BLANK_SHELL.
   * @throws std::logic_error if no blank shells remain.
   */
  ShellType simulateBlankShell();

  bool isNextShellRevealed() const;

  ShellType getRevealedNextShell() const;

  void setRevealedNextShell(ShellType shell);
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H
