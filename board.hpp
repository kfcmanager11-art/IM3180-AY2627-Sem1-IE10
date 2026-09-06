#pragma once

#include<vector>
#include<unordered_map>
#include<cstdint>
#include<random>
#include<tuple>

using Hash = std::uint64_t;

inline const int INF = 1e9, NEGINF = -1e9;

class Board{
    friend struct BoardTestAccess;

    std::vector<std::vector<int>> current_board{8, std::vector<int> (8, 0)};
    std::vector<std::tuple<int, int, int, int, int, int, int>> rollback; //old old new new piece turn move_left
    std::unordered_map<int, int> piece_value;
    std::unordered_map<int, Hash> piece_hash, turn_hash, move_left_hash;
    std::unordered_map<int, std::vector<std::pair<int, int>>> move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_capture_pattern;
    int turn;
    int move_left;
    int game_status;
    int engine_side;
    Hash board_hash;
    std::mt19937_64 rng{std::random_device{}()};
    std::size_t search_nodes = 0;
    std::size_t closed_window_nodes = 0;
    std::tuple<int, int, int, int> best_move{-1, -1, -1, -1};

    int negamax_search(int depth, int max_depth, int alpha, int beta,
        std::tuple<int, int, int, int>* root_move = nullptr);

public:
    explicit Board(int engineSide = 0);
    ~Board() = default;

    auto begin() -> std::vector<std::vector<int>>::iterator;
    auto end() -> std::vector<std::vector<int>>::iterator;
    auto cbegin() const -> std::vector<std::vector<int>>::const_iterator;
    auto cend() const -> std::vector<std::vector<int>>::const_iterator;

    bool inboard(int x, int y);
    bool check_game_ended();
    bool has_game_ended() const;
    bool is_engine_turn() const;
    int get_engine_side() const;
    int get_current_turn() const;
    int get_moves_left() const;
    const std::tuple<int, int, int, int>& get_best_move() const;
    bool find_best_move(int search_depth = 2);
    void get_turn();
    bool valid_move(int old_x, int old_y, int new_x, int new_y);
    bool make_move(int old_x, int old_y, int new_x, int new_y);
    bool make_capture(int old_x, int old_y, int new_x, int new_y);
    int board_eval();
    void rollback_move();
    int negamax(int move_remaining, int depth, int alpha = NEGINF, int beta = INF);
};

