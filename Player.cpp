#include "Player.h"

Player::Player(const std::string& name, int health)
        : name(name), health(health) {}

Player::~Player() {}

void Player::loseHealth(int amount) {
    health -= amount;
}

bool Player::isAlive() const {
    return health > 0;
}

std::string Player::getName() const {
    return name;
}
