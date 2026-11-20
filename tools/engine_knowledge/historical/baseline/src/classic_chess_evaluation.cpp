#include <cstdint>

#include <mcu-max-position.h>

#include "modules/classic_chess_evaluation.hpp"

namespace modules::classic_chess
{
namespace
{
constexpr uint8_t WHITE = 8;
constexpr uint8_t BLACK = 16;
constexpr int FULL_PHASE = 24;
constexpr int VALUES[8] = {0, 100, 100, 320, 0, 330, 500, 900};
constexpr int PHASE[8] = {0, 0, 0, 1, 0, 1, 2, 4};
constexpr int KNIGHT_STEPS[8] = {-33, -31, -18, -14, 14, 18, 31, 33};
constexpr int RAY_STEPS[8] = {-17, -15, 15, 17, -16, -1, 1, 16};
constexpr int PASSED_MIDDLE[8] = {0, 0, 6, 12, 24, 40, 65, 0};
constexpr int PASSED_END[8] = {0, 0, 12, 24, 48, 80, 125, 0};

// Feature weights use centipawns; confinement describes geometric risk, not a forced loss.
// Phase derives from the present pieces: promotions cannot overflow the taper.
struct AttackMap
{
    uint32_t low = 0;
    uint32_t high = 0;
};

struct Score
{
    int middle[2] = {};
    int end[2] = {};
    uint8_t pawns[2][8] = {};
    uint8_t bishops[2] = {};
    uint8_t queens[2] = {};
    uint8_t queenDistance[2] = {8U, 8U};
    uint16_t ringAttacks[2] = {};
    AttackMap attacks[2] = {};
    AttackMap twice[2] = {};
    AttackMap pawnAttacks[2] = {};
    uint8_t attackers[2] = {};
    uint16_t pressure[2] = {};
    int phase = 0;
};

int Abs(int value)
{
    return value < 0 ? -value : value;
}

bool OnBoard(int square)
{
    return square >= 0 && square < 128 && (square & 0x88) == 0;
}

uint8_t Type(uint8_t piece)
{
    return piece & 7;
}

uint8_t Color(uint8_t piece)
{
    return piece & 24;
}

int SideIndex(uint8_t color)
{
    return color == WHITE ? 0 : 1;
}

int RelativeRank(int square, int side)
{
    return side == 0 ? 7 - (square >> 4) : square >> 4;
}

bool Contains(const AttackMap& map, int square)
{
    const int index = ((square & 0x70) >> 1) | (square & 7);
    const uint32_t bit = 1U << (index & 31);
    return ((index < 32 ? map.low : map.high) & bit) != 0;
}


void MarkPawnAttack(AttackMap& map, int square)
{
    if (!OnBoard(square)) return;
    const int index = ((square & 0x70) >> 1) | (square & 7);
    (index < 32 ? map.low : map.high) |= 1U << (index & 31);
}

void AddSquareBonus(Score& score, int square, int side, uint8_t type)
{
    const int rank = RelativeRank(square, side);
    const int fileCenter = 7 - Abs(2 * (square & 7) - 7);
    const int center = fileCenter + 7 - Abs(2 * (square >> 4) - 7);
    int middle = 0;
    int end = 0;
    switch (type)
    {
    case MCUMAX_PAWN_UPSTREAM:
    case MCUMAX_PAWN_DOWNSTREAM:
        middle = (rank - 1) * 5 + fileCenter * 2;
        end = (rank - 1) * 8;
        break;
    case MCUMAX_KNIGHT:
        middle = center * 3 - (rank == 0 ? 10 : 0);
        end = center * 2;
        break;
    case MCUMAX_BISHOP:
        middle = center * 2 + rank * 2;
        end = center * 2;
        break;
    case MCUMAX_ROOK:
        middle = rank * 2;
        end = center;
        break;
    case MCUMAX_QUEEN:
        middle = center;
        end = center * 2;
        break;
    case MCUMAX_KING:
        middle = -center * 2 - rank * 8;
        end = center * 4;
        break;
    default:
        break;
    }
    score.middle[side] += VALUES[type] + middle;
    score.end[side] += VALUES[type] + end;
}

bool IsPassed(const mcumax_position& position, int square, int side)
{
    const int step = side == 0 ? -16 : 16;
    const uint8_t enemy = side == 0 ? BLACK : WHITE;
    const uint8_t pawn = side == 0 ? MCUMAX_PAWN_DOWNSTREAM : MCUMAX_PAWN_UPSTREAM;
    for (int ahead = square + step; OnBoard(ahead); ahead += step)
    {
        for (int file = -1; file <= 1; ++file)
        {
            const int target = ahead + file;
            if (OnBoard(target) && Color(position.board[target]) == enemy &&
                Type(position.board[target]) == pawn)
                return false;
        }
    }
    return true;
}

void AddPawnFeatures(const mcumax_position& position, Score& score, int square, int side)
{
    const int file = square & 7;
    const bool left = file > 0 && score.pawns[side][file - 1] != 0;
    const bool right = file < 7 && score.pawns[side][file + 1] != 0;
    if (!left && !right)
    {
        score.middle[side] -= 10;
        score.end[side] -= 8;
    }
    if (Contains(score.pawnAttacks[side], square))
    {
        score.middle[side] += 6;
        score.end[side] += 8;
    }
    if (IsPassed(position, square, side))
    {
        const int rank = RelativeRank(square, side);
        score.middle[side] += PASSED_MIDDLE[rank];
        score.end[side] += PASSED_END[rank];
    }
}

bool InKingRing(const mcumax_position& position, int square, int kingSide)
{
    const int king = position.king_square[kingSide];
    return Abs((square & 7) - (king & 7)) <= 1 &&
           Abs((square >> 4) - (king >> 4)) <= 1;
}

void MarkRingAttack(const mcumax_position& position, Score& score,
                    int square, int kingSide)
{
    if (!InKingRing(position, square, kingSide)) return;
    const int king = position.king_square[kingSide];
    const int bit = ((square >> 4) - (king >> 4) + 1) * 3 +
                    (square & 7) - (king & 7) + 1;
    score.ringAttacks[kingSide] |= static_cast<uint16_t>(1U << bit);
}


void RecordAttack(const mcumax_position& position, Score& score,
                  int target, int side, uint8_t type)
{
    const int index = ((target & 0x70) >> 1) | (target & 7);
    const uint32_t bit = 1U << (index & 31);
    uint32_t& all = index < 32 ? score.attacks[side].low : score.attacks[side].high;
    uint32_t& twice = index < 32 ? score.twice[side].low : score.twice[side].high;
    twice |= all & bit;
    all |= bit;
    if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM)
        (index < 32 ? score.pawnAttacks[side].low : score.pawnAttacks[side].high) |= bit;
    MarkRingAttack(position, score, target, 1 - side);
}

void AddKnightConfinement(const mcumax_position& position, Score& score,
                          int square, int side)
{
    const uint8_t color = side == 0 ? WHITE : BLACK;
    bool destination = false;
    for (int step : KNIGHT_STEPS)
    {
        const int target = square + step;
        if (!OnBoard(target) || Color(position.board[target]) == color ||
            Type(position.board[target]) == MCUMAX_KING) continue;
        destination = true;
        // A profitable capture is useful even if it trades the Knight.
        if (position.board[target] != 0 && VALUES[Type(position.board[target])] >= VALUES[MCUMAX_KNIGHT]) return;
        // This Knight already attacks each destination. A second friendly
        // attack is required to count a real defender after it moves there.
        const bool guarded = Contains(score.twice[side], target);
        if (!Contains(score.pawnAttacks[1 - side], target) &&
            (!Contains(score.attacks[1 - side], target) || guarded)) return;
    }
    if (destination)
    {
        score.middle[side] -= VALUES[MCUMAX_KNIGHT] / 2;
        score.end[side] -= VALUES[MCUMAX_KNIGHT] / 2;
    }
}

int SafeKingFlights(const mcumax_position& position, const Score& score, int side)
{
    const int king = position.king_square[side];
    const uint8_t color = side == 0 ? WHITE : BLACK;
    int flights = 0;
    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int df = -1; df <= 1; ++df)
        {
            if (dr == 0 && df == 0) continue;
            const int square = king + dr * 16 + df;
            const int bit = (dr + 1) * 3 + df + 1;
            if (OnBoard(square) && Color(position.board[square]) != color &&
                Type(position.board[square]) != MCUMAX_KING &&
                (score.ringAttacks[side] & (1U << bit)) == 0U)
                ++flights;
        }
    }
    return flights;
}

// Geometric attacks stop at the first blocker. This is a small positional
// feature, not a substitute for legal move generation or tactical search.
void AddActivity(const mcumax_position& position, Score& score, int square,
                 int side, uint8_t type)
{
    const uint8_t color = side == 0 ? WHITE : BLACK;
    int mobility = 0;
    int ringHits = 0;
    if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM)
    {
        const int forward = side == 0 ? -16 : 16;
        for (int offset = -1; offset <= 1; offset += 2)
        {
            const int target = square + forward + offset;
            if (!OnBoard(target)) continue;
            RecordAttack(position, score, target, side, type);
            if (InKingRing(position, target, 1 - side)) ++ringHits;
        }
    }
    else if (type == MCUMAX_KING)
    {
        for (const int step : RAY_STEPS)
        {
            const int target = square + step;
            if (OnBoard(target)) RecordAttack(position, score, target, side, type);
        }
    }
    else if (type == MCUMAX_KNIGHT)
    {
        for (const int step : KNIGHT_STEPS)
        {
            const int target = square + step;
            if (!OnBoard(target)) continue;
            RecordAttack(position, score, target, side, type);
            if (InKingRing(position, target, 1 - side)) ++ringHits;
            if (Color(position.board[target]) != color &&
                Type(position.board[target]) != MCUMAX_KING &&
                !Contains(score.pawnAttacks[1 - side], target))
                ++mobility;
        }
    }
    else if (type == MCUMAX_BISHOP || type == MCUMAX_ROOK || type == MCUMAX_QUEEN)
    {
        const int first = type == MCUMAX_ROOK ? 4 : 0;
        const int last = type == MCUMAX_BISHOP ? 4 : 8;
        for (int direction = first; direction < last; ++direction)
        {
            const int step = RAY_STEPS[direction];
            for (int target = square + step; OnBoard(target); target += step)
            {
                const uint8_t occupant = position.board[target];
                RecordAttack(position, score, target, side, type);
                if (InKingRing(position, target, 1 - side)) ++ringHits;
                if (Color(occupant) != color && Type(occupant) != MCUMAX_KING &&
                    !Contains(score.pawnAttacks[1 - side], target))
                    ++mobility;
                if (occupant != 0)
                {
                    // A king move vacates this blocker. The square immediately
                    // behind it remains attacked by the same slider.
                    if (Type(occupant) == MCUMAX_KING && Color(occupant) != color)
                    {
                        const int beyond = target + step;
                        if (OnBoard(beyond)) MarkRingAttack(position, score, beyond, 1 - side);
                    }
                    break;
                }
            }
        }
    }
    if (ringHits != 0)
    {
        const int weight = type == MCUMAX_QUEEN ? 4 : type == MCUMAX_ROOK ? 3 :
                           type == MCUMAX_KNIGHT || type == MCUMAX_BISHOP ? 2 : 1;
        ++score.attackers[1 - side];
        score.pressure[1 - side] += ringHits * weight;
    }
    // Neutral reference mobility avoids adding a large bonus merely for owning
    // a queen; capture/pawn safety remains the search's responsibility.
    switch (type)
    {
    case MCUMAX_KNIGHT:
        score.middle[side] += (mobility - 4) * 4;
        score.end[side] += (mobility - 4) * 4;
        break;
    case MCUMAX_BISHOP:
        score.middle[side] += (mobility - 7) * 4;
        score.end[side] += (mobility - 7) * 4;
        break;
    case MCUMAX_ROOK:
        score.middle[side] += (mobility - 7) * 2;
        score.end[side] += (mobility - 7) * 3;
        break;
    case MCUMAX_QUEEN:
        score.middle[side] += mobility - 14;
        score.end[side] += (mobility - 14) * 2;
        break;
    default:
        break;
    }
}

void AddKingShelter(const mcumax_position& position, Score& score, int side)
{
    const int king = position.king_square[side];
    const int kingFile = king & 7;
    const int forward = side == 0 ? -16 : 16;
    const uint8_t color = side == 0 ? WHITE : BLACK;
    const uint8_t pawn = side == 0 ? MCUMAX_PAWN_UPSTREAM : MCUMAX_PAWN_DOWNSTREAM;
    for (int file = kingFile - 1; file <= kingFile + 1; ++file)
    {
        if (file < 0 || file > 7)
            continue;
        int shelter = score.pawns[side][file] == 0 ? -12 : -6;
        for (int distance = 2; distance >= 1; --distance)
        {
            const int target = (king & 0x70) + file + distance * forward;
            if (OnBoard(target) && Color(position.board[target]) == color &&
                Type(position.board[target]) == pawn)
                shelter = distance == 1 ? 14 : 8;
        }
        score.middle[side] += shelter;
    }
    const int attackers = score.attackers[side] < 3 ? score.attackers[side] : 3;
    const int pressure = score.pressure[side] < 24 ? score.pressure[side] : 24;
    // Coordinated contacts reinforce each other. Cap below one Queen;
    // a single attacker keeps its modest linear value.
    const int units = pressure * attackers;
    const int coordinated = units * units / 4;
    const int danger = attackers < 2 ? pressure * (2 + attackers) / 2 :
        (coordinated < 900 ? coordinated : 900);
    score.middle[side] -= danger;
    // Apply only with coordinated pressure and an enemy Queen. Ordinary
    // blocked opening Kings and low-material endings get no new penalty.
    if (attackers >= 2 && score.queens[1 - side] != 0 && score.queenDistance[1 - side] <= 3U)
    {
        constexpr int CONFINEMENT[4] = {600, 450, 300, 150};
        const int flights = SafeKingFlights(position, score, side);
        if (flights < 4) score.middle[side] -= CONFINEMENT[flights];
    }
}
}

int PieceValue(uint8_t type)
{
    return VALUES[Type(type)];
}

int Evaluate(const mcumax_position& position)
{
    Score score;
    for (int square = 0; square < 128; ++square)
    {
        if ((square & 0x88) != 0)
            continue;
        const uint8_t piece = position.board[square];
        if (piece == 0)
            continue;
        const uint8_t type = Type(piece);
        const int side = SideIndex(Color(piece));
        score.phase += PHASE[type];
        if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM)
        {
            ++score.pawns[side][square & 7];
            // Complete both sides before mobility/defended-pawn queries.
            const int forward = side == 0 ? -16 : 16;
            MarkPawnAttack(score.pawnAttacks[side], square + forward - 1);
            MarkPawnAttack(score.pawnAttacks[side], square + forward + 1);
        }
        if (type == MCUMAX_BISHOP)
            ++score.bishops[side];
        if (type == MCUMAX_QUEEN)
        {
            ++score.queens[side];
            const int king = position.king_square[1 - side];
            const int files = Abs((square & 7) - (king & 7));
            const int ranks = Abs((square >> 4) - (king >> 4));
            const int distance = files > ranks ? files : ranks;
            if (distance < score.queenDistance[side]) score.queenDistance[side] = distance;
        }
    }
    for (int square = 0; square < 128; ++square)
    {
        if ((square & 0x88) != 0 || position.board[square] == 0)
            continue;
        const uint8_t piece = position.board[square];
        const uint8_t type = Type(piece);
        const int side = SideIndex(Color(piece));
        AddSquareBonus(score, square, side, type);
        AddActivity(position, score, square, side, type);
        if (type == MCUMAX_PAWN_UPSTREAM || type == MCUMAX_PAWN_DOWNSTREAM)
            AddPawnFeatures(position, score, square, side);
        if (type == MCUMAX_ROOK && score.pawns[side][square & 7] == 0)
        {
            const bool open = score.pawns[1 - side][square & 7] == 0;
            score.middle[side] += open ? 18 : 10;
            score.end[side] += open ? 14 : 8;
        }
    }
    for (int square = 0; square < 128; ++square)
    {
        if ((square & 0x88) == 0 && Type(position.board[square]) == MCUMAX_KNIGHT)
            AddKnightConfinement(position, score, square, SideIndex(Color(position.board[square])));
    }
    for (int side = 0; side < 2; ++side)
    {
        for (const uint8_t count : score.pawns[side])
        {
            if (count > 1)
            {
                score.middle[side] -= (count - 1) * 12;
                score.end[side] -= (count - 1) * 18;
            }
        }
        if (score.bishops[side] >= 2)
        {
            score.middle[side] += 25;
            score.end[side] += 35;
        }
        AddKingShelter(position, score, side);
    }
    const int phase = score.phase < FULL_PHASE ? score.phase : FULL_PHASE;
    const int middle = score.middle[0] - score.middle[1];
    const int end = score.end[0] - score.end[1];
    const int whiteScore = (middle * phase + end * (FULL_PHASE - phase)) / FULL_PHASE;
    return position.side == WHITE ? whiteScore : -whiteScore;
}
}
