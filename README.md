<a id="readme-top"></a>

[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![project_license][license-shield]][license-url]
[![LinkedIn][linkedin-shield]][linkedin-url]

<br />
<div align="center">
  <h3 align="center">Buckshot Roulette Bot</h3>

  <p align="center">
    An AI that plays <a href="https://store.steampowered.com/app/2537590/BUCKSHOT_ROULETTE/">Buckshot Roulette</a> optimally using Expectiminimax search with alpha-beta pruning.
    <br />
    <br />
    <a href="https://github.com/CameronScarpati/buckshot-roulette-bot/issues/new?labels=bug&template=bug-report---.md">Report Bug</a>
    &middot;
    <a href="https://github.com/CameronScarpati/buckshot-roulette-bot/issues/new?labels=enhancement&template=feature-request---.md">Request Feature</a>
  </p>
</div>

## About

Buckshot Roulette is a strategic tabletop game where two players take turns with a shotgun loaded with a hidden sequence of live and blank shells. Each turn, a player can shoot themselves or their opponent, and use items to gain an edge. This project builds an AI that plays the game optimally by searching the full decision tree, weighing probabilistic outcomes, and selecting the highest-value action at every turn.

The bot uses an **Expectiminimax algorithm with alpha-beta pruning** -- the standard approach for adversarial games with chance nodes. It evaluates thousands of future game states per move within a 1-second time budget, using iterative deepening (depth 5-10) to balance search quality with responsiveness.

### Built With

* [![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://cplusplus.com/)
* [![CMake](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Architecture

```
├── main.cpp                   # Entry point and game mode selection
├── Game.h/.cpp                # Round lifecycle, turn management, win conditions
├── Player.h/.cpp              # Abstract base: health, inventory, turn state
│   ├── HumanPlayer            # Terminal input for human players
│   ├── BotPlayer              # Expectiminimax AI decision engine
│   └── SimulatedPlayer        # Lightweight clone used during search
├── Shotgun.h/.cpp             # Shell queue, draw mechanics, saw state
│   └── SimulatedShotgun       # Probability-only copy (no real shell queue)
├── Items/
│   ├── Item.h/.cpp            # Abstract base + factory method (createByName)
│   ├── Cigarette              # Restore 1 HP
│   ├── Beer                   # Eject current shell
│   ├── Handcuffs              # Skip opponent's next turn
│   ├── Handsaw                # Double next live round's damage
│   └── MagnifyingGlass        # Reveal the next shell
└── Simulations/
    ├── SimulatedGame           # Deep-copyable game state for tree search
    ├── SimulatedPlayer         # Cloneable player with item reconstruction
    └── SimulatedShotgun        # Tracks live/blank counts without a real queue
```

**Key design patterns:**

| Pattern | Where | Purpose |
|---------|-------|---------|
| **Factory** | `Item::createByName()` | Instantiate items by name string |
| **Strategy** | `Player::chooseAction()` | Polymorphic decision-making (human input vs. AI search) |
| **Prototype** | `Item::clone()` | Deep-copy inventory during state simulation |
| **Template Method** | `Game::runGame()` | Shared round flow with subclass-specific behavior |

The `Simulations/` layer exists so the AI can explore future states without mutating the real game. Each node in the search tree gets its own deep-copied `SimulatedGame`, with `SimulatedPlayer` and `SimulatedShotgun` objects that track only the information needed for evaluation.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## How the AI Works

The bot's decision engine is a time-bounded **Expectiminimax** search with **alpha-beta pruning**:

1. **Action enumeration** -- On each turn, the bot identifies all feasible actions (shoot self, shoot opponent, use an item) based on current inventory and game state.

2. **Strategic prioritization** -- Actions are ordered so the most promising branches are searched first. Known-shell shots and information-gathering items (Magnifying Glass) are evaluated before lower-value moves, which improves alpha-beta cutoff rates.

3. **Expectiminimax tree search** -- The bot builds a game tree where:
   - **Max nodes** represent the bot's turns (maximize expected value).
   - **Min nodes** represent the opponent's turns (minimize expected value).
   - **Chance nodes** represent uncertain shell draws, with branches weighted by `P(live)` and `P(blank)`.

   Deterministic actions (Cigarette, Handsaw, Handcuffs) skip the chance layer entirely.

4. **Alpha-beta pruning** -- Standard pruning eliminates branches that cannot influence the final decision, reducing the effective branching factor significantly.

5. **State evaluation** -- Leaf nodes are scored by a weighted heuristic considering health differential (150), item advantage (Magnifying Glass: 180, Handcuffs: 90, Handsaw: 70), turn advantage (85), shell distribution favorability (60), and a penalty for holding the turn when the shell distribution is dangerous (80).

6. **Time management** -- Search runs with iterative deepening from depth 5 to 10, hard-capped at 1 second. The best result found within the time budget is used.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Sample Gameplay

```
=== Round 1 ===
Shotgun loaded with 5 shells: 3 live, 2 blank.

Cameron's items: [Magnifying Glass] [Cigarette] [Handcuffs]
Dealer's items:  [Beer] [Handsaw] [Magnifying Glass]

Cameron's turn (HP: 3)
> Available actions: shoot self, shoot opponent, use magnifying glass, use cigarette, use handcuffs
> Choice: use magnifying glass
  Cameron peeks at the next shell... it's LIVE.

Cameron's turn (HP: 3)
> Choice: use handcuffs
  Dealer is handcuffed! They will skip their next turn.

Cameron's turn (HP: 3)
> Choice: shoot opponent
  BANG! Dealer takes 1 damage. (HP: 3 -> 2)

Dealer's turn -- SKIPPED (handcuffed)

Cameron's turn (HP: 3)
> Choice: shoot self
  *click* -- blank shell. Cameron keeps their turn.

Cameron's turn (HP: 3)
> Choice: shoot opponent
  BANG! Dealer takes 1 damage. (HP: 2 -> 1)

Dealer's turn (HP: 1)
  Dealer uses Beer. Ejected shell: LIVE.
  Dealer uses Magnifying Glass. Next shell is BLANK.
  Dealer shoots self.
  *click* -- blank shell. Dealer keeps their turn.

  ...

=== Cameron wins the round! ===
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Getting Started

### Prerequisites

* **C++ compiler** with C++17 support (GCC 7+, Clang 5+, MSVC 2017+)
* **CMake** 3.15+

```sh
# Ubuntu / Debian
sudo apt-get update && sudo apt-get install -y cmake g++

# macOS
brew install cmake
```

### Build

```sh
git clone https://github.com/CameronScarpati/buckshot-roulette-bot.git
cd buckshot-roulette-bot
mkdir build && cd build
cmake ..
cmake --build .
```

### Run

```sh
./buckshot_roulette_bot
```

### Test

```sh
# From the build directory
cmake --build . --target tests
ctest --output-on-failure
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Game Modes

| Mode | Description |
|------|-------------|
| **Human vs. Bot** | Test your skills against the AI |
| **Human vs. Human** | Play with a friend over the terminal |
| **Bot vs. Bot** | Watch two AI agents play against each other |

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Roadmap

- [x] Core game mechanics (shells, items, rounds, win conditions)
- [x] Expectiminimax bot with alpha-beta pruning
- [x] Time-bounded iterative deepening search
- [ ] Advisor mode for use alongside the real game

See the [open issues](https://github.com/CameronScarpati/buckshot-roulette-bot/issues) for a full list of proposed features and known issues.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Contributing

Contributions are welcome. Fork the repo, create a feature branch, and open a pull request.

```sh
git checkout -b feature/your-feature
git commit -m 'Add your feature'
git push origin feature/your-feature
```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## License

Distributed under the MIT License. See `LICENSE.txt` for more information.

## Contact

Cameron Scarpati - [LinkedIn](https://linkedin.com/in/cameron-scarpati) - cameronscarp@gmail.com

Project Link: [github.com/CameronScarpati/buckshot-roulette-bot](https://github.com/CameronScarpati/buckshot-roulette-bot)

## Acknowledgments

* [Buckshot Roulette](https://store.steampowered.com/app/2537590/BUCKSHOT_ROULETTE/) by Mike Klubnika
* [Expectiminimax Algorithm](https://en.wikipedia.org/wiki/Expectiminimax_tree)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- MARKDOWN LINKS & IMAGES -->

[contributors-shield]: https://img.shields.io/github/contributors/CameronScarpati/buckshot-roulette-bot.svg?style=for-the-badge

[contributors-url]: https://github.com/CameronScarpati/buckshot-roulette-bot/graphs/contributors

[forks-shield]: https://img.shields.io/github/forks/CameronScarpati/buckshot-roulette-bot.svg?style=for-the-badge

[forks-url]: https://github.com/CameronScarpati/buckshot-roulette-bot/network/members

[stars-shield]: https://img.shields.io/github/stars/CameronScarpati/buckshot-roulette-bot.svg?style=for-the-badge

[stars-url]: https://github.com/CameronScarpati/buckshot-roulette-bot/stargazers

[issues-shield]: https://img.shields.io/github/issues/CameronScarpati/buckshot-roulette-bot.svg?style=for-the-badge

[issues-url]: https://github.com/CameronScarpati/buckshot-roulette-bot/issues

[license-shield]: https://img.shields.io/github/license/CameronScarpati/buckshot-roulette-bot?style=for-the-badge

[license-url]: https://github.com/CameronScarpati/buckshot-roulette-bot/blob/main/LICENSE

[linkedin-shield]: https://img.shields.io/badge/-LinkedIn-black.svg?style=for-the-badge&logo=linkedin&colorB=555

[linkedin-url]: https://linkedin.com/in/cameron-scarpati
