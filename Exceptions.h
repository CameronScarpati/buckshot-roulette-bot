#ifndef BUCKSHOT_ROULETTE_BOT_EXCEPTIONS_H
#define BUCKSHOT_ROULETTE_BOT_EXCEPTIONS_H

#include <stdexcept>
#include <string>

/**
 * @brief Base exception for all game-related errors.
 */
class GameException : public std::runtime_error {
public:
  explicit GameException(const std::string &message)
      : std::runtime_error(message) {}
};

/**
 * @brief Thrown when an invalid item operation is attempted.
 */
class InvalidItemException : public GameException {
public:
  explicit InvalidItemException(const std::string &message)
      : GameException(message) {}
};

/**
 * @brief Thrown when an invalid action is attempted.
 */
class InvalidActionException : public GameException {
public:
  explicit InvalidActionException(const std::string &message)
      : GameException(message) {}
};

/**
 * @brief Thrown when an operation is attempted on an empty shotgun.
 */
class EmptyShotgunException : public GameException {
public:
  explicit EmptyShotgunException(const std::string &message)
      : GameException(message) {}
};

/**
 * @brief Thrown when an operation is attempted on a dead player.
 */
class PlayerDeadException : public GameException {
public:
  explicit PlayerDeadException(const std::string &message)
      : GameException(message) {}
};

/**
 * @brief Thrown when a simulation-only method is called inappropriately.
 */
class SimulationException : public GameException {
public:
  explicit SimulationException(const std::string &message)
      : GameException(message) {}
};

/**
 * @brief Thrown when invalid arguments are provided to game constructors or
 * methods.
 */
class InvalidGameArgumentException : public GameException {
public:
  explicit InvalidGameArgumentException(const std::string &message)
      : GameException(message) {}
};

#endif // BUCKSHOT_ROULETTE_BOT_EXCEPTIONS_H
