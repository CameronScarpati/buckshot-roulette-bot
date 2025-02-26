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
class SimulatedShotgun final : public Shotgun {
public:
  /**
   * @brief Constructs a simulated shotgun.
   * @param total Total shells.
   * @param live Live shells.
   * @param blank Blank shells.
   * @param sawUsed Is saw applied?
   * @throws std::invalid_argument If total != live + blank
   */
  SimulatedShotgun(int total, int live, int blank, bool sawUsed);

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
};

#endif // BUCKSHOT_ROULETTE_BOT_SIMULATEDSHOTGUN_H