cmake_minimum_required(VERSION 3.27)
project(buckshot_roulette_bot)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(buckshot_roulette_bot
        BotPlayer.cpp
        BotPlayer.h
        Game.cpp
        Game.h
        HumanPlayer.cpp
        HumanPlayer.h
        main.cpp
        Player.cpp
        Player.h
        Shotgun.cpp
        Shotgun.h
        Simulations/SimulatedPlayer.h
        Simulations/SimulatedPlayer.cpp
        Simulations/SimulatedGame.h
        Simulations/SimulatedGame.cpp
        Items/Cigarette.h
        Items/Cigarette.cpp
        Items/Handcuffs.h
        Items/Handcuffs.cpp
        Items/MagnifyingGlass.h
        Items/MagnifyingGlass.cpp
        Items/Beer.h
        Items/Beer.cpp
        Items/Handsaw.h
        Items/Handsaw.cpp
        Simulations/SimulatedShotgun.cpp
        Items/Item.cpp)
