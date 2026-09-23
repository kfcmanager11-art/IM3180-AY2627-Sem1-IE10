cmake_minimum_required(VERSION 3.28)
project(ChessAI LANGUAGES CXX)

option(CHESS_BUILD_GUI "Build the SFML chess application" ON)
include(CTest)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chess_core STATIC
    board.cpp
    board.hpp
    evaluator.cpp
    evaluator.hpp
    engine.cpp
    engine.hpp
    Engines/NewEvaluator.cpp
    Engines/NewEvaluator.hpp
)
target_include_directories(chess_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(chess_core PUBLIC cxx_std_17)

if(CHESS_BUILD_GUI)
    include(FetchContent)

    # The UI uses both the Graphics and Audio modules.
    set(SFML_BUILD_AUDIO ON CACHE BOOL "Build SFML's Audio module" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "Build SFML's Network module" FORCE)

    FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 3.0.2
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
        SYSTEM
    )
    FetchContent_MakeAvailable(SFML)

    add_executable(chess main.cpp "UI files/ui.cpp" "UI files/ui.hpp")
    set_target_properties(chess PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(chess PRIVATE chess_core SFML::Graphics SFML::Audio)
endif()

if(BUILD_TESTING)
    add_executable(board_engine_tests tests/board_engine_tests.cpp)
    target_link_libraries(board_engine_tests PRIVATE chess_core)
    add_test(NAME board_engine_tests COMMAND board_engine_tests)
endif()
