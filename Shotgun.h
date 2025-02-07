#ifndef BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SHOTGUN_H


#include <deque>
#include <system_error>

/**
 * @brief This enum defines shell types.
 */
enum class ShellType : int {
    Blank = 0,
    Live = 1
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
     * @return ShellType::Live/Blank (Random).
     */
    [[nodiscard("The shell needs to be used.")]] ShellType getNextShell();

    /**
     * @brief This function returns if the Shotgun is empty or not.
     * @return Boolean (Empty/Has shells).
     */
    bool isEmpty() const;

private:
    int totalShells;
    int liveShells;
    int blankShells;
    std::deque<ShellType> loadedShells;
};


#endif //BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
