#include "UI files/ui.hpp"
#include "Engines/NewEvaluator.hpp"

int main() {
    ChessUI ui(
        1,
        3,
        nullptr,
        std::make_unique<NewEvaluator>()
    );
    ui.run();

    return 0;
}