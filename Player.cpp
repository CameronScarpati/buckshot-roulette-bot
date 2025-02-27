#include "Player.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

int Player::maxHealth = 0;

Player::Player(std::string name, int health)
    : name(std::move(name)), health(health), opponent(nullptr) {
  if (maxHealth == 0) {
    maxHealth = health;
  }
}

Player::Player(std::string name, int health, Player *opp)
    : name(std::move(name)), health(health), opponent(opp) {
  if (maxHealth == 0) {
    maxHealth = health;
  }
}

Player::Player(const Player &other)
    : name(other.name), health(other.health), opponent(other.opponent),
      handcuffsApplied(other.handcuffsApplied),
      nextShellRevealed(other.nextShellRevealed),
      knownNextShell(other.knownNextShell),
      handcuffsUsedThisTurn(other.handcuffsUsedThisTurn) {

  // Deep copy items
  items.reserve(other.items.size());
  for (const auto &item : other.items)
    if (item)
      items.push_back(Item::createByName(item->getName()));
}

Player &Player::operator=(const Player &other) {
  if (this == &other) {
    return *this;
  }

  name = other.name;
  health = other.health;
  opponent = other.opponent;
  handcuffsApplied = other.handcuffsApplied;
  nextShellRevealed = other.nextShellRevealed;
  knownNextShell = other.knownNextShell;
  handcuffsUsedThisTurn = other.handcuffsUsedThisTurn;

  // Clear and deep copy items
  items.clear();
  items.reserve(other.items.size());
  for (const auto &item : other.items)
    if (item)
      items.push_back(Item::createByName(item->getName()));

  return *this;
}

Player::Player(Player &&other) noexcept
    : name(std::move(other.name)), health(other.health),
      opponent(other.opponent), items(std::move(other.items)),
      handcuffsApplied(other.handcuffsApplied),
      nextShellRevealed(other.nextShellRevealed),
      knownNextShell(other.knownNextShell),
      handcuffsUsedThisTurn(other.handcuffsUsedThisTurn) {

  // Reset other's state
  other.health = 0;
  other.opponent = nullptr;
  other.handcuffsApplied = false;
  other.nextShellRevealed = false;
  other.handcuffsUsedThisTurn = false;
}

Player &Player::operator=(Player &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  name = std::move(other.name);
  health = other.health;
  opponent = other.opponent;
  items = std::move(other.items);
  handcuffsApplied = other.handcuffsApplied;
  nextShellRevealed = other.nextShellRevealed;
  knownNextShell = other.knownNextShell;
  handcuffsUsedThisTurn = other.handcuffsUsedThisTurn;

  // Reset other's state
  other.health = 0;
  other.opponent = nullptr;
  other.handcuffsApplied = false;
  other.nextShellRevealed = false;
  other.handcuffsUsedThisTurn = false;

  return *this;
}

void Player::setOpponent(Player *opp) noexcept { opponent = opp; }

void Player::loseHealth(bool sawUsed) {
  if (!isAlive()) {
    throw std::runtime_error("This player is already dead.");
  }
  health -= (sawUsed) ? 2 : 1;
}

void Player::smokeCigarette() noexcept {
  if (health < maxHealth) {
    ++health;
  }
}

void Player::resetMaxHealth(int newHealth) noexcept { maxHealth = newHealth; }

void Player::resetHealth() noexcept { health = maxHealth; }

bool Player::isAlive() const noexcept { return health > 0; }

std::string_view Player::getName() const noexcept { return name; }

int Player::getHealth() const noexcept { return health; }

int Player::getMaxHealth() noexcept { return maxHealth; }

void Player::applyHandcuffs() noexcept { handcuffsApplied = true; }

void Player::removeHandcuffs() noexcept { handcuffsApplied = false; }

bool Player::areHandcuffsApplied() const noexcept { return handcuffsApplied; }

void Player::setKnownNextShell(ShellType nextShell) noexcept {
  nextShellRevealed = true;
  knownNextShell = nextShell;
}

bool Player::isNextShellRevealed() const noexcept { return nextShellRevealed; }

ShellType Player::returnKnownNextShell() const noexcept {
  return knownNextShell;
}

void Player::resetKnownNextShell() noexcept { nextShellRevealed = false; }

bool Player::addItem(std::unique_ptr<Item> newItem) {
  if (items.size() >= MAX_ITEMS || !newItem) {
    return false;
  }
  items.push_back(std::move(newItem));
  return true;
}

bool Player::useItem(int index) {
  if (index < 0 || index >= static_cast<int>(items.size())) {
    return false;
  }

  if (items[index]) {
    items[index]->use(this, opponent);
    items.erase(items.begin() + index);
    return true;
  }
  return false;
}

bool Player::useItem(int index, Shotgun *shotgun) {
  if (index < 0 || index >= static_cast<int>(items.size())) {
    return false;
  }

  if (items[index]) {
    items[index]->use(this, opponent, shotgun);
    items.erase(items.begin() + index);
    return true;
  }
  return false;
}

int Player::getItemCount() const noexcept {
  return static_cast<int>(items.size());
}

bool Player::useItemByName(std::string_view itemName, Shotgun *shotgun) {
  auto it = std::find_if(items.begin(), items.end(),
                         [&itemName](const std::unique_ptr<Item> &item) {
                           return item && item->getName() == itemName;
                         });

  if (it != items.end()) {
    size_t index = std::distance(items.begin(), it);
    return shotgun ? useItem(static_cast<int>(index), shotgun)
                   : useItem(static_cast<int>(index));
  }
  return false;
}

bool Player::removeItemByName(std::string_view itemName) {
  auto it = std::find_if(items.begin(), items.end(),
                         [&itemName](const std::unique_ptr<Item> &item) {
                           return item && item->getName() == itemName;
                         });

  if (it != items.end()) {
    items.erase(it);
    return true;
  }
  return false;
}

bool Player::hasItem(std::string_view itemName) const {
  return std::any_of(items.begin(), items.end(),
                     [&itemName](const std::unique_ptr<Item> &item) {
                       return item && item->getName() == itemName;
                     });
}

int Player::countItem(std::string_view itemName) const {
  return static_cast<int>(
      std::count_if(items.begin(), items.end(),
                    [&itemName](const std::unique_ptr<Item> &item) {
                      return item && item->getName() == itemName;
                    }));
}

std::vector<Item *> Player::getItemsView() const {
  std::vector<Item *> itemPointers;
  itemPointers.reserve(items.size());

  for (const auto &item : items)
    if (item)
      itemPointers.push_back(item.get());

  return itemPointers;
}

void Player::printItems() const {
  std::cout << getName() << "'s items: ";

  if (items.empty()) {
    std::cout << "None";
  } else {
    for (size_t i = 0; i < items.size(); i++) {
      if (items[i]) {
        std::cout << items[i]->getName() << (i < items.size() - 1 ? ", " : "");
      }
    }
  }
  std::cout << "\n";
}

void Player::resetHandcuffUsage() noexcept { handcuffsUsedThisTurn = false; }

void Player::useHandcuffsThisTurn() noexcept { handcuffsUsedThisTurn = true; }

bool Player::hasUsedHandcuffsThisTurn() const noexcept {
  return handcuffsUsedThisTurn;
}

std::ostream &operator<<(std::ostream &os, const Player &player) {
  os << std::left << std::setw(15) << player.getName()
     << " | Health: " << std::setw(3) << player.getHealth();
  return os;
}