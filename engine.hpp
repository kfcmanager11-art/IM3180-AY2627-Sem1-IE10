#pragma once

#include "board.hpp"
#include <tuple>

class Engine {
private:
    static const int pawn_sq_table[64];
    static const int knight_sq_table[64];
    static const int king_sq_table[64];

    int board_eval(const Board& board) const;
    int negamax_search(Board& board, int depth, int max_depth, int alpha, int beta,
                       std::tuple<int, int, int, int>* root_move = nullptr);

public:
    Engine() = default;
    ~Engine() = default;

    bool find_best_move(Board& board, int search_depth, std::tuple<int, int, int, int>& out_move);
};