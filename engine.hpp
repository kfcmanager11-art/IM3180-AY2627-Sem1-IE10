#pragma once

#include "board.hpp"
#include "evaluator.hpp"

#include <cstddef>
#include <memory>
#include <tuple>
#include <unordered_map>

class Engine {
    enum class BoundType { Exact, Lower, Upper };

    struct TranspositionEntry {
        int depth;
        int score;
        BoundType bound;
    };

    int side;
    int default_search_depth;
    std::unique_ptr<Evaluator> evaluator;
    Move best_move{-1, -1, -1, -1};
    std::size_t search_nodes = 0;
    std::size_t closed_window_nodes = 0;
    std::unordered_map<Hash, TranspositionEntry> transposition;

    int negamax_search(Board& board, int depth, int max_depth,
                       int alpha, int beta, Move* root_move = nullptr);

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
