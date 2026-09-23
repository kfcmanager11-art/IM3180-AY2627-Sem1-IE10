#pragma once

#include<vector>
#include<unordered_map>
#include<cstdint>
#include<random>

using Hash = std::uint64_t;

inline const int INF = 1e9, NEGINF = -1e9;

enum class SpecialMove : std::uint8_t {
    None = 0,
    CastleKingside = 1,
    CastleQueenside = 2,
    PromoteQueen = 3
};

struct Move {
    int old_x = -1;
    int old_y = -1;
    int new_x = -1;
    int new_y = -1;
    SpecialMove special_move = SpecialMove::None;

    bool operator==(const Move& other) const {
        return old_x == other.old_x && old_y == other.old_y
            && new_x == other.new_x && new_y == other.new_y
            && special_move == other.special_move;
    }
};

enum class CastlingRight : std::uint8_t {
    WhiteKingside = 1u << 0,
    WhiteQueenside = 1u << 1,
    BlackKingside = 1u << 2,
    BlackQueenside = 1u << 3
};

class Board{
    friend struct BoardTestAccess;

    struct RollbackState {
        Move move;
        int moved_piece = 0;
        int captured_piece = 0;
        int old_turn = 0;
        int old_move_left = 0;
        int old_game_status = 0;
        std::uint8_t old_castling_rights = 0;
        Move old_last_move;
        Hash old_hash = 0;
    };

    std::vector<std::vector<int>> current_board{8, std::vector<int> (8, 0)};
    std::vector<RollbackState> rollback;
    std::unordered_map<int, Hash> piece_hash, turn_hash, move_left_hash;
    std::unordered_map<int, Hash> castling_rights_hash;
    std::unordered_map<int, std::vector<std::pair<int, int>>> move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_capture_pattern;
    int turn;
    int move_left;
    int game_status;
    std::uint8_t castling_rights;
    Hash board_hash;
    Move last_move;
    std::mt19937_64 rng{std::random_device{}()};

    bool valid_move_geometry(int old_x, int old_y, int new_x, int new_y,
                             SpecialMove* special_move = nullptr) const;
    bool valid_castle(int old_x, int old_y, int new_x, int new_y,
                      SpecialMove& special_move) const;
    void update_castling_rights(int old_x, int old_y, int new_x, int new_y,
                                int moved_piece, int captured_piece);
    Hash calculate_hash() const;
    void refresh_hash();
    void set_piece_with_hash(int row, int column, int piece);

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
    int get_piece(int x, int y) const;
    Hash get_hash() const;
    std::uint8_t get_castling_rights() const;
    const Move& get_last_move() const;
    std::vector<Move> legal_moves();
    void get_turn();
    bool valid_move(int old_x, int old_y, int new_x, int new_y);
    bool make_move(const Move& move);
    bool make_move(int old_x, int old_y, int new_x, int new_y);
    bool make_capture(int old_x, int old_y, int new_x, int new_y);
    void rollback_move();
};

