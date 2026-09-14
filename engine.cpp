#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <stdexcept>

const int Engine::pawn_sq_table[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

const int Engine::knight_sq_table[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

const int Engine::king_sq_table[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
   -10,-20,-20,-20,-20,-20,-20,-10,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30
};

bool Engine::find_best_move(Board& board, int search_depth, std::tuple<int, int, int, int>& out_move) {
    if (search_depth < 1) throw std::invalid_argument("search_depth must be positive");
    out_move = {-1, -1, -1, -1};
    negamax_search(board, 0, search_depth, NEGINF, INF, &out_move);
    return std::get<0>(out_move) != -1;
}

int Engine::board_eval(const Board& board) const {
    if (board.has_game_ended()) {
        int winner_score = (board.get_game_status() > 0) ? 999999 : -999999;
        return winner_score * (board.get_current_turn() ? -1 : 1);
    }

    int raw_score = 0;
    int pawn_count_white[8] = {0};
    int pawn_count_black[8] = {0};

    int white_king_x = -1, white_king_y = -1;
    int black_king_x = -1, black_king_y = -1;

    std::vector<std::pair<int, int>> white_rooks;
    std::vector<std::pair<int, int>> black_rooks;
    const int passed_pawn_bonus[8] = {0, 5, 15, 30, 60, 100, 160, 0};

    auto is_on_board = [](int r, int c) {
        return r >= 0 && r < 8 && c >= 0 && c < 8;
    };

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int piece = board.get_piece(r, c);
            if (piece == 0) continue;

            int abs_p = std::abs(piece);
            int mat = board.get_piece_value(abs_p);
            int pst = 0;
            int sq_idx = (piece > 0) ? (r * 8 + c) : ((7 - r) * 8 + c); 

            switch (abs_p) {
                case 1: {
                    pst = pawn_sq_table[sq_idx];
                    if (piece > 0) pawn_count_white[c]++;
                    else pawn_count_black[c]++;

                    bool is_passed = true;
                    int forward_dir = (piece > 0) ? 1 : -1;
                    for (int check_r = r + forward_dir; check_r >= 0 && check_r < 8; check_r += forward_dir) {
                        for (int check_c = c - 1; check_c <= c + 1; ++check_c) {
                            if (is_on_board(check_r, check_c)) {
                                int enemy_pawn = (piece > 0) ? -1 : 1;
                                if (board.get_piece(check_r, check_c) == enemy_pawn) {
                                    is_passed = false;
                                    break;
                                }
                            }
                        }
                        if (!is_passed) break;
                    }
                    if (is_passed) {
                        int rank_idx = (piece > 0) ? r : (7 - r);
                        raw_score += (piece > 0 ? 1 : -1) * passed_pawn_bonus[rank_idx];
                    }
                    break;
                }    
                case 2: {
                    pst = knight_sq_table[sq_idx];
                    int mobility = 0;
                    const int knight_moves[8][2] = {
                        {-2,-1}, {-2,1}, {-1,-2}, {-1,2},
                        { 1,-2}, { 1,2}, { 2,-1}, { 2,1}
                    };
                    for (auto& m : knight_moves) {
                        int nr = r + m[0], nc = c + m[1];
                        if (is_on_board(nr, nc)) {
                            int dest = board.get_piece(nr, nc);
                            if (dest == 0 || (dest > 0) != (piece > 0)) mobility++;
                        }
                    }
                    raw_score += (piece > 0 ? 1 : -1) * (mobility * 4);
                    break;
                }
                case 3: {
                    int mobility = 0;
                    const int bishop_dirs[4][2] = {{-1,-1}, {-1,1}, {1,-1}, {1,1}};
                    for (auto& d : bishop_dirs) {
                        int nr = r + d[0], nc = c + d[1];
                        while (is_on_board(nr, nc)) {
                            int dest = board.get_piece(nr, nc);
                            if (dest == 0) {
                                mobility++;
                            } else {
                                if ((dest > 0) != (piece > 0)) mobility++;
                                break;
                            }
                            nr += d[0]; nc += d[1];
                        }
                    }
                    raw_score += (piece > 0 ? 1 : -1) * (mobility * 3);
                    break;
                }
                case 4:
                    if (piece > 0) white_rooks.push_back({r, c});
                    else black_rooks.push_back({r, c});
                    break;
                case 6: 
                    pst = king_sq_table[sq_idx];
                    if (piece > 0) { white_king_x = r; white_king_y = c; }
                    else { black_king_x = r; black_king_y = c; }
                    break;
                default:
                    break;
            }

            int total_piece_val = (piece > 0) ? (mat * 100 + pst) : -(mat * 100 + pst);
            raw_score += total_piece_val;
        }
    }

    for (int c = 0; c < 8; ++c) {
        if (pawn_count_white[c] > 1) raw_score -= (pawn_count_white[c] - 1) * 20; 
        if (pawn_count_white[c] > 0) {
            bool left = (c > 0) && (pawn_count_white[c - 1] > 0);
            bool right = (c < 7) && (pawn_count_white[c + 1] > 0);
            if (!left && !right) raw_score -= 15; 
        }

        if (pawn_count_black[c] > 1) raw_score += (pawn_count_black[c] - 1) * 20;
        if (pawn_count_black[c] > 0) {
            bool left = (c > 0) && (pawn_count_black[c - 1] > 0);
            bool right = (c < 7) && (pawn_count_black[c + 1] > 0);
            if (!left && !right) raw_score += 15;
        }
    }

    auto eval_rook_connection = [&](const std::vector<std::pair<int, int>>& rooks) {
        if (rooks.size() < 2) return 0;
        int connection_score = 0;
        for (size_t i = 0; i < rooks.size(); ++i) {
            for (size_t j = i + 1; j < rooks.size(); ++j) {
                int r1 = rooks[i].first, c1 = rooks[i].second;
                int r2 = rooks[j].first, c2 = rooks[j].second;
                if (r1 == r2) {
                    bool blocked = false;
                    for (int col = std::min(c1, c2) + 1; col < std::max(c1, c2); ++col)
                        if (board.get_piece(r1, col) != 0) { blocked = true; break; }
                    if (!blocked) connection_score += 25;
                } else if (c1 == c2) {
                    bool blocked = false;
                    for (int row = std::min(r1, r2) + 1; row < std::max(r1, r2); ++row)
                        if (board.get_piece(row, c1) != 0) { blocked = true; break; }
                    if (!blocked) connection_score += 25;
                }
            }
        }
        return connection_score;
    };
    raw_score += eval_rook_connection(white_rooks);
    raw_score -= eval_rook_connection(black_rooks);

    auto eval_shield = [&](int k_x, int k_y, int p_type) {
        if (k_x == -1) return 0;
        int shield_score = 0;
        int forward_row = k_x + (p_type > 0 ? 1 : -1);

        if (forward_row >= 0 && forward_row < 8) {
            for (int dc = -1; dc <= 1; ++dc) {
                int sc = k_y + dc;
                if (sc >= 0 && sc < 8) {
                    if (board.get_piece(forward_row, sc) == p_type) {
                        shield_score += 25; 
                    }
                }
            }
        }
        return shield_score;
    };

    raw_score += eval_shield(white_king_x, white_king_y, 1);
    raw_score -= eval_shield(black_king_x, black_king_y, -1);

    return raw_score * (board.get_current_turn() ? -1 : 1);
}

int Engine::negamax_search(Board& board, int depth, int max_depth, int alpha, int beta,
                           std::tuple<int, int, int, int>* root_move) {
    const int move_remaining = board.get_moves_left();
    if (move_remaining == 2 && depth >= max_depth) return board_eval(board);
    if (board.has_game_ended()) return board_eval(board);

    int best_score = NEGINF;
    bool found_move = false;

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int piece = board.get_piece(r, c);
            if (piece == 0) continue;

            int current_turn = board.get_current_turn();
            if ((current_turn == 0 && piece < 0) || (current_turn == 1 && piece > 0)) continue;

            const auto& moves = (std::abs(piece) == 1) ? board.get_pawn_move_patterns(piece) 
                                                       : board.get_move_patterns(piece);
            for (auto& move : moves) {
                int new_x = r + move.first;
                int new_y = c + move.second;
                if (board.valid_move(r, c, new_x, new_y)) {
                    if (board.get_piece(new_x, new_y) != 0) continue;
                    board.make_move(r, c, new_x, new_y);
                    auto score = negamax_search(board, depth + (move_remaining == 1 ? 1 : 0), max_depth,
                        move_remaining == 2 ? alpha : -beta, move_remaining == 2 ? beta : -alpha) * (move_remaining == 1 ? -1 : 1);
                    if (!found_move || best_score < score) {
                        best_score = score;
                        if (root_move) *root_move = std::make_tuple(r, c, new_x, new_y);
                    }
                    found_move = true;
                    alpha = std::max(alpha, score);
                    board.rollback_move();
                    if (alpha >= beta) return best_score;
                }
            }

            if (move_remaining == 1) continue;
            const auto& captures = (std::abs(piece) == 1) ? board.get_pawn_capture_patterns(piece) 
                                                          : board.get_move_patterns(piece);
            for (auto& move : captures) {
                int new_x = r + move.first;
                int new_y = c + move.second;
                if (board.valid_move(r, c, new_x, new_y)) {
                    if (board.get_piece(new_x, new_y) == 0) continue;
                    board.make_capture(r, c, new_x, new_y);
                    auto score = -negamax_search(board, depth + 1, max_depth, -beta, -alpha);
                    if (!found_move || best_score < score) {
                        best_score = score;
                        if (root_move) *root_move = std::make_tuple(r, c, new_x, new_y);
                    }
                    found_move = true;
                    alpha = std::max(alpha, score);
                    board.rollback_move();
                    if (alpha >= beta) return best_score;
                }
            }
        }
    }

    return best_score;
}