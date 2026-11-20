# Classic opening book

The book stores 408 distinct opening roots from 41 original, legal UCI lines
and one short baseline-selected continuation.
The lines describe reachable positions; they do not choose the recommendation.
At every root the pinned original Litch engine completes its ordinary Full
search at depth five and supplies the selected legal move and side-to-move
score. All 408 searches complete, using 27,001,933 nodes in total. Independent
python-chess replay verifies every input state and selected move. No external
opening database, random-key array or evaluation coefficients are imported.

The baseline is commit `1da224d728b49708c8525989afad39c2fcb78e4f`, identified as
`classic-full-v1`, with the exact source-input manifest hash in the generated
metadata. Depth is ordinary chess plies using that baseline's search and
evaluation semantics. The offline depth/score describe generation, not live
search work, measured strength or a guarantee of a good move.

| Authored family | Lines | Examples |
| --- | ---: | --- |
| Open games | 8 | Ruy Lopez, Italian, Scotch, Four Knights, Vienna, Bishop, Petroff, Philidor |
| Sicilian | 5 | Najdorf, Dragon, e6, Closed, Alapin positions |
| French | 3 | Advance, Classical, Exchange |
| Caro Kann | 2 | Classical, Advance |
| Other e4 defenses | 4 | Scandinavian, Alekhine, Pirc, Modern |
| Queen pawn | 6 | QGD, QGA, Slav, Tarrasch, London, Colle |
| Indian defenses | 4 | Kings Indian, Nimzo Indian, Queens Indian, Grunfeld |
| Dutch | 1 | Development position |
| Benoni | 1 | Development position |
| Flank openings | 8 | English, Reti, both fianchettos, Bird, Van Geet, baseline-selected continuation |

The eight covered first moves are e4, d4, c4, Nf3, g3, b3, f4 and Nc3.
Shared prefixes and exact transpositions appear once. When a position falls outside
the book, the caller can continue with a normal search.

## Looking up a move

Pass `TryOpeningBook` a Classic position, the complete move sequence from the
standard starting position, and the current halfmove and fullmove counters.
Skip the lookup for imported FENs, incomplete histories and other variants.
The function accepts up to sixteen plies. It replays the moves, checks the
counters and rejects repeated positions. A hit also requires the castling rights,
king metadata and en-passant state to match the stored record.

A hit returns the move, side-to-move score and depth from the search used to
build the book. Looking up a move performs no search or allocation. A miss leaves
the output untouched. Each record is 16 bytes. The table, line moves, offsets
and metadata occupy 7,937 bytes in the native build.

## Reproducing the included book

The [reproduction guide](historical/README.md) describes the 25 source files,
generator, driver and seed that produced the [shipped table](../../src/classic_chess_opening_book_data.inc).
The original generator includes the absolute source directory in its manifest
digest. When generating in a different directory, compare the records and
positions. An identical local manifest digest is unnecessary.

## Generating a new book

Use Python with python-chess installed and a frozen standalone Litch-Max source
manifest containing `identity`, `head`, `root`, and a `files` mapping of relative
path to SHA256. `identity` names the new campaign baseline, for example
`litch-max-full-v1`; it must not reuse the historical `classic-full-v1` name.
`root` identifies the frozen standalone checkout and `head` its full commit ID.
The file mapping must cover all library build inputs, including `CMakeLists.txt`,
public/internal headers, sources, generated data, and vendored source inputs.

The generator verifies every listed baseline file before and after the campaign.
It consumes that snapshot's `litch_max::litch_max` CMake target, which supplies
the standalone include paths without an external Litch checkout. It builds only
the driver target in a fresh temporary artifact directory, with at most two
compiler jobs:

```sh
python tools/engine_knowledge/generate_opening_book.py \
  --baseline-manifest /absolute/path/baseline-source.json \
  --artifact-dir /absolute/temporary/path/fresh-opening-book-generation \
  --output /absolute/temporary/path/classic_chess_opening_book_data.inc --jobs 2
```

The output includes the generated table and `validated-records.json`. Each record
contains its FEN, move prefix, chosen move, score, search depth, node count and
source line. The table includes hashes of the seed, packed records and baseline
manifest. Use `--resume` to finish packaging an interrupted run with the same
driver, seed, build inputs and requests.

The standalone KPK generator uses only standard C++17 and does not depend on a
historical engine snapshot. From the repository root, compile
`tools/engine_knowledge/generate_kpk.cpp` to a temporary executable with
`c++ -std=c++17 -O2`. Its `--check src/classic_chess_kpk_data.inc` mode compares
regenerated content with the checked-in table; `--write` explicitly replaces the
selected output. Neither generator runs as part of ordinary library builds.
To compile both drivers through CMake without executing either campaign:

```sh
cmake -S . -B /absolute/temporary/path/litch-max-generators \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLITCH_MAX_BUILD_GENERATORS=ON
cmake --build /absolute/temporary/path/litch-max-generators --parallel 2 \
  --target litch_max_generate_kpk litch_max_generate_opening_book
```

To verify the book, run `ctest --test-dir build -R classic_opening_book --output-on-failure`
after building with tests enabled. It replays the records and checks behavior
for missing, repeated, illegal and out-of-book positions.
