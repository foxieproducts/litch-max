# Reproducing the shipped opening book

The [baseline sources](baseline/), [Python generator](generate_opening_book.py), [C++ driver](generate_opening_book.cpp) and [seed positions](generate_opening_book_seed.json) are the inputs for the [shipped opening book](../../../src/classic_chess_opening_book_data.inc). The engine used for generation is identified as `classic-full-v1`. Its search behavior is part of the input. The [provenance manifest](provenance.json) lists the original file paths, sizes and hashes.

1. Install python-chess in a local Python environment.
2. Copy [baseline.json](baseline.json) to a temporary directory and set its `root` field to the absolute path of the included `baseline/` directory.
3. Run `python3 generate_opening_book.py --help` from this directory. Supply the copied manifest with `--baseline-manifest` and choose a fresh artifact directory and output file with `--artifact-dir` and `--output`.
4. Compare the resulting records and positions with the shipped table. The manifest digest includes the machine-specific absolute root, so it changes when you regenerate locally.

Original Foxie code uses [MPL 2.0](../../../LICENSE). See [third-party notices](../../../THIRD_PARTY_NOTICES.md) for the included mcu-max and micro-Max terms. The parent [generator guide](../generate_opening_book.md) covers new books.
