cmake_minimum_required(VERSION 3.16)
project(buckshot_roulette_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warning flags, carried by an interface target so that they apply to this
# project's code and not to anything fetched into the build.
option(WARNINGS_AS_ERRORS "Turn warnings into errors" OFF)

add_library(bsr_warnings INTERFACE)
target_compile_options(bsr_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)
if(WARNINGS_AS_ERRORS)
    target_compile_options(bsr_warnings INTERFACE -Werror)
endif()

# Release optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

include_directories(.)

# ---------------------------------------------------------------------------
# Engine and solver: rules as pure functions, an exact solver over them, and a
# text notation for positions. No input, no output, no global state.
# ---------------------------------------------------------------------------
add_library(bsr_core STATIC
    engine/Items.cpp
    engine/Tube.cpp
    engine/State.cpp
    engine/Rules.cpp
    engine/Config.cpp
    engine/Notation.cpp
    solver/Solver.cpp
)
target_include_directories(bsr_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(bsr_core PUBLIC bsr_warnings)

# Position advisor: enter any position, get every move ranked by win probability.
add_executable(advisor cli/advisor.cpp)
target_link_libraries(advisor PRIVATE bsr_core bsr_warnings)

# Play a round against the solver.
add_executable(play cli/play.cpp)
target_link_libraries(play PRIVATE bsr_core bsr_warnings)

# ---------------------------------------------------------------------------
# Previous engine, kept until the new one covers everything it did.
# ---------------------------------------------------------------------------
set(LEGACY_SOURCES
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

add_executable(${PROJECT_NAME} main.cpp ${LEGACY_SOURCES})
add_executable(simulate simulate.cpp ${LEGACY_SOURCES})

# ---------------------------------------------------------------------------
# Tests. On by default: a test suite nobody runs is not a test suite, and the
# README's build instructions used to fail because this defaulted to OFF.
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests" ON)

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

    file(GLOB LEGACY_TEST_SOURCES tests/legacy/*.cpp)
    file(GLOB CORE_TEST_SOURCES tests/*.cpp)

    if(CORE_TEST_SOURCES)
        add_executable(tests ${CORE_TEST_SOURCES})
        target_link_libraries(tests PRIVATE bsr_core bsr_warnings GTest::gtest_main)
        gtest_discover_tests(tests)
    endif()

    if(LEGACY_TEST_SOURCES)
        add_executable(legacy_tests ${LEGACY_TEST_SOURCES} ${LEGACY_SOURCES})
        target_link_libraries(legacy_tests PRIVATE GTest::gtest_main)
        target_include_directories(legacy_tests PRIVATE ${CMAKE_SOURCE_DIR})
        gtest_discover_tests(legacy_tests)
    endif()
endif()
