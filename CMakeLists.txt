cmake_minimum_required(VERSION 3.27)
project(buckshot_roulette_bot)

set(CMAKE_CXX_STANDARD 17)

include_directories(.)

# Collect all library sources (everything except main.cpp)
set(LIB_SOURCES
        BotPlayer.cpp
        BotPlayer.h
        Game.cpp
        Game.h
        HumanPlayer.cpp
        HumanPlayer.h
        Player.cpp
        Player.h
        Shotgun.cpp
        Shotgun.h
        Simulations/SimulatedPlayer.h
        Simulations/SimulatedPlayer.cpp
        Simulations/SimulatedGame.h
        Simulations/SimulatedGame.cpp
        Simulations/SimulatedShotgun.cpp
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
        Items/Item.cpp)

add_executable(buckshot_roulette_bot
        main.cpp
        ${LIB_SOURCES})

# ---- Google Test ----
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(buckshot_roulette_tests
        tests/tests.cpp
        ${LIB_SOURCES})

target_link_libraries(buckshot_roulette_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(buckshot_roulette_tests)
