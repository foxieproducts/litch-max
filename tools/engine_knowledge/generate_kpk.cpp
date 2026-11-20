// Original offline KPK retrograde generator. No external table or engine source.
// From the repository root:
// c++ -std=c++17 -O2 tools/engine_knowledge/generate_kpk.cpp -o /tmp/generate_kpk
// Regenerate: /tmp/generate_kpk --write src/classic_chess_kpk_data.inc
// Verify:     /tmp/generate_kpk --check src/classic_chess_kpk_data.inc
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr uint32_t STATE_COUNT = 24U * 64U * 64U * 2U;
constexpr uint32_t BYTE_COUNT = STATE_COUNT / 8U;
constexpr uint16_t UNKNOWN_DISTANCE = std::numeric_limits<uint16_t>::max();
constexpr unsigned GENERATOR_VERSION = 1U;

// Squares a1=0 ... h8=63. Only pawn files a-d and ranks 2-7 are stored.
struct State {
    int pawn;
    int ownerKing;
    int defenderKing;
    bool defenderTurn;
};

uint32_t Index(const State& state) {
    const unsigned pawn = (state.pawn / 8 - 1) * 4 + state.pawn % 8;
    return (((pawn * 64U + state.ownerKing) * 64U + state.defenderKing) * 2U) +
           state.defenderTurn;
}

State Decode(uint32_t index) {
    State state{};
    state.defenderTurn = (index & 1U) != 0;
    index /= 2U;
    state.defenderKing = index % 64U;
    index /= 64U;
    state.ownerKing = index % 64U;
    index /= 64U;
    state.pawn = (index / 4U + 1U) * 8U + index % 4U;
    return state;
}

int Distance(int lhs, int rhs) {
    return std::max(std::abs(lhs / 8 - rhs / 8), std::abs(lhs % 8 - rhs % 8));
}

bool PawnAttacks(int pawn, int square) {
    return square / 8 == pawn / 8 + 1 && std::abs(square % 8 - pawn % 8) == 1;
}

bool Legal(const State& state) {
    return state.ownerKing != state.pawn && state.defenderKing != state.pawn &&
           Distance(state.ownerKing, state.defenderKing) > 1 &&
           (state.defenderTurn || !PawnAttacks(state.pawn, state.defenderKing));
}

template <typename Callback>
void KingDestinations(int king, Callback callback) {
    for (int rank = -1; rank <= 1; ++rank) {
        for (int file = -1; file <= 1; ++file) {
            if (rank == 0 && file == 0) continue;
            const int toRank = king / 8 + rank, toFile = king % 8 + file;
            if (toRank >= 0 && toRank < 8 && toFile >= 0 && toFile < 8)
                callback(toRank * 8 + toFile);
        }
    }
}

bool MajorAttacks(int major, int king, int target, bool queen) {
    const int rank = target / 8 - major / 8;
    const int file = target % 8 - major % 8;
    if (rank != 0 && file != 0 && (!queen || std::abs(rank) != std::abs(file)))
        return false;
    if (rank == 0 && file == 0) return false;
    const int step = (rank == 0 ? 0 : rank > 0 ? 8 : -8) +
                     (file == 0 ? 0 : file > 0 ? 1 : -1);
    for (int square = major + step; square != target; square += step)
        if (square == king) return false;
    return true;
}

// KRK/KQK conversion is won unless the new piece is capturable immediately
// or the defender is stalemated. A rook can avoid a queen-promotion stalemate.
bool PromotionWins(const State& state, bool queen) {
    const int promoted = state.pawn + 8;
    bool capture = false, legalMove = false;
    KingDestinations(state.defenderKing, [&](int to) {
        if (Distance(to, state.ownerKing) <= 1) return;
        if (to == promoted) {
            capture = true;
            legalMove = true;
        } else if (!MajorAttacks(promoted, state.ownerKing, to, queen)) {
            legalMove = true;
        }
    });
    return !capture && (legalMove ||
           MajorAttacks(promoted, state.ownerKing, state.defenderKing, queen));
}

struct Moves {
    std::array<uint32_t, 10> next{};
    uint8_t count = 0;
    uint8_t legalCount = 0;
    bool winningPromotion = false;
};

Moves Successors(const State& state) {
    Moves moves;
    const int mover = state.defenderTurn ? state.defenderKing : state.ownerKing;
    KingDestinations(mover, [&](int to) {
        State after = state;
        if (state.defenderTurn) {
            if (Distance(to, state.ownerKing) <= 1) return;
            if (to == state.pawn) { ++moves.legalCount; return; } // K-K draw.
            if (PawnAttacks(state.pawn, to)) return;
            after.defenderKing = to;
        } else {
            if (to == state.pawn || Distance(to, state.defenderKing) <= 1) return;
            after.ownerKing = to;
        }
        after.defenderTurn = !after.defenderTurn;
        moves.next[moves.count++] = Index(after);
        ++moves.legalCount;
    });
    if (!state.defenderTurn) {
        const int to = state.pawn + 8;
        if (to != state.ownerKing && to != state.defenderKing) {
            if (state.pawn / 8 == 6) {
                moves.legalCount += 4; // Explicit Q/R/B/N promotion choices.
                moves.winningPromotion = PromotionWins(state, true) || PromotionWins(state, false);
            } else {
                auto after = state;
                after.pawn = to;
                after.defenderTurn = true;
                moves.next[moves.count++] = Index(after);
                ++moves.legalCount;
                if (state.pawn / 8 == 1 && to + 8 != state.ownerKing &&
                    to + 8 != state.defenderKing) {
                    after.pawn = to + 8;
                    moves.next[moves.count++] = Index(after);
                    ++moves.legalCount;
                }
            }
        }
    }
    return moves;
}

bool Mated(const State& state, const Moves& moves) {
    return state.defenderTurn && moves.legalCount == 0 &&
           PawnAttacks(state.pawn, state.defenderKing);
}

struct Table {
    std::vector<uint8_t> legal = std::vector<uint8_t>(STATE_COUNT, 0);
    std::vector<uint8_t> won = std::vector<uint8_t>(STATE_COUNT, 0);
    std::vector<uint32_t> offsets = std::vector<uint32_t>(STATE_COUNT + 1U, 0);
    std::vector<uint32_t> predecessors;
    std::vector<uint16_t> zeroing = std::vector<uint16_t>(STATE_COUNT, UNKNOWN_DISTANCE);
    uint32_t legalCount = 0;
    uint32_t winCount = 0;
    uint16_t maxZeroing = 0;
};

Table Generate() {
    Table table;
    std::vector<uint8_t> remaining(STATE_COUNT, 0);
    std::queue<uint32_t> queue;
    for (uint32_t index = 0; index < STATE_COUNT; ++index) {
        const auto state = Decode(index);
        if (!Legal(state)) continue;
        table.legal[index] = 1;
        ++table.legalCount;
        const auto moves = Successors(state);
        remaining[index] = moves.legalCount;
        for (unsigned move = 0; move < moves.count; ++move)
            ++table.offsets[moves.next[move] + 1U];
        if (moves.winningPromotion || Mated(state, moves)) {
            table.won[index] = 1;
            queue.push(index);
        }
    }
    for (uint32_t index = 1; index <= STATE_COUNT; ++index)
        table.offsets[index] += table.offsets[index - 1U];
    table.predecessors.resize(table.offsets.back());
    auto cursor = table.offsets;
    for (uint32_t index = 0; index < STATE_COUNT; ++index) {
        if (!table.legal[index]) continue;
        const auto moves = Successors(Decode(index));
        for (unsigned move = 0; move < moves.count; ++move) {
            const uint32_t child = moves.next[move];
            if (!table.legal[child]) throw std::runtime_error("illegal generated successor");
            table.predecessors[cursor[child]++] = index;
        }
    }
    while (!queue.empty()) {
        const auto child = queue.front();
        queue.pop();
        for (auto offset = table.offsets[child]; offset < table.offsets[child + 1U]; ++offset) {
            const auto parent = table.predecessors[offset];
            if (table.won[parent]) continue;
            if ((parent & 1U) == 0 || --remaining[parent] == 0) {
                table.won[parent] = 1;
                queue.push(parent);
            }
        }
    }

    // An auxiliary offline certificate solves min/max distance to the next
    // WDL-preserving pawn move, promotion or mate. Only its global maximum ships.
    using Pending = std::pair<uint16_t, uint32_t>;
    std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> progress;
    std::vector<uint16_t> maxChild(STATE_COUNT, 0);
    for (uint32_t index = 0; index < STATE_COUNT; ++index) {
        if (!table.won[index]) continue;
        ++table.winCount;
        const auto state = Decode(index);
        const auto moves = Successors(state);
        remaining[index] = moves.legalCount;
        bool zeroes = moves.winningPromotion;
        if (!state.defenderTurn) {
            for (unsigned move = 0; move < moves.count; ++move)
                zeroes |= Decode(moves.next[move]).pawn != state.pawn && table.won[moves.next[move]];
        }
        if (zeroes || Mated(state, moves)) {
            table.zeroing[index] = zeroes ? 1U : 0U;
            progress.emplace(table.zeroing[index], index);
        }
    }
    while (!progress.empty()) {
        const auto [distance, child] = progress.top();
        progress.pop();
        for (auto offset = table.offsets[child]; offset < table.offsets[child + 1U]; ++offset) {
            const auto parent = table.predecessors[offset];
            if (!table.won[parent] || table.zeroing[parent] != UNKNOWN_DISTANCE ||
                Decode(parent).pawn != Decode(child).pawn) continue;
            maxChild[parent] = std::max(maxChild[parent], distance);
            if ((parent & 1U) == 0 || --remaining[parent] == 0) {
                table.zeroing[parent] = (parent & 1U) == 0 ? distance + 1U : maxChild[parent] + 1U;
                progress.emplace(table.zeroing[parent], parent);
            }
        }
    }
    for (uint32_t index = 0; index < STATE_COUNT; ++index) {
        if (!table.legal[index]) continue;
        const auto state = Decode(index);
        const auto moves = Successors(state);
        unsigned wonChildren = 0;
        for (unsigned move = 0; move < moves.count; ++move)
            wonChildren += table.won[moves.next[move]];
        const bool wins = state.defenderTurn ? Mated(state, moves) ||
                          (moves.legalCount != 0 && wonChildren == moves.legalCount) :
                          moves.winningPromotion || wonChildren != 0;
        if (wins != bool(table.won[index])) throw std::runtime_error("Bellman mismatch");
        if (table.won[index]) {
            if (table.zeroing[index] == UNKNOWN_DISTANCE)
                throw std::runtime_error("win has no forced progress certificate");
            table.maxZeroing = std::max(table.maxZeroing, table.zeroing[index]);
        }
    }
    return table;
}

std::string Render(const Table& table) {
    std::array<uint8_t, BYTE_COUNT> packed{};
    for (uint32_t index = 0; index < STATE_COUNT; ++index)
        if (table.won[index]) packed[index / 8U] |= uint8_t(1U << (index % 8U));
    std::ostringstream text;
    text << "// Generated by tools/engine_knowledge/generate_kpk.cpp, version " << GENERATOR_VERSION << ".\n"
         << "// Original rules/retrograde; no downloaded table or GPL source.\n"
         << "// Regenerate/check with --write/--check; see the generator's header.\n"
         << "// a1=0; index=(((4*(pawnRank-2)+pawnFile)*64+ownerKing)*64+defenderKing)*2+defenderTurn.\n"
         << "// Horizontal mirror stores files a-d; vertical/color mirror stores White pawns.\n"
         << "// Legal states: " << table.legalCount << "; wins: " << table.winCount << "; invalid bits are zero.\n"
         << "// Infinite-rule WDL; repetition remains a caller responsibility.\n"
         << "// Offline certified maximum plies to a winning pawn move/promotion/mate: " << table.maxZeroing << ".\n"
         << "constexpr uint16_t KPK_MAX_ZEROING_PLIES = " << table.maxZeroing << "U;\n"
         << "constexpr uint8_t KPK_WIN_BITS[24576] = {\n";
    text << std::hex << std::setfill('0');
    for (unsigned byte = 0; byte < packed.size(); ++byte) {
        if (byte % 16U == 0) text << "    ";
        text << "0x" << std::setw(2) << unsigned(packed[byte]) << ',';
        text << (byte % 16U == 15U ? '\n' : ' ');
    }
    text << "};\n";
    return text.str();
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 || (std::string(argv[1]) != "--write" && std::string(argv[1]) != "--check"))
            throw std::runtime_error("usage: generate_kpk --write|--check path");
        const auto table = Generate();
        const auto rendered = Render(table);
        if (std::string(argv[1]) == "--check") {
            std::ifstream input(argv[2], std::ios::binary);
            const std::string actual{std::istreambuf_iterator<char>(input), {}};
            if (!input || actual != rendered) throw std::runtime_error("generated asset differs");
        } else {
            std::ofstream output(argv[2], std::ios::binary);
            output << rendered;
            if (!output) throw std::runtime_error("cannot write asset");
        }
        std::cout << "KPK version=" << GENERATOR_VERSION << " states=" << STATE_COUNT
                  << " legal=" << table.legalCount << " wins=" << table.winCount
                  << " bytes=" << BYTE_COUNT << " max_zeroing_plies=" << table.maxZeroing << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
