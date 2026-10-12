#include "../board.hpp"
#include "../engine.hpp"
#include "../Engines/NewEvaluator.hpp"

#include <iostream>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct BoardTestAccess {
    static void empty(Board& board, int side_to_move = 0, int moves_left = 2,
                      std::uint8_t rights = 0x0F) {
        for (auto& row : board.current_board)
            std::fill(row.begin(), row.end(), 0);

        board.turn = side_to_move;
        board.move_left = moves_left;
        board.game_status = 0;
        board.castling_rights = rights;
        board.rollback.clear();
        board.last_move = Move{};
        board.refresh_hash();
    }

    static void set_piece(Board& board, int row, int column, int piece) {
        board.current_board[row][column] = piece;
        board.refresh_hash();
    }

    static void set_rights(Board& board, std::uint8_t rights) {
        board.castling_rights = rights;
        board.refresh_hash();
    }

    static Hash recomputed_hash(const Board& board) {
        return board.calculate_hash();
    }

    static std::size_t undo_size(const Board& board) {
        return board.rollback.size();
    }

    static int searched_turns(const Board& board, std::size_t root_history_size) {
        int turns = 0;
        for (std::size_t i = root_history_size; i < board.rollback.size(); ++i) {
            const int next_turn = i + 1 < board.rollback.size()
                ? board.rollback[i + 1].old_turn : board.turn;
            if (board.rollback[i].old_turn != next_turn) ++turns;
        }
        return turns;
    }

    static Move first_search_move(const Board& board) {
        return board.rollback.empty() ? Move{} : board.rollback.front().move;
    }

    static void force_slot_collisions(Board& board) {
        constexpr Hash mask = ~((Hash{1} << 20) - 1);
        for (auto* table : {&board.piece_hash, &board.turn_hash,
                            &board.move_left_hash, &board.castling_rights_hash})
            for (auto& item : *table) item.second &= mask;
        board.refresh_hash();
    }
};

struct EngineTestAccess {
    static void seed_root_lower(Engine& engine, const Board& board, const Move& witness) {
        auto& entry = engine.transposition[board.get_hash() & Engine::kTTMask];
        entry.key = board.get_hash();
        entry.score = 100;
        entry.depth = 2;
        entry.bound = static_cast<std::uint8_t>(Engine::BoundType::Lower);
        entry.best_move = Engine::encode_move(witness);
    }

    static void weaken_root_to_lower_bound(Engine& engine, const Board& board) {
        auto& entry = engine.transposition[board.get_hash() & Engine::kTTMask];
        if (entry.key != board.get_hash()) throw std::runtime_error("root TT key missing");
        entry.score = -100;
        entry.bound = static_cast<std::uint8_t>(Engine::BoundType::Lower);
    }

    static bool root_is_exact_zero(const Engine& engine, const Board& board) {
        const auto& entry = engine.transposition[board.get_hash() & Engine::kTTMask];
        return entry.key == board.get_hash() && entry.score == 0 && entry.depth == 1
            && entry.bound == static_cast<std::uint8_t>(Engine::BoundType::Exact)
            && entry.best_move == Engine::encode_move(engine.get_best_move());
    }

    static Move cached_move(const Engine& engine, const Board& board) {
        const auto& entry = engine.transposition[board.get_hash() & Engine::kTTMask];
        if (entry.key != board.get_hash()) throw std::runtime_error("root TT key missing");
        return Engine::decode_move(entry.best_move);
    }
};

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool play(Board& board, const Move& move) {
    return board.make_move(move);
}

class FixedEvaluator final : public Evaluator {
    int score;

public:
    explicit FixedEvaluator(int score) : score(score) {}

    int evaluate(const Board&) const override { return score; }
};

class SpecialMoveEvaluator final : public Evaluator {
public:
    int evaluate(const Board& board) const override {
        if (board.get_last_move().special_move == SpecialMove::CastleKingside)
            return board.get_current_turn() == 1 ? -100000 : 100000;
        return 0;
    }
};

std::uint8_t all_castling_rights() {
    return static_cast<std::uint8_t>(CastlingRight::WhiteKingside)
        | static_cast<std::uint8_t>(CastlingRight::WhiteQueenside)
        | static_cast<std::uint8_t>(CastlingRight::BlackKingside)
        | static_cast<std::uint8_t>(CastlingRight::BlackQueenside);
}

bool same_board(const Board& left, const Board& right) {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            if (left.get_piece(row, column) != right.get_piece(row, column))
                return false;
        }
    }
    return left.get_current_turn() == right.get_current_turn()
        && left.get_moves_left() == right.get_moves_left()
        && left.get_game_status() == right.get_game_status()
        && left.get_castling_rights() == right.get_castling_rights()
        && left.get_hash() == right.get_hash()
        && left.get_last_move() == right.get_last_move();
}

void require_hash_matches_recomputed(const Board& board, const std::string& context) {
    require(board.get_hash() == BoardTestAccess::recomputed_hash(board),
            context + ": stored hash differs from full recomputation");
}

void require_hash_transition(Board& board, const Move& move,
                             const std::string& context, bool expected_valid = true) {
    const Board before = board;
    const auto undo_size = BoardTestAccess::undo_size(board);
    require_hash_matches_recomputed(board, context + " before move");
    require(board.make_move(move) == expected_valid,
            context + ": move acceptance differed from expected");
    require_hash_matches_recomputed(board, context + " after move");
    require(BoardTestAccess::undo_size(board) == undo_size + (expected_valid ? 1 : 0),
            context + ": unexpected undo history size");
    if (expected_valid) {
        board.rollback_move();
        require_hash_matches_recomputed(board, context + " after rollback");
    }
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == undo_size,
            context + ": move/rollback or rejected move changed the original state");
}

std::vector<Move> require_moves_match_exhaustive_scan(
    Board& board, const std::string& context) {
    require_hash_matches_recomputed(board, context + " before generation");
    const Board before = board;
    const auto generated = board.legal_moves();
    require(same_board(board, before), context + ": move generation changed the board");

    // Keep the old exhaustive traversal as an independent candidate-generation
    // oracle. A pawn reaching the final rank yields one move per promotion piece,
    // in the same value order the generator emits (Queen, Rook, Bishop, Knight).
    Board probe = board;
    std::vector<Move> expected;
    const SpecialMove promotions[] = {
        SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
        SpecialMove::PromoteBishop, SpecialMove::PromoteKnight
    };
    for (int from = 0; from < 64; ++from) {
        for (int to = 0; to < 64; ++to) {
            const int fx = from / 8, fy = from % 8, tx = to / 8, ty = to % 8;
            const Move move{fx, fy, tx, ty};
            if (!probe.valid_move(move.old_x, move.old_y, move.new_x, move.new_y))
                continue;
            const int piece = probe.get_piece(fx, fy);
            const bool pawn_promo = (piece == 1 || piece == -1) && tx == (piece > 0 ? 7 : 0);
            if (pawn_promo) {
                for (const SpecialMove promo : promotions) {
                    Move promo_move = move;
                    promo_move.special_move = promo;
                    require(probe.make_move(promo_move), context + ": validated promotion was rejected");
                    require_hash_matches_recomputed(probe, context + " after generated move");
                    expected.push_back(probe.get_last_move());
                    probe.rollback_move();
                    require_hash_matches_recomputed(probe, context + " after generated rollback");
                }
            } else {
                require(probe.make_move(move), context + ": validated move was rejected");
                require_hash_matches_recomputed(probe, context + " after generated move");
                expected.push_back(probe.get_last_move());
                probe.rollback_move();
                require_hash_matches_recomputed(probe, context + " after generated rollback");
            }
        }
    }
    require(same_board(probe, before), context + ": oracle did not restore the board");
    require(generated == expected,
            context + ": generated moves differ in coordinates, order, or special tags"
                + " (generated " + std::to_string(generated.size())
                + ", expected " + std::to_string(expected.size()) + ")");
    return generated;
}

void test_move_generation_matches_exhaustive_scan() {
    for (int side = 0; side < 2; ++side) {
        for (int actions = 1; actions <= 2; ++actions) {
            Board board;
            const int sign = side == 0 ? 1 : -1;
            const std::string context = "side " + std::to_string(side)
                + ", actions " + std::to_string(actions);
            auto place = [&](int row, int column, int piece) {
                BoardTestAccess::set_piece(
                    board, side == 0 ? row : 7 - row, column, sign * piece);
            };

            for (int piece = 1; piece <= 6; ++piece) {
                BoardTestAccess::empty(board, side, actions, 0);
                place(3, 3, piece);
                place(3, 5, 1);
                place(3, 1, -1);
                place(3, 0, -4); // An enemy piece beyond another enemy is unreachable.
                place(5, 3, 1);
                place(1, 3, -1);
                place(5, 5, 1);
                place(1, 1, -1);
                place(4, 2, -1);
                place(4, 4, 1);
                require_moves_match_exhaustive_scan(
                    board, context + ", centre piece " + std::to_string(piece));

                BoardTestAccess::empty(board, side, actions, 0);
                place(0, 0, piece);
                place(0, 3, 1);
                place(0, 5, -4);
                place(2, 2, -1);
                place(4, 4, -4);
                require_moves_match_exhaustive_scan(
                    board, context + ", corner piece " + std::to_string(piece));
            }

            BoardTestAccess::empty(board, side, actions);
            place(6, 0, 1);
            place(6, 6, 1);
            place(7, 1, -4);
            place(7, 6, 2);
            place(7, 7, -4);
            require_moves_match_exhaustive_scan(board, context + ", promotions");

            BoardTestAccess::empty(board, side, actions);
            place(0, 3, 6);
            place(0, 0, 4);
            place(0, 7, 4);
            require_moves_match_exhaustive_scan(board, context + ", both castles");

            BoardTestAccess::set_rights(board, 0);
            require_moves_match_exhaustive_scan(board, context + ", no castling rights");

            BoardTestAccess::set_rights(board, all_castling_rights());
            place(0, 2, 2);
            place(0, 4, -2);
            require_moves_match_exhaustive_scan(board, context + ", blocked castles");

            place(0, 2, 0);
            place(0, 4, 0);
            place(0, 0, 0);
            require_moves_match_exhaustive_scan(board, context + ", missing rook");
        }

        Board terminal;
        const int sign = side == 0 ? 1 : -1;
        BoardTestAccess::empty(terminal, side);
        BoardTestAccess::set_piece(terminal, 3, 3, sign * 4);
        BoardTestAccess::set_piece(terminal, 3, 5, -sign * 6);
        require(terminal.make_move(3, 3, 3, 5), "terminal fixture king capture failed");
        require(terminal.has_game_ended(), "king capture did not end the fixture");
        require(require_moves_match_exhaustive_scan(terminal, "terminal position").empty(),
                "terminal position generated a move");
    }
}

void test_move_generation_on_reachable_positions() {
    Board board;
    const Board initial = board;
    std::vector<Hash> previous_hashes;
    std::uint32_t selection = 0x3180;
    for (int ply = 0; ply < 64; ++ply) {
        const auto moves = require_moves_match_exhaustive_scan(
            board, "reachable position " + std::to_string(ply));
        if (moves.empty()) break;
        selection = selection * 1664525u + 1013904223u;
        previous_hashes.push_back(board.get_hash());
        require(board.make_move(moves[selection % moves.size()]),
                "generated move was rejected during deterministic play");
        require_hash_matches_recomputed(board, "deterministic play");
    }
    require_moves_match_exhaustive_scan(board, "final reachable position");
    while (!previous_hashes.empty()) {
        const Hash expected = previous_hashes.back();
        previous_hashes.pop_back();
        board.rollback_move();
        require(board.get_hash() == expected, "sequence rollback restored the wrong hash");
        require_hash_matches_recomputed(board, "sequence rollback");
    }
    require(same_board(board, initial), "sequence rollback did not restore the initial state");
}

void test_hash_consistency_for_move_types() {
    for (int side = 0; side < 2; ++side) {
        const int sign = side == 0 ? 1 : -1;
        const int home_row = side == 0 ? 0 : 7;
        const int other_home_row = 7 - home_row;
        const int promotion_row = other_home_row - sign;
        for (int actions = 1; actions <= 2; ++actions) {
            Board board;
            const std::string context = "hash side " + std::to_string(side)
                + ", actions " + std::to_string(actions);
            BoardTestAccess::empty(board, side, actions);
            BoardTestAccess::set_piece(board, 3, 3, sign * 2);
            BoardTestAccess::set_piece(board, 5, 4, -sign);
            require_hash_transition(board, {3, 3, 5, 2}, context + ", quiet knight");
            require_hash_transition(board, {3, 3, 5, 4}, context + ", knight capture",
                                    actions == 2);
            require_hash_transition(board, {-1, 3, 5, 2}, context + ", invalid source", false);
            require_hash_transition(board, {3, 3, 5, 2, SpecialMove::PromoteQueen},
                                    context + ", invalid special tag", false);

            BoardTestAccess::empty(board, side, actions);
            BoardTestAccess::set_piece(board, home_row, 3, sign * 6);
            for (const int column : {0, 7}) {
                BoardTestAccess::set_piece(board, home_row, column, sign * 4);
                BoardTestAccess::set_piece(board, other_home_row, column, -sign * 4);
            }
            require_hash_transition(board, {home_row, 3, home_row, 1,
                                            SpecialMove::CastleQueenside},
                                    context + ", queenside castle");
            require_hash_transition(board, {home_row, 3, home_row, 5,
                                            SpecialMove::CastleKingside},
                                    context + ", kingside castle");
            require_hash_transition(board, {home_row, 3, home_row + sign, 3},
                                    context + ", king loses both castling rights");
            for (const int column : {0, 7}) {
                require_hash_transition(board, {home_row, column, home_row + sign, column},
                                        context + ", rook loses castling right");
                require_hash_transition(board, {home_row, column, other_home_row, column},
                                        context + ", both rooks lose castling rights",
                                        actions == 2);
            }

            BoardTestAccess::empty(board, side, actions);
            BoardTestAccess::set_piece(board, promotion_row, 6, sign);
            require_hash_transition(board, {promotion_row, 6, other_home_row, 6,
                                            SpecialMove::PromoteQueen},
                                    context + ", quiet promotion");
            for (const int captured_piece : {4, 6}) {
                BoardTestAccess::set_piece(board, other_home_row, 7, -sign * captured_piece);
                require_hash_transition(board, {promotion_row, 6, other_home_row, 7,
                                                SpecialMove::PromoteQueen},
                                        context + ", promotion captures "
                                            + std::to_string(captured_piece), actions == 2);
            }
        }
    }
}

void setup_castling_board(Board& board) {
    BoardTestAccess::empty(board);
    BoardTestAccess::set_piece(board, 0, 3, 6);
    BoardTestAccess::set_piece(board, 0, 0, 4);
    BoardTestAccess::set_piece(board, 0, 7, 4);
}

void test_castling_and_rights() {
    {
        Board board;
        setup_castling_board(board);
        Board before = board;

        require(board.valid_move(0, 3, 0, 5),
                "kingside castling should be valid on a clear board");
        require(board.make_move({0, 3, 0, 5, SpecialMove::CastleKingside}),
                "kingside castling move was rejected");
        require(board.get_piece(0, 5) == 6 && board.get_piece(0, 4) == 4
                    && board.get_piece(0, 3) == 0 && board.get_piece(0, 7) == 0,
                "kingside castling did not move king and rook correctly");
        require(board.get_last_move().special_move == SpecialMove::CastleKingside,
                "kingside castling metadata was not recorded");
        require((board.get_castling_rights()
                 & (static_cast<std::uint8_t>(CastlingRight::WhiteKingside)
                    | static_cast<std::uint8_t>(CastlingRight::WhiteQueenside))) == 0,
                "king movement did not clear white castling rights");

        board.rollback_move();
        require(same_board(board, before),
                "rollback did not restore a kingside castle completely");
    }

    {
        Board board;
        setup_castling_board(board);

        require(board.valid_move(0, 3, 0, 1),
                "queenside castling should be valid on a clear board");
        require(board.make_move({0, 3, 0, 1, SpecialMove::CastleQueenside}),
                "queenside castling move was rejected");
        require(board.get_piece(0, 1) == 6 && board.get_piece(0, 2) == 4
                    && board.get_piece(0, 3) == 0 && board.get_piece(0, 0) == 0,
                "queenside castling did not move king and rook correctly");
    }

    {
        Board board;
        setup_castling_board(board);
        BoardTestAccess::set_piece(board, 0, 4, 2);
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail when its path is occupied");
    }

    {
        Board board;
        setup_castling_board(board);
        require(board.make_move(0, 3, 1, 3), "king move for rights test failed");
        require(board.make_move(1, 3, 0, 3), "king return for rights test failed");
        BoardTestAccess::empty(board, 0, 2, board.get_castling_rights());
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail after the king moved and returned");
    }

    {
        Board board;
        setup_castling_board(board);
        require(board.make_move(0, 7, 1, 7), "rook move for rights test failed");
        require(board.make_move(1, 7, 0, 7), "rook return for rights test failed");
        BoardTestAccess::empty(board, 0, 2, board.get_castling_rights());
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail after the rook moved and returned");
    }

    {
        Board board;
        BoardTestAccess::empty(board, 1);
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        BoardTestAccess::set_piece(board, 1, 7, -4);
        require(board.make_move(1, 7, 0, 7),
                "black rook should be able to capture the original white rook");
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail after the original rook was captured");
    }

    {
        Board board;
        setup_castling_board(board);
        const Hash with_rights = board.get_hash();
        BoardTestAccess::set_rights(
            board,
            static_cast<std::uint8_t>(all_castling_rights()
                & ~static_cast<std::uint8_t>(CastlingRight::WhiteKingside)));
        require(board.get_hash() != with_rights,
                "castling rights should affect the position hash");
    }
}

void test_promotion_and_metadata() {
    {
        Board board;
        BoardTestAccess::empty(board);
        BoardTestAccess::set_piece(board, 6, 0, 1);
        require(board.make_move({6, 0, 7, 0, SpecialMove::PromoteQueen}),
                "quiet promotion was rejected");
        require(board.get_piece(7, 0) == 5
                    && board.get_last_move().special_move == SpecialMove::PromoteQueen,
                "quiet promotion did not produce a tagged queen");
        board.rollback_move();
        require(board.get_piece(6, 0) == 1 && board.get_piece(7, 0) == 0,
                "quiet promotion rollback did not restore the pawn");
    }

    {
        Board board;
        BoardTestAccess::empty(board);
        BoardTestAccess::set_piece(board, 6, 0, 1);
        BoardTestAccess::set_piece(board, 7, 1, -4);
        Board before = board;

        require(board.make_move({6, 0, 7, 1, SpecialMove::PromoteQueen}),
                "capture-promotion was rejected");
        require(board.get_piece(7, 1) == 5 && board.get_last_move().special_move
                    == SpecialMove::PromoteQueen,
                "capture-promotion did not produce a queen");
        require(board.get_piece(6, 0) == 0,
                "capture-promotion left the original pawn behind");
        board.rollback_move();
        require(same_board(board, before),
                "capture-promotion rollback did not restore pawn and capture");
    }

    {
        Board board;
        BoardTestAccess::empty(board);
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 0, 4);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        BoardTestAccess::set_piece(board, 6, 6, 1);

        const auto moves = board.legal_moves();
        const auto castle = std::find_if(moves.begin(), moves.end(), [](const Move& move) {
            return move.old_x == 0 && move.old_y == 3
                && move.new_x == 0 && move.new_y == 5;
        });
        const auto promotion = std::find_if(moves.begin(), moves.end(), [](const Move& move) {
            return move.old_x == 6 && move.old_y == 6
                && move.new_x == 7 && move.new_y == 6;
        });

        require(castle != moves.end()
                    && castle->special_move == SpecialMove::CastleKingside,
                "legal_moves did not tag kingside castling");
        require(promotion != moves.end()
                    && promotion->special_move == SpecialMove::PromoteQueen,
                "legal_moves did not tag promotion");
    }
}

void test_engine_searches_special_moves_without_mutating_root() {
    Board board;
    BoardTestAccess::empty(board, 0, 1);
    BoardTestAccess::set_piece(board, 0, 3, 6);
    BoardTestAccess::set_piece(board, 0, 0, 4);
    BoardTestAccess::set_piece(board, 0, 7, 4);
    BoardTestAccess::set_piece(board, 7, 3, -6);
    BoardTestAccess::set_rights(board, all_castling_rights());
    Board before = board;

    Engine engine(0, 1, std::make_unique<SpecialMoveEvaluator>());
    require(engine.find_best_move(board), "engine found no move on castle position");
    require(engine.get_best_move().special_move == SpecialMove::CastleKingside,
            "engine did not preserve/select the tagged castling move");
    require(same_board(board, before),
            "engine search mutated the authoritative board on a special move");
}

void test_board_owns_rules_only() {
    Board board;
    require(board.get_current_turn() == 0, "White should move first");
    require(board.get_moves_left() == 1, "White should have one opening action");
    require(board.get_piece(1, 0) == 1, "initial white pawn is missing");
    require(!board.has_game_ended(), "initial board is terminal");

    require(board.make_move(1, 0, 2, 0), "board rejected a legal quiet move");
    require(board.get_moves_left() == 2 && board.get_current_turn() == 1,
            "a completed opening action should hand control to Black");
}

void test_engines_take_turns_on_one_board() {
    Board board;
    Engine white(0, 1);
    Engine black(1, 1);

    require(white.is_turn(board), "White engine should start");
    require(!black.is_turn(board), "Black engine moved before its turn");
    require(!black.find_best_move(board), "Black engine searched out of turn");
    require(white.find_best_move(board), "White engine found no opening move");
    require(play(board, white.get_best_move()), "White engine move was rejected");
    require(!white.is_turn(board) && black.is_turn(board),
            "the same board should pass control to Black");
    require(black.find_best_move(board), "Black engine found no move");
    require(play(board, black.get_best_move()), "Black engine move was rejected");
}

void test_engine_configuration_is_independent() {
    EngineConfig aggressive = EngineConfig::standard();
    EngineConfig defensive = EngineConfig::standard();
    aggressive.connected_rook_bonus = 100;
    defensive.connected_rook_bonus = 5;

    Engine white(0, 1, aggressive);
    Engine black(1, 1, defensive);
    require(white.get_config().connected_rook_bonus == 100,
            "White engine lost its custom evaluation configuration");
    require(black.get_config().connected_rook_bonus == 5,
            "Black engine lost its custom evaluation configuration");
    require(white.get_config().connected_rook_bonus !=
                black.get_config().connected_rook_bonus,
            "engines unexpectedly share evaluation configuration");
}

void test_custom_evaluator_is_used() {
    Board board;
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(1234));

    require(engine.evaluate(board) == 1234,
            "Engine did not delegate evaluation to its custom Evaluator");
    require(dynamic_cast<const FixedEvaluator*>(&engine.get_evaluator()) != nullptr,
            "Engine did not retain its custom Evaluator");
}

void test_new_evaluator_is_injectable() {
    Board board;
    Engine engine(0, 1, std::make_unique<NewEvaluator>());

    require(dynamic_cast<const NewEvaluator*>(&engine.get_evaluator()) != nullptr,
            "NewEvaluator was not retained by the engine");
    require(engine.evaluate(board) == engine.get_evaluator().evaluate(board),
            "Engine did not delegate evaluation to NewEvaluator");
}

void test_search_does_not_mutate_position() {
    Board board;
    Engine engine(0, 2);
    Hash before_hash = board.get_hash();
    int before_turn = board.get_current_turn();
    int before_actions = board.get_moves_left();

    require(engine.find_best_move(board), "search found no legal move");
    require(board.get_hash() == before_hash &&
                board.get_current_turn() == before_turn &&
                board.get_moves_left() == before_actions,
            "engine search mutated the authoritative board");
}

void test_move_rollback_restores_hash() {
    Board board;
    const Hash before = board.get_hash();
    require(board.make_move(1, 0, 2, 0), "quiet move for rollback test was rejected");
    require(board.get_hash() != before, "a move should change the position hash");
    board.rollback_move();
    require(board.get_hash() == before && board.get_current_turn() == 0 &&
                board.get_moves_left() == 1,
            "rolling back a quiet move should restore the complete position");
}

void test_independent_boards_share_position_hashes() {
    Board first;
    Board second;
    require(first.get_hash() == second.get_hash(),
            "independent initial boards must have identical position hashes");
    const Hash initial_hash = first.get_hash();
    std::uint32_t selection = 0x3180;
    int played = 0;
    for (; played < 40; ++played) {
        const auto moves = first.legal_moves();
        if (moves.empty()) break;
        selection = selection * 1664525u + 1013904223u;
        const Move move = moves[selection % moves.size()];
        require(first.make_move(move) && second.make_move(move),
                "independent boards rejected the same legal move");
        require(first.get_hash() == second.get_hash(),
                "identical move sequences must produce identical hashes");
        require_hash_matches_recomputed(first, "fixed hash first board");
        require_hash_matches_recomputed(second, "fixed hash second board");
    }
    for (int i = 0; i < played; ++i) {
        first.rollback_move();
        second.rollback_move();
        require(first.get_hash() == second.get_hash(),
                "independent rollback must preserve matching hashes");
    }
    require(first.get_hash() == initial_hash && second.get_hash() == initial_hash,
            "independent boards must restore the fixed initial hash");
}

void test_tt_persistence_across_undo_and_new_game() {
    Board board;
    const Board initial_board = board;
    Engine engine(0, 2);

    require(engine.find_best_move(board, 2), "engine found no opening move");
    const Move first_best = engine.get_best_move();
    const auto cold_nodes = engine.get_search_nodes();

    require(board.make_move(first_best), "engine opening move was rejected");
    require(board.make_move(6, 3, 5, 3), "black quiet move 1 was rejected");
    require(board.make_move(6, 4, 5, 4), "black quiet move 2 was rejected");
    const Board after_black = board;
    require(engine.find_best_move(after_black, 2), "engine found no second move");

    require(engine.find_best_move(initial_board, 2),
            "re-search of an undone position returned no move (warm-TT root bug)");
    require(engine.get_best_move() == first_best,
            "undo re-search must reproduce the cached root best move");

    Board fresh_game;
    require(EngineTestAccess::cached_move(engine, fresh_game) == first_best,
            "an independent initial board must match the cached root key");
    require(engine.find_best_move(fresh_game, 2),
            "engine found no move when reusing the TT in a new game");
    require(engine.get_best_move() == first_best && engine.get_search_nodes() < cold_nodes,
            "new-game search must retain and reuse the cached result");

}

void test_search_completes_shallow_iterations_before_final_depth() {
    class DepthRecordingEvaluator final : public Evaluator {
        std::vector<int>& depths;
    public:
        explicit DepthRecordingEvaluator(std::vector<int>& depths) : depths(depths) {}
        int evaluate(const Board& board) const override {
            depths.push_back(BoardTestAccess::searched_turns(board, 0));
            return 0;
        }
    };

    Board board;
    const Board before = board;
    std::vector<int> depths;
    Engine engine(0, 2, std::make_unique<DepthRecordingEvaluator>(depths));
    require(engine.find_best_move(board), "iterative search found no opening move");
    require(std::find(depths.begin(), depths.end(), 1) != depths.end(),
            "search skipped the depth-1 iteration before depth 2");
    require(std::find(depths.begin(), depths.end(), 2) != depths.end(),
            "search reused shallow scores instead of reaching depth 2");
    require(same_board(board, before), "iterative search mutated the root");
}

void test_shallow_tt_move_orders_deeper_search_without_reusing_its_score() {
    const Move shallow_best{1, 0, 2, 0};
    const Move deep_best{1, 1, 2, 1};
    class HorizonEvaluator final : public Evaluator {
        Move shallow_best;
        Move deep_best;
        Move& first_deep_move;
    public:
        HorizonEvaluator(Move shallow, Move deep, Move& first_deep)
            : shallow_best(shallow), deep_best(deep), first_deep_move(first_deep) {}
        int evaluate(const Board& board) const override {
            const int depth = BoardTestAccess::searched_turns(board, 0);
            const Move first = BoardTestAccess::first_search_move(board);
            if (depth == 2 && first_deep_move.old_x == -1) first_deep_move = first;
            int white_score = first == shallow_best ? 100 : (first == deep_best ? -100 : 0);
            if (depth >= 2) white_score = -white_score;
            return board.get_current_turn() == 0 ? white_score : -white_score;
        }
    };

    Board board;
    Move first_deep_move{};
    Engine engine(0, 2, std::make_unique<HorizonEvaluator>(
        shallow_best, deep_best, first_deep_move));
    require(engine.find_best_move(board, 1), "shallow search found no move");
    require(engine.get_best_move() == shallow_best, "shallow fixture chose the wrong move");
    require(engine.find_best_move(board, 2), "deeper search found no move");
    require(first_deep_move == shallow_best,
            "depth-1 TT best move was not searched first at depth 2");
    require(engine.get_best_move() == deep_best,
            "depth-2 search reused the shallow score instead of finding the new best move");
    require(engine.find_best_move(board, 2) && engine.get_best_move() == deep_best,
            "warm iterative re-search lost the final-depth best move");
}

int exhaustive_score(Board& board, const Evaluator& evaluator, int depth, int target) {
    if (board.has_game_ended() || (board.get_moves_left() == 2 && depth >= target))
        return evaluator.evaluate(board);
    const int turn = board.get_current_turn();
    int best = NEGINF;
    for (const Move& move : board.legal_moves()) {
        require(board.make_move(move), "oracle rejected a generated move");
        const bool changed = board.get_current_turn() != turn;
        const int score = exhaustive_score(board, evaluator, depth + (changed ? 1 : 0), target);
        best = std::max(best, changed ? -score : score);
        board.rollback_move();
    }
    return best == NEGINF ? evaluator.evaluate(board) : best;
}

void test_iterative_search_matches_exhaustive_final_depth() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            Board board;
            BoardTestAccess::empty(board, side, actions, 0);
            BoardTestAccess::set_piece(board, 1, 0, 6);
            BoardTestAccess::set_piece(board, 6, 7, -6);
            BoardTestAccess::set_piece(board, 3, 3, 1);
            BoardTestAccess::set_piece(board, 4, 4, -1);
            const Board before = board;
            const auto history_size = BoardTestAccess::undo_size(board);
            Engine engine(side, 3);
            for (int target : {1, 2, 3, 3}) {
                Board oracle = board;
                const int expected = exhaustive_score(oracle, engine.get_evaluator(), 0, target);
                require(engine.find_best_move(board, target), "iterative oracle fixture found no move");
                Board selected = board;
                require(selected.make_move(engine.get_best_move()), "iterative search returned an illegal move");
                const bool changed = selected.get_current_turn() != side;
                const int child = exhaustive_score(selected, engine.get_evaluator(), changed ? 1 : 0, target);
                require((changed ? -child : child) == expected,
                        "iterative best move differs from exhaustive minimax at final depth");
                require(same_board(board, before) && BoardTestAccess::undo_size(board) == history_size,
                        "iterative search altered root state or undo history");
            }
        }
    }
}

void test_default_depth_is_three() {
    Engine engine(0);
    require(engine.get_search_depth() == 3,
            "default depth should be 3 after GUI-workload benchmarking");
}

void test_equal_depth_result_replaces_cached_lower_bound() {
    Board board;
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    require(engine.find_best_move(board, 1), "first equal-depth search found no move");
    EngineTestAccess::weaken_root_to_lower_bound(engine, board);
    require(engine.find_best_move(board, 1), "equal-depth re-search found no move");
    require(EngineTestAccess::root_is_exact_zero(engine, board),
            "equal-depth result must replace the cached lower bound with its exact result");
}

void test_retained_lower_bound_keeps_its_move_witness() {
    class BoundWitnessEvaluator final : public Evaluator {
    public:
        int evaluate(const Board& board) const override {
            const bool move_a = board.get_piece(2, 0) == 1;
            const bool move_b = board.get_piece(2, 1) == 1;
            if (board.get_current_turn() == 1)
                return -(move_a ? 200 : (move_b ? 100 : -200));
            return move_a ? (board.get_piece(6, 1) == -1 ? 100 : 0)
                          : (move_b ? 100 : -200);
        }
    };

    Board board;
    const Move witness{1, 1, 2, 1};
    Engine engine(0, 2, std::make_unique<BoundWitnessEvaluator>());
    Board oracle = board;
    require(exhaustive_score(oracle, engine.get_evaluator(), 0, 2) == 100,
            "retained lower-bound fixture has an invalid score");
    EngineTestAccess::seed_root_lower(engine, board, witness);
    require(engine.find_best_move(board, 2), "bound-witness search found no move");
    require(engine.get_best_move() == witness,
            "shallow iteration corrupted the deeper lower-bound move witness");
}

void test_colliding_children_cannot_corrupt_the_root_tt_move() {
    Board board;
    BoardTestAccess::force_slot_collisions(board);
    Engine engine(0, 2, std::make_unique<FixedEvaluator>(0));
    require(engine.find_best_move(board, 2), "forced-collision search found no move");
    require(EngineTestAccess::cached_move(engine, board) == engine.get_best_move(),
            "recursive slot collision stored another position's move under the root key");
}

} // namespace

int main() {
    try {
        test_board_owns_rules_only();
        test_engines_take_turns_on_one_board();
        test_engine_configuration_is_independent();
        test_custom_evaluator_is_used();
        test_new_evaluator_is_injectable();
        test_search_does_not_mutate_position();
        test_move_rollback_restores_hash();
        test_castling_and_rights();
        test_promotion_and_metadata();
        test_move_generation_matches_exhaustive_scan();
        test_move_generation_on_reachable_positions();
        test_hash_consistency_for_move_types();
        test_engine_searches_special_moves_without_mutating_root();
        test_independent_boards_share_position_hashes();
        test_tt_persistence_across_undo_and_new_game();
        test_search_completes_shallow_iterations_before_final_depth();
        test_shallow_tt_move_orders_deeper_search_without_reusing_its_score();
        test_iterative_search_matches_exhaustive_final_depth();
        test_default_depth_is_three();
        test_equal_depth_result_replaces_cached_lower_bound();
        test_retained_lower_bound_keeps_its_move_witness();
        test_colliding_children_cannot_corrupt_the_root_tt_move();
        std::cout << "Passed: Board/Engine ownership, two-engine turns, custom configurations, and state isolation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
