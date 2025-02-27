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
    An optimal AI bot designed to win the game of Buckshot Roulette using Expectiminimax search.
    <br />
    <a href="https://github.com/CameronScarpati/buckshot-roulette-bot"><strong>Explore the docs »</strong></a>
    <br />
    <br />
    <a href="https://github.com/CameronScarpati/buckshot-roulette-bot">View Demo</a>
    ·
    <a href="https://github.com/CameronScarpati/buckshot-roulette-bot/issues/new?labels=bug&template=bug-report---.md">Report Bug</a>
    ·
    <a href="https://github.com/CameronScarpati/buckshot-roulette-bot/issues/new?labels=enhancement&template=feature-request---.md">Request Feature</a>
  </p>
</div>

## About The Project

This project implements an AI bot designed to
play [Buckshot Roulette](https://steamcommunity.com/sharedfiles/filedetails/?id=3218902482)
optimally. Buckshot Roulette is a strategic game that combines luck and decision-making, where
players take turns using a shotgun loaded with both live and blank shells.

The bot uses an Expectiminimax algorithm with alpha-beta pruning to evaluate possible game states
and make optimal decisions. It considers probabilities, health states, and available items to
determine the best action at each turn.

Key features:

- Strategic decision-making using lookahead search
- Item usage optimization (Cigarettes, Handcuffs, Magnifying Glass, etc.)
- Probabilistic reasoning for shell outcomes
- Dynamic evaluation of game states

### Built With

* [![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://cplusplus.com/)
* [![CMake](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)](https://cmake.org/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Getting Started

To set up the project locally, follow these steps.

### Prerequisites

Ensure you have the following installed:

* **C++ Compiler**: A compiler that supports C++17 or later.
* **CMake**: Version 3.15 or newer.
  ```sh
  # For Ubuntu
  sudo apt-get update
  sudo apt-get install -y cmake

  # For macOS
  brew install cmake
  ```

### Installation

1. **Clone the repository**:
   ```sh
   git clone https://github.com/CameronScarpati/buckshot-roulette-bot.git
   cd buckshot-roulette-bot
   ```

2. **Create a build directory and navigate into it**:
   ```sh
   mkdir build && cd build
   ```

3. **Configure the project using CMake**:
   ```sh
   cmake ..
   ```

4. **Build the project**:
   ```sh
   cmake --build .
   ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Usage

After building the project, you can run the executable to play against the AI:

```sh
./buckshot_roulette_bot
```

The game supports three modes:

- Human vs. Bot: Test your skills against the AI
- Human vs. Human: Play with a friend
- Bot vs. Bot: Watch the AI play against itself

### Game Rules

- Each round starts with a shotgun loaded with live and blank shells
- Players take turns either shooting themselves or their opponent
- Live shells deal damage; blank shells grant an extra turn
- Items can be used strategically:
    - Cigarette: Restores 1 HP
    - Magnifying Glass: Reveals the next shell
    - Handcuffs: Makes opponent skip their next turn
    - Beer: Ejects the current shell
    - Handsaw: Doubles the damage to the next live round

The first player to win three rounds is the victor.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Roadmap

- [x] Implement basic game mechanics
- [x] Develop initial bot strategy using Expectiminimax
- [ ] Support for "double or nothing" items
- [ ] Create interface to use the bot as an advisor for real games

See the [open issues](https://github.com/CameronScarpati/buckshot-roulette-bot/issues) for a full
list of proposed features and known issues.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Contributing

Contributions are welcome! If you have suggestions for improving the bot or adding new features,
please fork the repository and create a pull request.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

### Development Areas

- **AI Improvements**: Enhance the decision-making capabilities of the bot
- **UI Enhancements**: Improve the game's text-based interface
- **Performance Optimization**: Reduce memory usage and increase search depth
- **New Items**: Implement additional items from the original game
- **Testing**: Create comprehensive test cases for game mechanics

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## License

Distributed under the MIT License. See `LICENSE.txt` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Contact

Cameron Scarpati - [LinkedIn](https://linkedin.com/in/cameron-scarpati) - cameronscarp@gmail.com

Project
Link: [https://github.com/CameronScarpati/buckshot-roulette-bot](https://github.com/CameronScarpati/buckshot-roulette-bot)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Acknowledgments

* [Buckshot Roulette](https://store.steampowered.com/app/2537590/BUCKSHOT_ROULETTE/) by Mike
  Klubnika
* [Expectiminimax Algorithm](https://en.wikipedia.org/wiki/Expectiminimax_tree)
* [C++ Standard Library](https://en.cppreference.com/w/)
* [CMake Documentation](https://cmake.org/documentation/)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- MARKDOWN LINKS & IMAGES -->
<!-- https://www.markdownguide.org/basic-syntax/#reference-style-links -->

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