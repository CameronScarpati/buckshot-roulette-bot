#include "Player.h"
#include <stdexcept>
#include <utility>

int Player::maxHealth = 0;

Player::Player(std::string name, int health)
    : Player(std::move(name), health, nullptr) {}

Player::Player(std::string name, int health, Player *opp)
    : name(std::move(name)), health(health), opponent(opp),
      handcuffsApplied(false), handcuffsUsedThisTurn(false) {
  if (maxHealth == 0)
    maxHealth = health;
}

Player::Player(const Player &other) {
  if (this == &other)
    return;

  this->name = other.name;
  this->health = other.health;
  this->handcuffsApplied = other.handcuffsApplied;
  this->nextShellRevealed = other.nextShellRevealed;
  this->knownNextShell = other.knownNextShell;
  this->handcuffsUsedThisTurn = other.handcuffsUsedThisTurn;
  this->opponent = other.opponent;
  this->items = other.items; // std::vector copy
}

Player &Player::operator=(const Player &other) {
  if (this == &other)
    return *this;

  this->name = other.name;
  this->health = other.health;
  this->handcuffsApplied = other.handcuffsApplied;
  this->nextShellRevealed = other.nextShellRevealed;
  this->knownNextShell = other.knownNextShell;
  this->handcuffsUsedThisTurn = other.handcuffsUsedThisTurn;
  this->opponent = other.opponent;
  this->items = other.items;
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

bool Player::isNextShellRevealed() const { return nextShellRevealed; }

ShellType Player::returnKnownNextShell() const { return knownNextShell; }

void Player::resetKnownNextShell() { nextShellRevealed = false; }

bool Player::addItem(Item *newItem) {
  if (items.size() >= MAX_ITEMS)
    return false;
  items.push_back(newItem);
  return true;
}

void Player::useItem(int index) {
  if (index < 0 || index >= static_cast<int>(items.size()))
    return;
  if (items[index])
    items[index]->use(this, opponent);
  items.erase(items.begin() + index);
}

void Player::useItem(int index, Shotgun *shotgun) {
  if (index < 0 || index >= static_cast<int>(items.size()))
    return;
  if (items[index])
    items[index]->use(this, opponent, shotgun);
  items.erase(items.begin() + index);
}

int Player::getItemCount() const { return static_cast<int>(items.size()); }

bool Player::useItemByName(const std::string &itemName, Shotgun *shotgun) {
  auto it = std::find_if(items.begin(), items.end(), [&](const Item *item) {
    return item && item->getName() == itemName;
  });
  if (it != items.end()) {
    size_t index = std::distance(items.begin(), it);
    if (shotgun)
      useItem(static_cast<int>(index), shotgun);
    else
      useItem(static_cast<int>(index));
    return true;
  }
  return false;
}

void Player::removeItemByName(const std::string &itemName) {
  auto it = std::find_if(items.begin(), items.end(), [&](const Item *item) {
    return item && item->getName() == itemName;
  });
  if (it != items.end())
    items.erase(it);
}

bool Player::hasItem(const std::string &itemName) const {
  return std::any_of(items.begin(), items.end(), [&](const Item *item) {
    return item && item->getName() == itemName;
  });
}

int Player::countItem(const std::string &itemName) const {
  return static_cast<int>(
      std::count_if(items.begin(), items.end(), [&](const Item *item) {
        return item && item->getName() == itemName;
      }));
}

std::vector<Item *> Player::getItems() const { return items; }

void Player::printItems() const {
  std::cout << getName() << "'s items: ";
  if (items.empty()) {
    std::cout << "None";
  } else {
    for (size_t i = 0; i < items.size(); i++) {
      if (items[i])
        std::cout << items[i]->getName() << (i < items.size() - 1 ? ", " : "");
    }
  }
  std::cout << "\n";
}

void Player::resetHandcuffUsage() { handcuffsUsedThisTurn = false; }

void Player::useHandcuffsThisTurn() { handcuffsUsedThisTurn = true; }

bool Player::hasUsedHandcuffsThisTurn() const { return handcuffsUsedThisTurn; }

std::ostream &operator<<(std::ostream &os, const Player &player) {
  os << std::left << std::setw(15) << player.getName()
     << " | Health: " << std::setw(3) << player.getHealth();
  return os;
}