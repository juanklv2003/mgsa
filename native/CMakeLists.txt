cmake_minimum_required(VERSION 3.20)
project(mgsa_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mgsa_game STATIC src/game.cpp)
target_include_directories(mgsa_game PUBLIC include)

enable_testing()
add_executable(mgsa_tests tests/game_tests.cpp)
target_link_libraries(mgsa_tests PRIVATE mgsa_game)
add_test(NAME game COMMAND mgsa_tests)
