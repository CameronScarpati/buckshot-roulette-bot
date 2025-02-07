#include "Shotgun.h"
#include <cstdlib>   // For random functions (e.g., rand())
#include <ctime>     // For seeding randomness (if needed)

Shotgun::Shotgun() : totalShells(8), liveShells(4), blankShells(4) {
    // Optionally seed your random number generator here, e.g., std::srand(std::time(nullptr));
}

void Shotgun::load() {
    // TODO: Randomly shuffle or set liveShells and blankShells.
}

int Shotgun::getNextShell() {
    // TODO: Return the next shell's type (1 for live, 0 for blank) and update the counts.
    return 0;
}

bool Shotgun::isEmpty() const {
    return (liveShells + blankShells) <= 0;
}