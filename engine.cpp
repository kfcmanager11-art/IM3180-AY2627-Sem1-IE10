#include "engine.hpp"

#include <algorithm>
#include <stdexcept>

Engine::Engine(int engine_side, int search_depth,
               std::unique_ptr<Evaluator> engine_evaluator)
    : side(engine_side), default_search_depth(search_depth),
      evaluator(std::move(engine_evaluator)),
      transposition(std::make_unique<TranspositionEntry[]>(kTTSize)) {
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

std::uint32_t Engine::encode_move(const Move& move) {
    if (move.old_x < 0 || move.new_x < 0) return 0;
    const std::uint32_t from =
        static_cast<std::uint32_t>(move.old_x * 8 + move.old_y);
    const std::uint32_t to =
        static_cast<std::uint32_t>(move.new_x * 8 + move.new_y);
    // from (6 bits) | to (6 bits) | special move (3 bits) = 15 bits.
    return (from << 12) | (to << 4) |
           static_cast<std::uint32_t>(move.special_move);
}

Move Engine::decode_move(std::uint32_t packed) {
    Move move;
    const int from = static_cast<int>(packed >> 12);
    const int to = static_cast<int>((packed >> 4) & 0x3Fu);
    move.old_x = from / 8;
    move.old_y = from % 8;
    move.new_x = to / 8;
    move.new_y = to % 8;
    move.special_move = static_cast<SpecialMove>(packed & 0x7u);
    return move;
}

void Engine::clear_transposition() {
    std::fill(transposition.get(), transposition.get() + kTTSize,
              TranspositionEntry{});
}

std::vector<Move> Engine::order_moves(Board& board,
                                      std::uint32_t tt_best) const {
    const std::vector<Move> legal = board.legal_moves();
    const std::size_t n = legal.size();

    // Piece values for MVV-LVA: pawn..king (king = 99999 in the standard
    // config, so a king capture sorts to the very top).
    constexpr int piece_value[7] = {0, 1, 3, 3, 5, 9, 99999};

    // Score each move: MVV-LVA for captures, 0 for quiet moves.
    std::vector<int> scores(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const Move& move = legal[i];
        const int victim = board.get_piece(move.new_x, move.new_y);
        if (victim == 0) continue;
        const int attacker = board.get_piece(move.old_x, move.old_y);
        scores[i] = piece_value[std::abs(victim)] * 8 -
                    piece_value[std::abs(attacker)];
    }

    // Stable index sort: highest score first; quiet moves and equal scores
    // keep board order, so tie-breaking stays deterministic.
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
                     [&scores](std::size_t a, std::size_t b) {
                         return scores[a] > scores[b];
                     });

    std::vector<Move> moves(n);
    for (std::size_t i = 0; i < n; ++i) moves[i] = legal[idx[i]];

    // TT best move first, if it is still legal.
    if (tt_best != 0) {
        const Move best = decode_move(tt_best);
        for (std::size_t i = 0; i < n; ++i) {
            if (moves[i] == best) {
                std::swap(moves[0], moves[i]);
                break;
            }
        }
    }
    return moves;
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
    TranspositionEntry& entry = transposition[key & kTTMask];
    if (entry.key == key && entry.depth >= max_depth - depth) {
        if (entry.bound == static_cast<std::uint8_t>(BoundType::Exact))
            return entry.score;
        if (entry.bound == static_cast<std::uint8_t>(BoundType::Lower))
            alpha = std::max(alpha, entry.score);
        else
            beta = std::min(beta, entry.score);
        if (alpha >= beta) return entry.score;
    }

    int best_score = NEGINF;
    bool found_move = false;
    const int turn_before = board.get_current_turn();

    // TT best move first, then captures by MVV-LVA, then quiet moves.
    const std::uint32_t tt_best = (entry.key == key &&
                                   entry.depth >= max_depth - depth)
                                      ? entry.best_move
                                      : 0;
    const std::vector<Move> moves = order_moves(board, tt_best);

    for (const Move& move : moves) {
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
            entry.best_move = encode_move(move);
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
    // Replacement policy: a stale slot is taken over; otherwise a new entry
    // must be at least as deep as the incumbent, so shallow probes never
    // clobber deeper results.
    const int new_depth = max_depth - depth;
    if (entry.key != key || new_depth >= entry.depth) {
        entry.key = key;
        entry.score = best_score;
        entry.depth = static_cast<std::uint8_t>(new_depth);
        entry.bound = static_cast<std::uint8_t>(bound);
        if (!found_move) entry.best_move = 0;
    }
    return best_score;
}

bool Engine::find_best_move(const Board& position, int requested_depth) {
    int search_depth = requested_depth < 0 ? default_search_depth : requested_depth;
    if (search_depth < 1) throw std::invalid_argument("search depth must be positive");

    best_move = Move{};
    search_nodes = 0;
    closed_window_nodes = 0;
    clear_transposition();

    if (!is_turn(position)) return false;

    Board search_board = position;
    negamax_search(search_board, 0, search_depth, NEGINF, INF, &best_move);
    return best_move.old_x != -1;
}

const Move& Engine::get_best_move() const { return best_move; }
