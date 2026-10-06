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
        std::cout << "Passed: Board/Engine ownership, two-engine turns, custom configurations, and state isolation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
