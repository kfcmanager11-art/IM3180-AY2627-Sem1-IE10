#include "engine.hpp"

#include <algorithm>
#include <stdexcept>

Engine::Engine(int engine_side, int search_depth,
               std::unique_ptr<Evaluator> engine_evaluator)
    : side(engine_side), default_search_depth(search_depth),
      evaluator(std::move(engine_evaluator)) {
    if (side < 0 || side > 1) {
        throw std::invalid_argument("engine side must be 0 (White) or 1 (Black)");
    }
    if (!evaluator)
        evaluator = std::make_unique<StandardEvaluator>();
    set_search_depth(search_depth);
}

Engine::Engine(int engine_side, int search_depth, EngineConfig config)
    : Engine(engine_side, search_depth,
             std::make_unique<StandardEvaluator>(std::move(config))) {}

int Engine::get_side() const { return side; }
int Engine::get_search_depth() const { return default_search_depth; }

void Engine::set_search_depth(int search_depth) {
    if (search_depth < 1) {
        throw std::invalid_argument("search depth must be positive");
    }
    default_search_depth = search_depth;
}

const Evaluator& Engine::get_evaluator() const { return *evaluator; }

const EngineConfig& Engine::get_config() const {
    const auto* standard_evaluator =
        dynamic_cast<const StandardEvaluator*>(evaluator.get());
    if (!standard_evaluator) {
        throw std::logic_error(
            "get_config() is only available for StandardEvaluator");
    }
    return standard_evaluator->get_config();
}

bool Engine::is_turn(const Board& board) const {
    return !board.has_game_ended() && board.get_current_turn() == side;
}

std::size_t Engine::get_search_nodes() const { return search_nodes; }
std::size_t Engine::get_closed_window_nodes() const { return closed_window_nodes; }

int Engine::evaluate(const Board& board) const {
    return evaluator->evaluate(board);
}

int Engine::negamax_search(Board& board, int depth, int max_depth,
                           int alpha, int beta, Move* root_move) {
    ++search_nodes;
    if (alpha >= beta) ++closed_window_nodes;

    if (board.has_game_ended()) return evaluate(board);
    if (board.get_moves_left() == 2 && depth >= max_depth) return evaluate(board);

    const Hash key = board.get_hash();
    const int original_alpha = alpha;
    const int original_beta = beta;
    auto cached = transposition.find(key);
    if (cached != transposition.end() && cached->second.depth >= max_depth - depth) {
        if (cached->second.bound == BoundType::Exact) return cached->second.score;
        if (cached->second.bound == BoundType::Lower)
            alpha = std::max(alpha, cached->second.score);
        else
            beta = std::min(beta, cached->second.score);
        if (alpha >= beta) return cached->second.score;
    }

    int best_score = NEGINF;
    bool found_move = false;
    const int turn_before = board.get_current_turn();

    for (const Move& move : board.legal_moves()) {
        if (!board.make_move(move)) continue;

        bool turn_changed = board.get_current_turn() != turn_before;
        int child_score;
        if (turn_changed) {
            child_score = -negamax_search(board, depth + 1, max_depth,
                                          -beta, -alpha, nullptr);
        } else {
            child_score = negamax_search(board, depth, max_depth,
                                         alpha, beta, nullptr);
        }

        if (!found_move || child_score > best_score) {
            best_score = child_score;
            if (root_move) *root_move = move;
        }
        found_move = true;
        alpha = std::max(alpha, child_score);
        board.rollback_move();

        if (alpha >= beta) break;
    }

    if (!found_move) best_score = evaluate(board);
    const BoundType bound = best_score <= original_alpha
        ? BoundType::Upper
        : (best_score >= original_beta ? BoundType::Lower : BoundType::Exact);
    transposition[key] = {max_depth - depth, best_score, bound};
    return best_score;
}

bool Engine::find_best_move(const Board& position, int requested_depth) {
    int search_depth = requested_depth < 0 ? default_search_depth : requested_depth;
    if (search_depth < 1) throw std::invalid_argument("search depth must be positive");

    best_move = Move{};
    search_nodes = 0;
    closed_window_nodes = 0;
    transposition.clear();

    if (!is_turn(position)) return false;

    Board search_board = position;
    negamax_search(search_board, 0, search_depth, NEGINF, INF, &best_move);
    return best_move.old_x != -1;
}

const Move& Engine::get_best_move() const { return best_move; }
