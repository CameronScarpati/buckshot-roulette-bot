#include "Player.h"

#include <utility>

int Player::maxHealth;

Player::Player(std::string name, int health)
        : Player(std::move(name), health, nullptr) {}

Player::Player(std::string name, int health, Player* opp)
        : name(std::move(name)), health(health), opponent(opp)
{
    if (maxHealth == 0)
        maxHealth = health;
}

void Player::loseHealth(bool sawUsed) {
    if (!isAlive()) throw std::runtime_error("This person is already dead.");
    health -= (sawUsed) ? 2 : 1;
}

void Player::useStimulant() {
    ++health;
}

void Player::resetHealth(int newHealth) {
    maxHealth = newHealth;
    health = newHealth;
}

void Player::resetHealth() {
    health = maxHealth;
}

bool Player::isAlive() const {
    return health > 0;
}

std::string Player::getName() const {
    return name;
}

int Player::getHealth() const {
    return health;
}
