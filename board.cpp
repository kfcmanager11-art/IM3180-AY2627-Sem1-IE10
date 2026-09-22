#include "board.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

Board::Board() {
    turn = 0;
    move_left = 1;
    game_status = 0;
    board_hash = 0;

    std::uniform_int_distribution<Hash> distribution(0, UINT64_MAX);
    for (int piece = -6; piece <= 6; ++piece) {
        for (int square = 0; square < 64; ++square) {
            piece_hash[piece * 64 + square] = distribution(rng);
        }
    }
    turn_hash[0] = distribution(rng);
    turn_hash[1] = distribution(rng);
    move_left_hash[1] = distribution(rng);
    move_left_hash[2] = distribution(rng);

    for (int column = 0; column < 8; ++column) {
        current_board[1][column] = 1;
        current_board[6][column] = -1;
    }
    current_board[0] = {4, 2, 3, 6, 5, 3, 2, 4};
    for (int column = 0; column < 8; ++column) {
        current_board[7][column] = -current_board[0][column];
    }

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            board_hash ^= piece_hash[current_board[row][column] * 64 + row * 8 + column];
        }
    }
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    pawn_move_pattern[1] = {{1, 0}};
    pawn_move_pattern[-1] = {{-1, 0}};
    pawn_capture_pattern[1] = {{1, 1}, {1, -1}};
    pawn_capture_pattern[-1] = {{-1, 1}, {-1, -1}};

    const std::vector<std::pair<int, int>> knight_directions = {
        {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
        {1, 2}, {1, -2}, {-1, 2}, {-1, -2}
    };
    move_pattern[2] = knight_directions;
    move_pattern[-2] = knight_directions;

    auto add_sliding_patterns = [&](int piece, const std::vector<std::pair<int, int>>& directions,
                                    int max_distance) {
        for (const auto& direction : directions) {
            for (int distance = 1; distance <= max_distance; ++distance) {
                move_pattern[piece].emplace_back(
                    direction.first * distance, direction.second * distance);
                move_pattern[-piece].emplace_back(
                    direction.first * distance, direction.second * distance);
            }
        }
    };

    const std::vector<std::pair<int, int>> bishop_directions = {
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    const std::vector<std::pair<int, int>> rook_directions = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
    const std::vector<std::pair<int, int>> king_directions = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    add_sliding_patterns(3, bishop_directions, 7);
    add_sliding_patterns(4, rook_directions, 7);
    add_sliding_patterns(5, {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    }, 7);
    add_sliding_patterns(6, king_directions, 1);
}

auto Board::begin() -> std::vector<std::vector<int>>::iterator {
    return current_board.begin();
}

auto Board::end() -> std::vector<std::vector<int>>::iterator {
    return current_board.end();
}

auto Board::cbegin() const -> std::vector<std::vector<int>>::const_iterator {
    return current_board.cbegin();
}

auto Board::cend() const -> std::vector<std::vector<int>>::const_iterator {
    return current_board.cend();
}

bool Board::inboard(int x, int y) const {
    return x >= 0 && y >= 0 && x < 8 && y < 8;
}

bool Board::check_game_ended() {
    if (game_status == 0) return false;
    std::cout << "Game ended\n";
    std::cout << (game_status > 0 ? "White won\n" : "Black won\n");
    return true;
}

bool Board::has_game_ended() const { return game_status != 0; }
int Board::get_current_turn() const { return turn; }
int Board::get_moves_left() const { return move_left; }
int Board::get_game_status() const { return game_status; }

int Board::get_piece(int x, int y) const {
    return inboard(x, y) ? current_board[x][y] : 0;
}

Hash Board::get_hash() const { return board_hash; }

std::vector<Move> Board::legal_moves() {
    std::vector<Move> moves;
    for (int old_x = 0; old_x < 8; ++old_x) {
        for (int old_y = 0; old_y < 8; ++old_y) {
            if (current_board[old_x][old_y] == 0) continue;
            for (int new_x = 0; new_x < 8; ++new_x) {
                for (int new_y = 0; new_y < 8; ++new_y) {
                    if (valid_move(old_x, old_y, new_x, new_y)) {
                        moves.emplace_back(old_x, old_y, new_x, new_y);
                    }
                }
            }
        }
    }
    return moves;
}

void Board::get_turn() {
    std::cout << (turn == 0 ? "White turn\n" : "Black turn\n");
    std::cout << move_left << " move(s) left\n";
}

bool Board::valid_move(int old_x, int old_y, int new_x, int new_y) {
    if (has_game_ended()) return false;
    if (!inboard(old_x, old_y) || !inboard(new_x, new_y)) return false;

    int piece = current_board[old_x][old_y];
    if (piece == 0) return false;
    if ((turn == 1 && piece > 0) || (turn == 0 && piece < 0)) return false;

    int destination = current_board[new_x][new_y];
    if (destination != 0 && move_left < 2) return false;
    if (destination != 0 && (piece > 0) == (destination > 0)) return false;

    int dx = new_x - old_x;
    int dy = new_y - old_y;

    if (std::abs(piece) == 3 || std::abs(piece) == 4 || std::abs(piece) == 5) {
        int step_x = (dx > 0) - (dx < 0);
        int step_y = (dy > 0) - (dy < 0);
        for (int distance = 1; distance < std::max(std::abs(dx), std::abs(dy)); ++distance) {
            const int check_x = old_x + distance * step_x;
            const int check_y = old_y + distance * step_y;
            if (!inboard(check_x, check_y)) return false;
            if (current_board[check_x][check_y] != 0)
                return false;
        }
    }

    if (std::abs(piece) == 1) {
        const auto& patterns = destination == 0
            ? pawn_move_pattern[piece]
            : pawn_capture_pattern[piece];
        return std::find(patterns.begin(), patterns.end(), std::make_pair(dx, dy)) != patterns.end();
    }

    auto pattern = move_pattern.find(piece);
    if (pattern == move_pattern.end()) return false;
    return std::find(pattern->second.begin(), pattern->second.end(), std::make_pair(dx, dy))
        != pattern->second.end();
}

bool Board::make_move(int old_x, int old_y, int new_x, int new_y) {
    if (has_game_ended()) return false;
    if (!valid_move(old_x, old_y, new_x, new_y)) return false;
    if (current_board[new_x][new_y] != 0)
        return make_capture(old_x, old_y, new_x, new_y);

    int piece = current_board[old_x][old_y];
    board_hash ^= piece_hash[piece * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[old_x * 8 + old_y];
    board_hash ^= turn_hash[turn] ^ move_left_hash[move_left];

    rollback.emplace_back(old_x, old_y, new_x, new_y, 0, turn, move_left);
    current_board[new_x][new_y] = piece;
    current_board[old_x][old_y] = 0;
    --move_left;
    if (move_left == 0) {
        turn = 1 - turn;
        move_left = 2;
    }

    board_hash ^= piece_hash[piece * 64 + new_x * 8 + new_y];
    board_hash ^= piece_hash[new_x * 8 + new_y];
    board_hash ^= turn_hash[turn] ^ move_left_hash[move_left];
    return true;
}

bool Board::make_capture(int old_x, int old_y, int new_x, int new_y) {
    if (!valid_move(old_x, old_y, new_x, new_y)) return false;
    int captured_piece = current_board[new_x][new_y];
    if (captured_piece == 0) return false;

    int piece = current_board[old_x][old_y];
    board_hash ^= piece_hash[piece * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[captured_piece * 64 + new_x * 8 + new_y];
    board_hash ^= piece_hash[old_x * 8 + old_y];
    board_hash ^= turn_hash[turn] ^ move_left_hash[move_left];

    rollback.emplace_back(old_x, old_y, new_x, new_y, captured_piece, turn, move_left);
    if (std::abs(captured_piece) == 6) game_status = -captured_piece / 6;
    current_board[new_x][new_y] = piece;
    current_board[old_x][old_y] = 0;
    turn = 1 - turn;

    board_hash ^= piece_hash[piece * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn] ^ move_left_hash[move_left];
    return true;
}

void Board::rollback_move() {
    if (rollback.empty()) return;
    auto [old_x, old_y, new_x, new_y, captured_piece, old_turn, old_move_left] = rollback.back();
    rollback.pop_back();

    board_hash ^= piece_hash[current_board[old_x][old_y] * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[current_board[new_x][new_y] * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn] ^ move_left_hash[move_left];

    current_board[old_x][old_y] = current_board[new_x][new_y];
    current_board[new_x][new_y] = captured_piece;
    if (std::abs(captured_piece) == 6) game_status = 0;
    turn = old_turn;
    move_left = old_move_left;

    board_hash ^= piece_hash[current_board[old_x][old_y] * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[current_board[new_x][new_y] * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn] ^ move_left_hash[move_left];
}
