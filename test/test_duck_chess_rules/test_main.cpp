#include <cstdint>
#include <cstdio>
#include <cstring>

#include "litch_max/chess_notation.hpp"
#include "litch_max/duck_chess_rules.hpp"

namespace {

int g_failures = 0;

void Check(const bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::printf("[FAIL] %s\n", message);
    ++g_failures;
}

uint8_t Square(const char* algebraic) {
    uint8_t square = litch_max::DUCK_CHESS_INVALID_SQUARE;
    Check(litch_max::TryParseUciSquare(algebraic, square), "test square parses");
    return square;
}

bool Contains(const uint64_t mask, const char* algebraic) {
    return (mask & (1ULL << Square(algebraic))) != 0ULL;
}

uint64_t Destinations(const char* fen,
                      const char* duck,
                      const char* from,
                      const char* context) {
    uint64_t mask = 0ULL;
    Check(litch_max::GetDuckLegalDestinationsMask(
              fen, Square(duck), Square(from), mask),
          context);
    return mask;
}

void CheckFen(const litch_max::DuckChessMoveResult& result,
              const char* expected,
              const char* context) {
    Check(std::strcmp(result.resultingFen.CStr(), expected) == 0, context);
}

void TestInputValidation() {
    static constexpr char FEN[] =
        "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1";
    uint64_t mask = ~0ULL;
    Check(!litch_max::GetDuckLegalDestinationsMask(nullptr, Square("a4"),
                                                 Square("e2"), mask),
          "null FEN rejected");
    Check(mask == 0ULL, "failed mask query clears output");
    Check(!litch_max::GetDuckLegalDestinationsMask("invalid", Square("a4"),
                                                 Square("e2"), mask),
          "malformed FEN rejected");
    Check(!litch_max::GetDuckLegalDestinationsMask(
              "4k3/8/8/44/8/8/8/4K3 w - - 0 1", Square("a4"), Square("e1"), mask),
          "adjacent empty-run digits are malformed FEN");
    Check(mask == 0ULL, "malformed placement leaves an empty destinations mask");
    Check(!litch_max::GetDuckLegalDestinationsMask(FEN, 64U,
                                                 Square("e2"), mask),
          "out-of-range duck rejected");
    Check(!litch_max::GetDuckLegalDestinationsMask(FEN, Square("e2"),
                                                 Square("e2"), mask),
          "duck cannot overlap a chess piece");
    Check(litch_max::GetDuckLegalDestinationsMask(FEN, Square("a4"),
                                                Square("e8"), mask),
          "wrong-side origin is a valid empty query");
    Check(mask == 0ULL, "wrong-side origin has no destinations");

    litch_max::DuckChessMoveResult result;
    Check(!litch_max::TryApplyDuckMove(FEN, Square("a4"), "e2e5", result),
          "illegal pawn move rejected");
    Check(!litch_max::TryApplyDuckMove(FEN, Square("a4"), "e2e3n", result),
          "promotion suffix on non-promotion rejected");
    Check(!litch_max::TryApplyDuckMove(FEN, Square("a4"), "e2e3junk", result),
          "long UCI rejected");
}

void TestSlidingBlocker() {
    static constexpr char ROOK_FEN[] =
        "4k3/8/8/8/R7/8/8/4K3 w - - 0 1";
    const uint64_t rook = Destinations(ROOK_FEN, "d4", "a4",
                                       "rook blocker query succeeds");
    Check(Contains(rook, "b4") && Contains(rook, "c4"),
          "rook reaches squares before duck");
    Check(!Contains(rook, "d4") && !Contains(rook, "e4"),
          "rook cannot land on or cross duck");
    Check(Contains(rook, "a8") && Contains(rook, "a1"),
          "duck only blocks its ray");

    static constexpr char BISHOP_FEN[] =
        "4k3/8/8/8/8/2B5/8/4K3 w - - 0 1";
    const uint64_t bishop = Destinations(BISHOP_FEN, "e5", "c3",
                                         "bishop blocker query succeeds");
    Check(Contains(bishop, "d4"), "bishop reaches square before duck");
    Check(!Contains(bishop, "e5") && !Contains(bishop, "f6"),
          "bishop cannot land on or cross duck");
}

void TestKnightAndPawnMoves() {
    static constexpr char KNIGHT_FEN[] =
        "4k3/8/8/8/8/2N5/8/4K3 w - - 0 1";
    const uint64_t knight = Destinations(KNIGHT_FEN, "d5", "c3",
                                         "knight query succeeds");
    Check(!Contains(knight, "d5"), "knight cannot land on duck");
    Check(Contains(knight, "e4") && Contains(knight, "b5"),
          "knight destinations otherwise remain pseudo-legal");

    static constexpr char PAWN_FEN[] =
        "4k3/8/8/8/8/5p2/4P3/4K3 w - - 0 1";
    const uint64_t pawn = Destinations(PAWN_FEN, "e4", "e2",
                                       "pawn query succeeds");
    Check(Contains(pawn, "e3"), "pawn single push remains legal");
    Check(!Contains(pawn, "e4"), "duck blocks pawn double destination");
    Check(Contains(pawn, "f3") && !Contains(pawn, "d3"),
          "pawn captures chess pieces but not empty diagonals");

    const uint64_t blockedPawn = Destinations(PAWN_FEN, "e3", "e2",
                                              "blocked pawn query succeeds");
    Check(!Contains(blockedPawn, "e3") && !Contains(blockedPawn, "e4"),
          "duck blocks both pawn advances when on first step");
}

void TestKingSafetyIsIgnored() {
    static constexpr char PINNED_FEN[] =
        "4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1";
    const uint64_t pinned = Destinations(PINNED_FEN, "a4", "e2",
                                         "pinned rook query succeeds");
    Check(Contains(pinned, "f2"), "pinned piece may expose its king");

    static constexpr char ADJACENT_FEN[] =
        "8/8/8/8/8/4k3/8/4K3 w - - 0 1";
    const uint64_t king = Destinations(ADJACENT_FEN, "a4", "e1",
                                       "king query succeeds");
    Check(Contains(king, "e2"), "king may move adjacent to enemy king");
}

void TestCastling() {
    static constexpr char FEN[] =
        "r3k2r/8/8/8/8/8/4r3/R3K2R w KQkq - 3 7";
    const uint64_t clear = Destinations(FEN, "a5", "e1",
                                        "castling query succeeds");
    Check(Contains(clear, "g1") && Contains(clear, "c1"),
          "castling ignores check and attacked transit squares");

    const uint64_t kingBlocked = Destinations(FEN, "f1", "e1",
                                              "king-side blocker query succeeds");
    Check(!Contains(kingBlocked, "g1") && Contains(kingBlocked, "c1"),
          "duck blocks king-side castling path");
    const uint64_t queenBlocked = Destinations(FEN, "b1", "e1",
                                               "queen-side blocker query succeeds");
    Check(!Contains(queenBlocked, "c1") && Contains(queenBlocked, "g1"),
          "duck blocks queen-side rook path");

    litch_max::DuckChessMoveResult result;
    Check(litch_max::TryApplyDuckMove(FEN, Square("a5"), "e1g1", result),
          "castling applies");
    CheckFen(result,
             "r3k2r/8/8/8/8/8/4r3/R4RK1 b kq - 4 7",
             "castling moves rook and updates rights and clocks");
}

void TestEnPassant() {
    static constexpr char FEN[] =
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 17 9";
    const uint64_t moves = Destinations(FEN, "a3", "e5",
                                        "en-passant query succeeds");
    Check(Contains(moves, "d6"), "en-passant destination generated");
    const uint64_t blocked = Destinations(FEN, "d6", "e5",
                                          "blocked en-passant query succeeds");
    Check(!Contains(blocked, "d6"), "duck blocks en-passant destination");

    litch_max::DuckChessMoveResult result;
    Check(litch_max::TryApplyDuckMove(FEN, Square("a3"), "e5d6", result),
          "en-passant applies");
    Check(result.capturedPieceValid && result.capturedPieceFen == 'p',
          "en-passant reports captured pawn");
    Check(result.captureSquareValid && result.captureSquare == Square("d5"),
          "en-passant reports off-destination capture square");
    Check(!result.capturedKing, "en-passant is not a king capture");
    CheckFen(result, "4k3/8/3P4/8/8/8/8/4K3 b - - 0 9",
             "en-passant removes pawn and clears target");
}

void TestPromotionAndKingCapture() {
    static constexpr char PROMOTION_FEN[] =
        "4k3/P7/8/8/8/8/8/4K3 w - - 7 12";
    litch_max::DuckChessMoveResult result;
    Check(litch_max::TryApplyDuckMove(PROMOTION_FEN, Square("h4"),
                                    "a7a8n", result),
          "underpromotion applies");
    CheckFen(result, "N3k3/8/8/8/8/8/8/4K3 b - - 0 12",
             "underpromotion writes selected piece");

    Check(litch_max::TryApplyDuckMove(PROMOTION_FEN, Square("h4"),
                                    "a7a8", result),
          "promotion without suffix applies");
    CheckFen(result, "Q3k3/8/8/8/8/8/8/4K3 b - - 0 12",
             "promotion without suffix defaults to queen");

    static constexpr char CAPTURE_FEN[] =
        "4k3/8/8/8/8/8/4Q3/4K3 w kq - 5 8";
    Check(litch_max::TryApplyDuckMove(CAPTURE_FEN, Square("a4"),
                                    "e2e8", result),
          "king capture applies");
    Check(result.capturedPieceValid && result.capturedPieceFen == 'k',
          "king capture reports captured piece");
    Check(result.captureSquareValid && result.captureSquare == Square("e8"),
          "king capture reports destination square");
    Check(result.capturedKing, "king capture raises terminal flag");
    CheckFen(result, "4Q3/8/8/8/8/8/8/4K3 b - - 0 8",
             "king capture removes king and its castling rights");
}

void TestCaptureAndCastlingRightsMetadata() {
    static constexpr char ROOK_MOVE_FEN[] =
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 4 20";
    litch_max::DuckChessMoveResult result;
    Check(litch_max::TryApplyDuckMove(ROOK_MOVE_FEN, Square("b5"),
                                    "h1h2", result),
          "home rook move applies");
    CheckFen(result,
             "r3k2r/8/8/8/8/8/7R/R3K3 b Qkq - 5 20",
             "home rook move clears only its castling right");

    static constexpr char ROOK_CAPTURE_FEN[] =
        "r3k2r/8/8/8/8/8/7R/R3K3 w KQkq - 4 20";
    Check(litch_max::TryApplyDuckMove(ROOK_CAPTURE_FEN, Square("b5"),
                                    "h2h8", result),
          "normal rook capture applies");
    Check(result.capturedPieceValid && result.capturedPieceFen == 'r',
          "normal capture reports captured piece");
    Check(result.captureSquareValid && result.captureSquare == Square("h8"),
          "normal capture reports destination capture square");
    CheckFen(result,
             "r3k2R/8/8/8/8/8/8/R3K3 b KQq - 0 20",
             "corner rook capture clears captured side castling right");

    static constexpr char NO_RIGHTS_FEN[] =
        "r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1";
    const uint64_t noRights = Destinations(NO_RIGHTS_FEN, "a5", "e1",
                                           "no-rights king query succeeds");
    Check(!Contains(noRights, "g1") && !Contains(noRights, "c1"),
          "castling requires FEN rights");

    static constexpr char NO_ROOK_FEN[] =
        "r3k2r/8/8/8/8/8/8/R3K3 w KQkq - 0 1";
    const uint64_t noRook = Destinations(NO_ROOK_FEN, "a5", "e1",
                                         "missing-rook king query succeeds");
    Check(!Contains(noRook, "g1") && Contains(noRook, "c1"),
          "castling requires the corresponding rook");
}

void TestMetadataAndFowling() {
    static constexpr char FEN[] =
        "4k3/3p4/8/8/8/8/4P3/4K3 w - - 11 4";
    litch_max::DuckChessMoveResult white;
    Check(litch_max::TryApplyDuckMove(FEN, Square("a5"), "e2e4", white),
          "white double push applies");
    CheckFen(white,
             "4k3/3p4/8/8/4P3/8/8/4K3 b - e3 0 4",
             "white double push sets en-passant and preserves fullmove");

    litch_max::DuckChessMoveResult black;
    Check(litch_max::TryApplyDuckMove(white.resultingFen.CStr(), Square("a5"),
                                    "d7d5", black),
          "black double push applies");
    CheckFen(black,
             "4k3/8/8/3p4/4P3/8/8/4K3 w - d6 0 5",
             "black move increments fullmove and sets en-passant");

    static constexpr char FOWLED_FEN[] =
        "1P5k/1P6/1P6/1P6/1P6/1P6/PP6/KP6 w - - 0 1";
    Check(!litch_max::HasAnyDuckChessMove(FOWLED_FEN, Square("a3")),
          "side with every pseudo-move blocked is fowled");
    Check(litch_max::HasAnyDuckChessMove(FOWLED_FEN, Square("c3")),
          "moving duck off a3 restores a pawn move");
    Check(!litch_max::HasAnyDuckChessMove(FOWLED_FEN, Square("b2")),
          "invalid overlapping duck is not a valid move position");
}

} // namespace

int main() {
    std::puts("[DUCK_CHESS_RULES] start");
    TestInputValidation();
    TestSlidingBlocker();
    TestKnightAndPawnMoves();
    TestKingSafetyIsIgnored();
    TestCastling();
    TestEnPassant();
    TestPromotionAndKingCapture();
    TestCaptureAndCastlingRightsMetadata();
    TestMetadataAndFowling();
    if (g_failures == 0) {
        std::puts("[DUCK_CHESS_RULES] ok");
        return 0;
    }
    std::printf("[DUCK_CHESS_RULES] failures=%d\n", g_failures);
    return 1;
}
