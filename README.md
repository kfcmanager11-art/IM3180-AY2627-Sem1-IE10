# IM3180-AY2627-Sem1-IE10

This project is a chess-like game with an SFML interface and a configurable
search engine.

## Build with CMake

The default configuration builds the GUI and fetches SFML 3.0.2 during the
CMake configure step. A machine with CMake, a C++17 compiler, Git, and network
access for the first configure can therefore build the project from a fresh
clone:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the public Board/Engine checks with:

```powershell
ctest --test-dir build --output-on-failure
```

The core library and tests can be built without SFML:

```powershell
cmake -S . -B build-core -G "MinGW Makefiles" -DCHESS_BUILD_GUI=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

The older `board_tests.cpp` suite targets the former Board-owned engine API and
is intentionally not part of the CMake test target while it is migrated. The
default test target uses the new public Board/Engine API.

## Architecture

- `Board` owns the authoritative position, turn/action rules, legal move
  generation, move rollback, terminal state, and position hash.
- `Engine` owns one side's search depth, negamax implementation, counters,
  best move, transposition table, and an evaluator object.
- `Evaluator` owns evaluation logic and any playstyle-specific state. The
  built-in `StandardEvaluator` uses `EngineConfig`; custom playstyles can
  derive from `Evaluator` without changing the search implementation.
- `ChessUI` owns one `Board` plus independent White and Black `Engine` objects.
  Player-vs-Player, Player-vs-Engine, and Engine-vs-Engine modes use the same
  Board while selecting the engine associated with the current side.

Each engine can receive its own `EngineConfig`, so evaluation metrics are not
shared accidentally between competing strategies.

To add a new playstyle, derive from `Evaluator` and inject it into an `Engine`:

```cpp
class KingSafetyEvaluator : public Evaluator {
public:
    int evaluate(const Board& board) const override {
        // Calculate and return a score from the side-to-move perspective.
        return 0;
    }
};

Engine white(0, 3, std::make_unique<KingSafetyEvaluator>());
```

`Engine::find_best_move` searches on a private copy of the supplied `Board`, so
the UI's authoritative position is not modified by search or evaluation.
The same evaluator injection is available through `ChessUI`'s optional White
and Black evaluator constructor arguments; omitting them keeps the standard
evaluator for both sides.

## Running the game

After building, run `build/chess.exe` (or the equivalent executable path for
the selected generator). The UI starts at the menu, where the game mode and
engine side can be selected. The optional `ChessUI` constructor arguments still
set the default engine side and search depth for code that constructs the UI
directly:

```cpp
ChessUI ui(1, 2); // default engine side: Black, search depth: 2
```
