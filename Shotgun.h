#ifndef BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
#define BUCKSHOT_ROULETTE_BOT_SHOTGUN_H


class Shotgun {
public:
    Shotgun();
    void load();         // Load shells randomly based on a fixed configuration.
    int getNextShell();  // Return 1 for a live shell, 0 for a blank.
    bool isEmpty() const;

private:
    int totalShells;  // e.g., 8
    int liveShells;   // e.g., 4 (adjust as needed)
    int blankShells;  // e.g., 4 (adjust as needed)
    // Optionally, add a container (like std::vector<int>) to represent the randomized shell order.
};


#endif //BUCKSHOT_ROULETTE_BOT_SHOTGUN_H
