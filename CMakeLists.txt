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
        SimulationState.h)
