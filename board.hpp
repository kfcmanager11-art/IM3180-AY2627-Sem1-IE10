#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <random>
#include <tuple>

using Hash = std::uint64_t;

inline const int INF = 1e9, NEGINF = -1e9;

class Board {
    friend struct BoardTestAccess;

    std::vector<std::vector<int>> current_board{8, std::vector<int>(8, 0)};
    std::vector<std::tuple<int, int, int, int, int, int, int>> rollback; // old_x, old_y, new_x, new_y, piece, turn, move_left
    std::unordered_map<int, int> piece_value;
    std::unordered_map<int, Hash> piece_hash, turn_hash, move_left_hash;
    std::unordered_map<int, std::vector<std::pair<int, int>>> move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_capture_pattern;
    
    int turn;
    int move_left;
    int game_status;
    Hash board_hash;
    std::mt19937_64 rng{std::random_device{}()};

public:
    Board();
    ~Board() = default;

    auto begin() -> std::vector<std::vector<int>>::iterator;
    auto end() -> std::vector<std::vector<int>>::iterator;
    auto cbegin() const -> std::vector<std::vector<int>>::const_iterator;
    auto cend() const -> std::vector<std::vector<int>>::const_iterator;

    bool inboard(int x, int y) const;
    bool check_game_ended();
    bool has_game_ended() const;
    int get_current_turn() const;
    int get_moves_left() const;
    int get_game_status() const;
    int get_piece(int r, int c) const;
    int get_piece_value(int piece) const;

    const std::vector<std::pair<int, int>>& get_move_patterns(int piece) const;
    const std::vector<std::pair<int, int>>& get_pawn_move_patterns(int piece) const;
    const std::vector<std::pair<int, int>>& get_pawn_capture_patterns(int piece) const;

    void get_turn();
    bool valid_move(int old_x, int old_y, int new_x, int new_y);
    bool make_move(int old_x, int old_y, int new_x, int new_y);
    bool make_capture(int old_x, int old_y, int new_x, int new_y);
    void rollback_move();
};