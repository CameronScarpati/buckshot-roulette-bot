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
    An optimal bot designed to win the game of Buckshot Roulette.
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

This project aims to develop an optimal bot to
play [Buckshot Roulette](https://steamcommunity.com/sharedfiles/filedetails/?id=3218902482), a
strategic game that challenges players' decision-making skills.
The bot is implemented in C++ and
utilizes an Expectiminimax algorithm.
Future enhancements include the integration of "double or nothing" items and creating a way for you
to use this bot in game.

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

After building the project, you can run the bot executable to see it in action:

```sh
./buckshot_roulette_bot
```

You can play against the bot, play against another human player, or have the bot compete against
itself.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Roadmap

- [x] Implement basic game mechanics.
- [x] Develop initial bot strategy.
- [x] Integrate handcuff functionality (usable once per player turn--which is currently bugged).
- [ ] Check for memory leaks and optimize code.
- [ ] Add support for "double or nothing" items.
- [ ] Allow for someone to use the bot to make decisions in game (chooseNextMove function and setup
  current game state).

See the [open issues](https://github.com/CameronScarpati/buckshot-roulette-bot/issues) for a full
list
of proposed features (and known issues).

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Contributing

Contributions are welcome! If you have suggestions for improving the bot or adding new features,
please fork the repository and create a pull request. You can also open an issue with the "
enhancement" label.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

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

* [Buckshot Roulette Rules](https://steamcommunity.com/sharedfiles/filedetails/?id=3218902482)
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

[issues-url]: https://github.com/CameronScarpati

[license-shield]: https://img.shields.io/github/license/CameronScarpati/buckshot-roulette-bot?style=for-the-badge

[license-url]: https://github.com/CameronScarpati/buckshot-roulette-bot/blob/main/LICENSE

[linkedin-shield]: https://img.shields.io/badge/-LinkedIn-black.svg?style=for-the-badge&logo=linkedin&colorB=555

[linkedin-url]: https://linkedin.com/in/cameron-scarpati
