#ifndef BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SHOTGUN_H


#include <deque>
#include <system_error>

/**
 * @brief This enum defines shell types.
 */
enum class ShellType : int {
    BLANK_SHELL = 0,
    LIVE_SHELL = 1
};

/**
 * @brief This class defines the Shotgun's behavior.
 */
class Shotgun {
public:
    /**
     * @brief This constructor initializes the Shotgun object and loads the shells.
     */
    Shotgun();

    /**
     * @brief This function returns the next shell in the chamber.
     * @return ShellType::LIVE_SHELL/BLANK_SHELL (Random).
     */
    [[nodiscard("The shell needs to be used.")]] ShellType getNextShell();

    /**
     * @brief This function returns if the Shotgun is empty or not.
     * @return Boolean (Empty/Has shells).
     */
    bool isEmpty() const;

    /**
     * @brief Getter.
     * @return Integer (Live shell count).
     */
    int liveShellCount() const;

    /**
     * @brief Getter.
     * @return Integer (Total shell count).
     */
    int totalShellCount() const;

private:
    int totalShells;
    int liveShells;
    int blankShells;
    std::deque<ShellType> loadedShells;
};


#endif //BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
