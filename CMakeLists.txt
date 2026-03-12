cmake_minimum_required(VERSION 3.16)
project(buckshot_roulette_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warning flags
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)

# Release optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

include_directories(.)

set(SOURCES
    BotPlayer.cpp
    Game.cpp
    HumanPlayer.cpp
    Player.cpp
    Shotgun.cpp
    Simulations/SimulatedPlayer.cpp
    Simulations/SimulatedGame.cpp
    Simulations/SimulatedShotgun.cpp
    Items/Cigarette.cpp
    Items/Handcuffs.cpp
    Items/MagnifyingGlass.cpp
    Items/Beer.cpp
    Items/Handsaw.cpp
    Items/Item.cpp
)

set(HEADERS
    BotPlayer.h
    Game.h
    HumanPlayer.h
    Player.h
    Shotgun.h
    Simulations/SimulatedPlayer.h
    Simulations/SimulatedGame.h
    Simulations/SimulatedShotgun.h
    Items/Cigarette.h
    Items/Handcuffs.h
    Items/MagnifyingGlass.h
    Items/Beer.h
    Items/Handsaw.h
    Items/Item.h
)

add_executable(${PROJECT_NAME} main.cpp ${SOURCES} ${HEADERS})

# Bot vs Bot simulation
add_executable(simulate simulate.cpp ${SOURCES} ${HEADERS})

# Testing
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)

    file(GLOB TEST_SOURCES tests/*.cpp)

    if(TEST_SOURCES)
        add_executable(tests ${TEST_SOURCES} ${SOURCES})
        target_link_libraries(tests GTest::gtest_main)
        target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR})
        gtest_discover_tests(tests)
    endif()
endif()
