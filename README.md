# Litch-Max

Litch-Max is the portable C and C++ chess engine used in the Foxie Litch electronic chessboard. It provides Classic and Duck chess rules, legal moves and bounded search. It runs on a desktop or a microcontroller without an Arduino or FreeRTOS dependency.

See the illuminated board and companion app on the Foxie Products site. [Meet Litch](https://foxieproducts.com/#litch) · [Board and assembly photos](https://foxieproducts.com/#build) · [Play and Game Review screenshots](https://foxieproducts.com/#features) · [Litchcraft gallery](https://foxieproducts.com/#litchcraft-screenshots) · [Buy a Litch board](https://foxieproducts.com/#buy)

## Build

Use CMake 3.20 or newer, a C11 compiler and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

In a CMake project, add this directory with `add_subdirectory` and link `litch_max::litch_max`. Set `BUILD_TESTING=OFF` when you only need the library. PlatformIO can use the included [library manifest](library.json).

## Run the examples

The [Classic example](examples/classic.cpp) plays `e2e4`, takes a position snapshot, searches for Black's reply and applies it to the game. The [Duck example](examples/duck.cpp) starts with the Duck on d4, plays a complete turn, then demonstrates a position where capturing the king ends the game immediately.

```sh
cmake -S . -B build/examples -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLITCH_MAX_BUILD_EXAMPLES=ON
cmake --build build/examples --parallel 2
./build/examples/example_classic
./build/examples/example_duck
```

With a multi-configuration generator such as Visual Studio, build with `--config Release` and run the programs from `build/examples/Release/`.

Use the Classic example to [compare 128 and 4,096 nodes after e2e4](docs/e2e4.md). Can Black still play a reply when the search runs out of nodes?

For a microcontroller build, see the [hardware and search tuning guide](docs/hardware_and_search.md). It gives RAM requirements on the ESP32-S3-MINI-1-N8, measured board timings and practical limits for each variant.

## Classic playing strength

Litch-Max played 650 desktop games against Stockfish 17.1 to estimate the eleven Classic CPU levels in Litch 1.0. The desktop searches used work budgets measured on the ESP32-S3 board. Wins, draws and losses are from Litch-Max’s perspective. The table rounds Elo estimates and intervals to the nearest ten.

| Level | Board time limit | Stockfish setting (W–D–L) | Estimated Elo | Approx. 95% interval |
| ---: | ---: | --- | ---: | ---: |
| 1 | 0.9 s | 1320 (0–0–50) | Not estimated | Not estimated |
| 2 | 0.9 s | 1320 (7–3–40) | 1,040 | 920–1,140 |
| 3 | 0.63 s | 1320 (13–6–5) · 1600 (6–1–19) | 1,430 | 1,300–1,570 |
| 4 | 1.4 s | 1320 (22–1–3) · 1600 (12–2–10) | 1,640 | 1,550–1,760 |
| 5 | 2.8 s | 1320 (20–0–4) · 1600 (11–6–9) | 1,620 | 1,470–1,840 |
| 6 | 4.9 s | 1600 (14–2–10) · 1800 (5–2–17) | 1,640 | 1,480–1,830 |
| 7 | 7 s | 1600 (13–3–8) · 1800 (9–3–14) | 1,710 | 1,540–1,920 |
| 8 | 10.5 s | 1600 (18–0–8) · 1800 (7–6–11) | 1,750 | 1,590–1,950 |
| 9 | 17.5 s | 1800 (11–5–8) · 2000 (12–5–9) | 1,910 | 1,730–2,140 |
| 10 | 28 s | 1800 (16–4–6) · 2000 (8–6–10) | 1,920 | 1,700–2,180 |
| 11 | 60 s | 1800 (45–10–17) · 2000 (36–3–39) | 1,930 | 1,750–2,150 |

Levels 1–10 played 50 games each. Level 11 played 150. The first 550 games used 25 six-ply openings at all eleven levels, once with each color. A further 50 openings supplied 100 new Level 11 games. Stockfish had one second per move, one thread and 64 MB of hash memory.

Stockfish’s 1320 setting anchors the scale. We estimated its higher settings and all eleven Litch levels together, counting draws as half a point. The 95% intervals come from resampling openings 10,000 times and keeping both colors together. Each resample used the original 25 openings across all levels and drew the extra Level 11 openings within their assigned opponent settings. Human and online rating pools have their own scales. [Stockfish rating calibration](https://official-stockfish.github.io/docs/stockfish-wiki/UCI-Protocol-and-Stockfish-Commands.html#uci-elo)

Level 1 scored 0/50 against Stockfish 1320, leaving its rating unestimated. Level 11 scored 81 wins, 13 draws and 56 losses for **1,928 Elo** (95% interval **1,753–2,153**). Against Stockfish’s 2000 setting alone it scored 37.5/78 points (48.1%). [Opening scores and board measurements](https://github.com/foxieproducts/litch/blob/934c1086fa1cf96767fce97092c298ca12e961c1/tools/elo/classic_strength_1_0.json) · [Match runner](https://github.com/foxieproducts/litch/tree/f824de294d26234b1b56d9ca9729987c473dc2a6/tools/elo) · [Earlier 550-game comparison](https://github.com/foxieproducts/litch-max/blob/f74febe32088bf775b783b44ccdad90de1a779a7/README.md#classic-playing-strength)

The Level 11 match budget of 273,280 nodes per move came from the lowest node count measured in four complex board positions. In the 100 new games, Litch-Max averaged 255,765 nodes and 6.31 completed plies per move in 0.506 seconds of desktop search. The board’s Level 11 limits are 60 seconds and 1.8 million nodes. Across six fixed positions on the V3.3, the board medians were 312,704 nodes, six completed plies and 60.006 seconds of search. [Physical timings at all eleven levels](docs/hardware_and_search.md)

## Search on the ESP32-S3

Litch-Max uses mcu-max’s board representation and move generator. Its Classic search and evaluation determine how deeply it explores each position within the caller’s budget. [Classic search](src/classic_chess_search.cpp) · [mcu-max provenance](docs/mcu_max_provenance.md)

With the same 60-second Level 11 limit, the V3.3 running full firmware searched **200,960 → 282,112 nodes (+40.4%)** in the Italian opening and again chose Ng5. In the middlegame it searched **224,128 → 324,224 nodes (+44.7%)**, reaching depth six and choosing Rfc1 in place of b4. The board advertised over Bluetooth during those runs. With a Bluetooth client connected, the middlegame improved from **216,576 to 312,960 nodes (+44.5%)**. All 11,261 LED frames across these measurements completed within 40 ms.

Classic updates material, piece-square scores and pawn information as it makes and undoes moves. The S3 build compiles the evaluator with `-O3` and places its 4,360-byte scoring function in instruction RAM. The subsequent 66-run board calibration recorded 273,280 nodes in the Italian opening and 315,520 in the middlegame at Level 11. [Benchmark positions](https://github.com/foxieproducts/litch/blob/f824de294d26234b1b56d9ca9729987c473dc2a6/src/engine_benchmark.cpp) · [Hardware and search tuning](docs/hardware_and_search.md)

## Using the engine

- Use [ClassicGame](include/litch_max/classic_game.hpp) for FEN, legal moves and moves in UCI notation. It wraps a single shared mcu-max position. Serialize access across all handles.
- Use [SearchWorkspace](include/litch_max/classic_chess_search.hpp) to analyze an owned position. Callers supply limits, cancellation and optional callbacks. One workspace serves one variant at a time.
- Use the [Duck rules](include/litch_max/duck_chess_rules.hpp) and [Duck search](include/litch_max/duck_chess_search.hpp) for a chess move followed by a Duck placement.
- The [notation helpers](include/litch_max/chess_notation.hpp) map square indices from `0=a8` to `63=h1`. The [opening-book guide](tools/engine_knowledge/generate_opening_book.md) explains the included book and how to reproduce the generated tables.

## Licenses

Foxie Products, LLC licenses original library code under [MPL 2.0](LICENSE) and original documentation under [CC BY-SA 4.0](LICENSE-DOCUMENTATION). The modified mcu-max engine retains its MIT license and H.G. Muller’s micro-Max attribution. See the [third-party notices](THIRD_PARTY_NOTICES.md) and [engine sources and permissions](docs/mcu_max_provenance.md).
