#include "Shotgun.h"
#include <random>

Shotgun::Shotgun() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(2, 8);

    totalShells = dist(gen);

    if (totalShells % 2 == 0) {
        liveShells = totalShells / 2;
        blankShells = totalShells / 2;
    } else {
        liveShells = (totalShells / 2) + 1;
        blankShells = totalShells / 2;
    }

    loadedShells = std::deque<ShellType>(liveShells, ShellType::Live);
    loadedShells.insert(loadedShells.end(), blankShells, ShellType::Blank);
    std::shuffle(loadedShells.begin(), loadedShells.end(), gen);
}

[[nodiscard]] ShellType Shotgun::getNextShell() {
    if (isEmpty()) throw std::runtime_error("The Shotgun is empty.");

    ShellType nextShell = loadedShells.front();
    loadedShells.pop_front();

    if (nextShell == ShellType::Live) {
        --liveShells;
    } else --blankShells;
    --totalShells;

    return nextShell;
}

bool Shotgun::isEmpty() const {
    return totalShells == 0;
}