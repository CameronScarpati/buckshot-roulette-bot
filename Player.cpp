#include "Player.h"
#include <stdexcept>
#include <utility>

int Player::maxHealth = 0;

Player::Player(std::string name, int health)
    : Player(std::move(name), health, nullptr) {}

Player::Player(std::string name, int health, Player *opp)
    : name(std::move(name)), health(health), opponent(opp), itemCount(0),
      handcuffsApplied(false) {
  itemCount = 0;
  if (maxHealth == 0)
    maxHealth = health;
  items.fill(nullptr);
}

Player::Player(const Player &other) {
  if (this == &other)
    return;

  itemCount = 0;

  this->name = other.name;
  this->health = other.health;
  this->handcuffsApplied = other.handcuffsApplied;
  this->nextShellRevealed = other.nextShellRevealed;
  this->knownNextShell = other.knownNextShell;

  this->opponent = other.opponent;

  for (int i = 0; i < other.itemCount; ++i) {
    if (other.items[i]) {
      this->items[i] = other.items[i];
      itemCount++;
    }
  }
}

Player &Player::operator=(const Player &other) {
  if (this == &other)
    return *this;

  itemCount = 0;

  this->name = other.name;
  this->health = other.health;
  this->handcuffsApplied = other.handcuffsApplied;
  this->nextShellRevealed = other.nextShellRevealed;
  this->knownNextShell = other.knownNextShell;

  this->opponent = other.opponent;

  for (int i = 0; i < other.itemCount; ++i) {
    if (other.items[i]) {
      this->items[i] = other.items[i];
      itemCount++;
    }
  }

  return *this;
}

void Player::setOpponent(Player *opp) { opponent = opp; }

void Player::loseHealth(bool sawUsed) {
  if (!isAlive())
    throw std::runtime_error("This person is already dead.");
  health -= (sawUsed) ? 2 : 1;
}

void Player::smokeCigarette() {
  if (health < maxHealth)
    ++health;
}

void Player::resetHealth(int newHealth) {
  maxHealth = newHealth;
  health = newHealth;
}

void Player::resetHealth() { health = maxHealth; }

bool Player::isAlive() const { return health > 0; }

std::string Player::getName() const { return name; }

int Player::getHealth() const { return health; }

void Player::applyHandcuffs() { handcuffsApplied = true; }

void Player::removeHandcuffs() { handcuffsApplied = false; }

bool Player::areHandcuffsApplied() const { return handcuffsApplied; }

void Player::setKnownNextShell(ShellType nextShell) {
  nextShellRevealed = true;
  knownNextShell = nextShell;
}

bool Player::isNextShellRevealed() const { return nextShellRevealed; };

ShellType Player::returnKnownNextShell() const { return knownNextShell; }

void Player::resetKnownNextShell() { nextShellRevealed = false; }

bool Player::addItem(Item *newItem) {
  if (itemCount >= MAX_ITEMS)
    return false;
  items[itemCount++] = newItem;
  return true;
}

void Player::useItem(int index) {
  if (index < 0 || index >= itemCount)
    return;
  if (items[index])
    items[index]->use(this, opponent);
  for (int i = index; i < itemCount - 1; i++) {
    items[i] = items[i + 1];
  }
  items[itemCount - 1] = nullptr;
  itemCount--;
}

void Player::useItem(int index, Shotgun *shotgun) {
  if (index < 0 || index >= itemCount)
    return;
  if (items[index])
    items[index]->use(this, opponent, shotgun);
  for (int i = index; i < itemCount - 1; i++) {
    items[i] = items[i + 1];
  }
  items[itemCount - 1] = nullptr;
  itemCount--;
}

int Player::getItemCount() const { return itemCount; }

bool Player::useItemByName(const std::string &itemName, Shotgun *shotgun) {
  for (int i = 0; i < itemCount; i++) {
    if (items[i] && items[i]->getName() == itemName) {
      if (shotgun)
        useItem(i, shotgun);
      else
        useItem(i);
      return true;
    }
  }
  return false;
}

void Player::removeItemByName(const std::string &itemName) {
  for (int i = 0; i < itemCount; ++i) {
    if (items[i] && items[i]->getName() == itemName) {
      for (int j = i; j < itemCount - 1; ++j)
        items[j] = items[j + 1];

      items[itemCount - 1] = nullptr;
      itemCount--;
      return;
    }
  }
}

bool Player::hasItem(const std::string &itemName) const {
  for (int i = 0; i < itemCount; i++)
    if (items[i] && items[i]->getName() == itemName)
      return true;
  return false;
}

int Player::countItem(const std::string &itemName) const {
  int sum = 0;
  for (int i = 0; i < itemCount; i++)
    if (items[i] && items[i]->getName() == itemName)
      ++sum;
  return sum;
}

void Player::printItems() const {
  std::cout << getName() << "'s items: ";
  if (itemCount == 0) {
    std::cout << "None";
  } else {
    for (int i = 0; i < itemCount; i++) {
      if (items[i])
        std::cout << items[i]->getName() << (i < itemCount - 1 ? ", " : "");
    }
  }
  std::cout << "\n";
}

void Player::resetHandcuffUsage() { handcuffsUsedThisTurn = false; }

void Player::useHandcuffsThisTurn() { handcuffsUsedThisTurn = true; }

bool Player::hasUsedHandcuffsThisTurn() const { return handcuffsUsedThisTurn; }
