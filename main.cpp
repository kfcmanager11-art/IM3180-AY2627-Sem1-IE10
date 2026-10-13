#include "UI files/ui.hpp"
#include "Engines/NewEvaluator.hpp"
#include "Engines/SecondEvaluator.hpp"

int main() {
    ChessUI ui(
        1,
        Engine::kDefaultSearchDepth,
        std::make_unique<SecondEvaluator>(),
        std::make_unique<NewEvaluator>()
    );
    ui.run();

    return 0;
}