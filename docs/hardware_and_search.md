# Running Litch-Max on the ESP32-S3

## Hardware and memory

The Foxie Litch board uses Espressif's ESP32-S3-MINI-1-N8 module. Its two 32-bit Xtensa LX7 cores run at up to 240 MHz. The chip has 512 KB of SRAM, and the N8 module has 8 MB of flash and no PSRAM. A search runs on one worker. The board also needs internal RAM for Bluetooth, board I/O and the rest of the firmware. [Espressif module datasheet, sections 1.1–1.2](https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.html)

| Memory to budget | Bytes on ESP32-S3 | What it covers |
| --- | ---: | --- |
| Shared workspace reservation | 12,112 | One 8-byte-aligned owner, including Classic frames, all 512 ordinary cache entries and 128 tactical entries |
| Duck search scratch | 5,568 | Space used within that reservation during a full Duck turn |
| Board firmware search worker stack | 8,192 | Separate firmware allocation shared by the two search modes |

The firmware engine owns one workspace for its lifetime. Its 12,112 bytes remain reserved while idle and during either search. Classic and Duck use the same storage in turn and make no heap requests. Each simultaneous search needs a separate owner. Keep the workspace outside the board worker's stack. `sizeof(SearchWorkspace)` gives the reservation for your target. `RequiredBytes()` and `DuckRequiredBytes()` give the scratch used by each variant. [Workspace and result types](../include/litch_max/classic_chess_search.hpp)

Pass that workspace in `DuckChessSearchConfig::workspace` for a full Duck turn on a small task stack. The convenience call without an owner uses 5,568 bytes of automatic scratch on Xtensa, in addition to ordinary call frames. Duck placement requires no search workspace. [Duck search configuration](../include/litch_max/duck_chess_search.hpp)

## What the board measured

A USB-powered V3.3 running at 240 MHz searched six fixed Classic positions at each of the eleven levels with normal board services active. The table gives medians and ranges over those six positions. Node and time medians are rounded to whole values. Worker time runs from search start to the final move. The measurement excludes input handling and delivery of the move to the player.

Here is one of the six positions, an Italian opening with White to move. The levels are firmware presets. Node counts include work spent trying to reach a deeper iteration.

```text
r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4
```

| Level | Median nodes (range) | Completed depth, plies (range) | Worker time, ms (range) |
| ---: | ---: | ---: | ---: |
| 1 | 36 (2–42) | 1 (1–1) | 8 (2–8) |
| 2 | 577 (14–1,117) | 2 (2–2) | 89 (3–177) |
| 3 | 3,264 (171–3,584) | 2 (1–4) | 636 (24–654) |
| 4 | 7,552 (171–9,852) | 3 (2–6) | 1,406 (25–1,430) |
| 5 | 14,400 (171–28,032) | 3 (3–6) | 2,808 (23–2,815) |
| 6 | 24,960 (171–49,152) | 4 (3–7) | 4,909 (24–4,919) |
| 7 | 34,624 (171–66,176) | 4 (3–7) | 7,010 (25–7,025) |
| 8 | 51,648 (171–101,248) | 4 (3–7) | 10,508 (26–10,524) |
| 9 | 88,704 (171–174,080) | 5 (3–8) | 17,517 (28–17,525) |
| 10 | 146,880 (171–277,888) | 5 (3–8) | 28,014 (28–28,021) |
| 11 | 312,704 (171–591,360) | 6 (3–9) | 60,006 (28–60,014) |

Levels 3–10 use shorter time limits than the earlier profile. Level 11 keeps its 60-second limit. Position complexity changes both node count and depth, so measure the positions that matter on your own target. [Playing-strength results](../README.md#classic-playing-strength)

## Choosing a budget

**Classic.** Start with an explicit `maxNodes` and `maxDepth`. The [Classic example](../examples/classic.cpp) sets 4,096 nodes and three plies. One ply is one player's move. Full search also examines tactical continuations at the horizon (quiescence), which count toward the node limit. Leave `firstRootNodeReserve` at zero for a strict node limit. A larger reserve can let the first root exceed `maxNodes`. A zero node limit removes that cap.

A quick one- or two-ply static evaluation (`staticLeafOnly` with the corresponding depth) can suit a novice opponent. It can miss a recapture or tactic beyond that horizon. Full search spends more work exploring such positions. Inspect `completedDepth` and `available` before playing a result. An interrupted search can retain a legal move from the last completed depth. If it never completed an eligible depth, it may have no move to play. Supply the real position's reversible halfmove count through `reversibleHalfmoves` when the fifty-move rule matters.

**Wall time.** A node budget alone cannot promise a response time. Supply a monotonic deadline through `SearchConfig::shouldStop` and `stopContext`. This desktop example uses a Classic `SearchConfig config` and `<chrono>`.

```cpp
using Clock = std::chrono::steady_clock;
auto deadline = Clock::now() + std::chrono::milliseconds(200);
config.stopContext = &deadline;
config.shouldStop = [](void* context) {
    return Clock::now() >= *static_cast<const Clock::time_point*>(context);
};
// Run the search while deadline is in scope.
```

On a board, use its monotonic timer in the callback. Cancellation is cooperative and takes effect at search checkpoints. Classic also exposes `cooperate` for the caller to yield to other tasks. Keep callbacks short and let search run off the input or display thread.

**Duck.** Its depth counts complete turns. Each ordinary chess move branches again over empty Duck destinations, so a one-turn Duck search and a one-ply Classic search cover different amounts of work. Set Duck's own `maxNodes` and `maxDepth` and pass a deadline callback to `FindBestDuckChessTurn` if needed. Check its return value and `completedDepth` before using the move. A budget that expires partway through the first depth can leave an empty result. The two variants also report scores in different units (100 per Classic pawn and 1,000 per Duck pawn).

Keep the Duck square alongside the FEN. The blocker cannot be captured. After an ordinary chess move, commit the resulting FEN and new Duck square together. Capturing the opposing king wins immediately and ends the turn before placement. The [Duck example](../examples/duck.cpp) exercises both paths.
