#pragma once

#include "board.hpp"
#include "evaluator.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <deque>

class Engine {
    enum class BoundType : std::uint8_t { Exact = 0, Lower = 1, Upper = 2 };

    // Flat open-addressed transposition table with a fixed size per engine
    // (1M entries = 16 MiB). A slot whose key is 0 is empty; a hit requires
    // an exact 64-bit key match.
    static constexpr std::size_t kTTSize = 1u << 20;
    static constexpr std::size_t kTTMask = kTTSize - 1;

    struct TranspositionEntry {
        std::uint64_t key = 0;
        int score = 0;
        std::uint8_t depth = 0;    // remaining depth when stored
        std::uint8_t bound = 0;    // BoundType
        std::uint32_t best_move = 0; // encoded best move, 0 = none
    };

    static std::uint32_t encode_move(const Move& move);
    static Move decode_move(std::uint32_t packed);
    void clear_transposition();

    int side;
    int default_search_depth;
    std::unique_ptr<Evaluator> evaluator;
    Move best_move{};
    std::size_t search_nodes = 0;
    std::size_t closed_window_nodes = 0;
    std::unique_ptr<TranspositionEntry[]> transposition;

    int negamax_search(Board& board, int depth, int max_depth,
                       int alpha, int beta, Move* root_move = nullptr);

    std::deque<std::pair<int,Hash>> evaluation_cache;

public:
    explicit Engine(int side, int search_depth = 2,
                    std::unique_ptr<Evaluator> evaluator = nullptr);
    Engine(int side, int search_depth, EngineConfig config);

    int get_side() const;
    int get_search_depth() const;
    void set_search_depth(int search_depth);
    const Evaluator& get_evaluator() const;
    const EngineConfig& get_config() const;

    bool is_turn(const Board& board) const;
    int evaluate(const Board& board) const;
    bool find_best_move(const Board& position, int search_depth = -1);
    const Move& get_best_move() const;
    std::size_t get_search_nodes() const;
    std::size_t get_closed_window_nodes() const;
};
