#!/usr/bin/env python3
"""Generate Litch-owned opening records from a frozen baseline's complete Full roots.

Requires python-chess for an independent legality/state cross-check. Builds only
in the explicit artifact directory, with at most two compiler jobs. No production
source is swapped and no runtime search is performed by the generated module.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time

import chess


BASELINE_SOURCES = [
    "lib/mcu-max/src/mcu-max-position.c",
    "src/classic_chess_search.cpp",
    "src/classic_chess_move_order.cpp",
    "src/classic_chess_evaluation.cpp",
    "src/duck_chess_analysis.cpp",
    "src/duck_chess_rules.cpp",
    "src/duck_chess_rules_generation.cpp",
    "src/duck_chess_rules_position.cpp",
]
PROMOTIONS = {0: None, 3: chess.KNIGHT, 5: chess.BISHOP, 6: chess.ROOK, 7: chess.QUEEN}


def Sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def WriteJson(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def Square88(square: int) -> int:
    return (7 - chess.square_rank(square)) * 16 + chess.square_file(square)


def PackedMove(move: chess.Move) -> int:
    promotion = {None: 0, chess.KNIGHT: 3, chess.BISHOP: 5, chess.ROOK: 6, chess.QUEEN: 7}[move.promotion]
    source = (7 - chess.square_rank(move.from_square)) * 8 + chess.square_file(move.from_square)
    target = (7 - chess.square_rank(move.to_square)) * 8 + chess.square_file(move.to_square)
    return source | (target << 6) | (promotion << 12)


def From88(square: int) -> int:
    if square < 0 or square >= 128 or square & 0x88:
        raise ValueError(f"invalid engine square: {square}")
    return chess.square(square & 7, 7 - (square >> 4))


def Prefixes(seed: dict) -> tuple[list[dict], list[list[chess.Move]]]:
    if seed["schema"] != 1 or seed["max_plies"] != 16 or seed["offline_full_depth"] < 5:
        raise ValueError("unsupported opening seed schema/depth")
    roots: list[dict] = []
    lines: list[list[chess.Move]] = []
    seen: set[tuple] = set()
    for line_id, line in enumerate(seed["lines"]):
        moves = [chess.Move.from_uci(uci) for uci in line["moves"].split()]
        if not moves or len(moves) > seed["max_plies"]:
            raise ValueError(f"unbounded/empty line: {line['name']}")
        lines.append(moves)
        board = chess.Board()
        repeated: set[str] = set()
        for ply in range(len(moves) + 1):
            fen = board.fen(en_passant="fen")
            identity = " ".join(board.fen(en_passant="legal").split()[:4])
            if identity in repeated:
                raise ValueError(f"repeated position in seed: {line['name']} at {ply}")
            repeated.add(identity)
            # Keep ineffective EP variants separate: runtime exact replay is strict.
            signature = (tuple(fen.split()[:4]), ply)
            if signature not in seen:
                roots.append({"line": line_id, "ply": ply, "prefix": [m.uci() for m in moves[:ply]],
                              "fen": fen, "family": line["family"], "name": line["name"]})
                seen.add(signature)
            if ply < len(moves):
                if moves[ply] not in board.legal_moves:
                    raise ValueError(f"illegal seed: {line['name']} ply {ply + 1}: {moves[ply]}")
                board.push(moves[ply])
    if len(roots) > 1024 or len(lines) > 65535:
        raise ValueError("opening campaign exceeded the intended compact bounds")
    return roots, lines


def CheckResult(root: dict, result: dict, depth: int) -> chess.Move:
    board = chess.Board(root["fen"])
    if not (result["available"] and result["legal"] and result["restored"] and
            not result["preliminary"] and not result["interrupted"] and result["depth"] >= depth):
        raise ValueError(f"incomplete Full search: {result}")
    expected_board = "".join(board.piece_at(chess.square(file, rank)).symbol()
                             if board.piece_at(chess.square(file, rank)) else "."
                             for rank in range(7, -1, -1) for file in range(8))
    rights = (int(board.has_kingside_castling_rights(chess.WHITE)) |
              int(board.has_queenside_castling_rights(chess.WHITE)) << 1 |
              int(board.has_kingside_castling_rights(chess.BLACK)) << 2 |
              int(board.has_queenside_castling_rights(chess.BLACK)) << 3)
    ep = 128 if board.ep_square is None else Square88(board.ep_square)
    if (result["board"] != expected_board or result["side"] != (8 if board.turn else 16) or
            result["castling"] != rights or result["ep"] != ep):
        raise ValueError(f"baseline/independent position mismatch: {root}")
    move = chess.Move(From88(result["from"]), From88(result["to"]), PROMOTIONS[result["promotion"]])
    if move not in board.legal_moves:
        raise ValueError(f"independent rules reject stored move {move}: {root}")
    if not -32768 <= result["score"] <= 32767:
        raise ValueError("offline score does not fit the record")
    return move


def Emit(path: Path, records: list[dict], lines: list[list[chess.Move]], provenance: dict) -> None:
    offsets = [0]
    packed = []
    for line in lines:
        packed.extend(PackedMove(move) for move in line)
        offsets.append(len(packed))
    if len(packed) > 65535:
        raise ValueError("opening line pool overflow")
    output = ["// Generated by tools/engine_knowledge/generate_opening_book.py; do not edit.\n",
              "// Own reachability lines; baseline completed Full search chooses every move.\n",
              f"constexpr size_t BOOK_ENTRY_COUNT = {len(records)}U;\n",
              f"constexpr size_t BOOK_LINE_COUNT = {len(lines)}U;\n",
              "constexpr BookRecord BOOK_RECORDS[]{\n"]
    for record in records:
        output.append(f"    {{0x{record['key']:016x}ULL, {record['score']}, "
                      f"0x{record['packed_move']:04x}U, {record['line']}U, "
                      f"{record['depth']}U, {record['ply']}U}},\n")
    output.append("};\nconstexpr uint16_t BOOK_LINE_OFFSETS[]{\n    " +
                  ", ".join(f"{offset}U" for offset in offsets) + "\n};\n")
    output.append("constexpr uint16_t BOOK_LINE_MOVES[]{\n")
    for start in range(0, len(packed), 12):
        output.append("    " + ", ".join(f"0x{move:04x}U" for move in packed[start:start + 12]) + ",\n")
    output.append("};\n")
    fields = [("BOOK_BASELINE", provenance["baseline_identity"]),
              ("BOOK_BASELINE_INPUTS_SHA256", provenance["baseline_manifest_sha256"]),
              ("BOOK_SEED_SHA256", provenance["seed_sha256"]),
              ("BOOK_RECORDS_SHA256", provenance["records_sha256"])]
    for name, value in fields:
        output.append(f"constexpr char {name}[] = {json.dumps(value)};\n")
    output.append("constexpr OpeningBookMetadata BOOK_METADATA{\n"
                  "    BOOK_BASELINE, BOOK_BASELINE_INPUTS_SHA256, BOOK_SEED_SHA256, BOOK_RECORDS_SHA256,\n"
                  "    BOOK_ENTRY_COUNT, BOOK_LINE_COUNT,\n"
                  "    sizeof(BOOK_RECORDS) + sizeof(BOOK_LINE_OFFSETS) + sizeof(BOOK_LINE_MOVES) +\n"
                  "    sizeof(BOOK_BASELINE) + sizeof(BOOK_BASELINE_INPUTS_SHA256) +\n"
                  "    sizeof(BOOK_SEED_SHA256) + sizeof(BOOK_RECORDS_SHA256) + sizeof(OpeningBookMetadata)\n"
                  "};\n")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(output))


def Main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-manifest", type=Path, required=True)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--seed", type=Path, default=Path(__file__).with_name("generate_opening_book_seed.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2, choices=(1, 2))
    parser.add_argument("--resume", action="store_true",
                        help="Finish packaging already-complete results after exact driver/seed/request checks")
    args = parser.parse_args()
    started = time.monotonic()
    manifest = json.loads(args.baseline_manifest.read_text())
    baseline = Path(manifest["root"])
    for relative, expected in manifest["files"].items():
        if Sha(baseline / relative) != expected:
            raise ValueError(f"frozen baseline mismatch: {relative}")
    seed = json.loads(args.seed.read_text())
    roots, lines = Prefixes(seed)
    depth = seed["offline_full_depth"]
    artifact = args.artifact_dir.resolve()
    artifact.mkdir(parents=True, exist_ok=args.resume)
    driver = artifact / "generate_opening_book.cpp"
    if args.resume:
        if Sha(driver) != Sha(Path(__file__).with_suffix(".cpp")) or Sha(artifact / "seed.json") != Sha(args.seed):
            raise ValueError("resume driver/seed differs from the completed campaign")
    else:
        shutil.copyfile(Path(__file__).with_suffix(".cpp"), driver)
        shutil.copyfile(args.seed, artifact / "seed.json")
    sources = "\n".join(f'  "{baseline / source}"' for source in BASELINE_SOURCES)
    includes = "\n".join(f'  "{baseline / relative}"' for relative in
                         ("include", "lib/mcu-max/src", "lib/foxie_embedded/include", "lib/foxie_embedded/src"))
    cmake = ("cmake_minimum_required(VERSION 3.20)\nproject(opening_book_generator LANGUAGES C CXX)\n"
             "set(CMAKE_C_STANDARD 11)\nset(CMAKE_CXX_STANDARD 17)\n"
             f'add_executable(opening_book_generator "{driver}"\n{sources}\n)\n'
             f"target_include_directories(opening_book_generator PRIVATE\n{includes}\n)\n")
    if args.resume:
        if (artifact / "CMakeLists.txt").read_text() != cmake:
            raise ValueError("resume baseline/build inputs differ")
    else:
        (artifact / "CMakeLists.txt").write_text(cmake)
    commands = [["cmake", "-S", str(artifact), "-B", str(artifact / "build"), "-DCMAKE_BUILD_TYPE=Release"],
                ["cmake", "--build", str(artifact / "build"), "--parallel", str(args.jobs)]]
    if not args.resume:
        with (artifact / "build.log").open("w") as log:
            for command in commands:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = artifact / "build/opening_book_generator"
    requests = "".join(f"{index} {root['line']} {root['ply']} " + " ".join(root["prefix"]) + "\n"
                       for index, root in enumerate(roots))
    if args.resume:
        if (artifact / "requests.txt").read_text() != requests:
            raise ValueError("resume requests differ from the completed campaign")
    else:
        (artifact / "requests.txt").write_text(requests)
    print(f"Validating {len(roots)} unique roots from {len(lines)} lines at complete Full{depth}", flush=True)
    if not args.resume:
        with (artifact / "search-results.jsonl").open("w") as output, (artifact / "search.log").open("w") as log:
            subprocess.run([str(binary), str(depth)], input=requests, text=True, stdout=output, stderr=log, check=True)
    results = [json.loads(line) for line in (artifact / "search-results.jsonl").read_text().splitlines()]
    if len(results) != len(roots):
        raise ValueError("incomplete offline campaign")
    records = []
    for index, (root, result) in enumerate(zip(roots, results)):
        if (result["index"], result["line"], result["ply"]) != (index, root["line"], root["ply"]):
            raise ValueError("root/result ordering mismatch")
        move = CheckResult(root, result, depth)
        records.append({"key": int(result["key"], 16), "score": result["score"], "packed_move": PackedMove(move),
                        "line": root["line"], "depth": result["depth"], "ply": root["ply"], "move": move.uci(),
                        "nodes": result["nodes"], "fen": root["fen"], "prefix": root["prefix"],
                        "family": root["family"], "name": root["name"]})
    records.sort(key=lambda record: (record["key"], record["ply"], record["line"]))
    raw = b"".join(struct.pack("<QhHHBB", record["key"], record["score"], record["packed_move"],
                              record["line"], record["depth"], record["ply"]) for record in records)
    (artifact / "records.bin").write_bytes(raw)
    provenance = {"baseline_identity": f"classic-full-v1:{manifest['head']}:depth{depth}",
                  "baseline_manifest_sha256": Sha(args.baseline_manifest), "seed_sha256": Sha(args.seed),
                  "records_sha256": hashlib.sha256(raw).hexdigest()}
    Emit(args.output, records, lines, provenance)
    WriteJson(artifact / "validated-records.json", records)
    families = sorted({line["family"] for line in seed["lines"]})
    receipt = {"status": "PASS_COMPLETE_OFFLINE_FULL", **provenance, "root_count": len(records),
               "source_line_count": len(lines), "families": families, "offline_depth": depth,
               "offline_stage": "Full", "offline_nodes": sum(record["nodes"] for record in records),
               "independent_legality": f"python-chess {chess.__version__}", "record_bytes": len(raw),
               "line_move_bytes": sum(len(line) for line in lines) * 2, "line_offset_bytes": (len(lines) + 1) * 2,
               "generated_source_sha256": Sha(args.output), "driver_source_sha256": Sha(driver),
               "generator_source_sha256": Sha(Path(__file__)), "binary_sha256": Sha(binary),
               "search_results_sha256": Sha(artifact / "search-results.jsonl"),
               "commands": commands + [[str(binary), str(depth)]], "elapsed_seconds": time.monotonic() - started,
               "elapsed_scope": "packaging only" if args.resume else "build, search and packaging",
               "packaging_resumed": args.resume,
               "qualification": "Offline original-engine Full depth is provenance, not live search depth or a strength claim."}
    for relative, expected in manifest["files"].items():
        if Sha(baseline / relative) != expected:
            raise ValueError(f"baseline changed during generation: {relative}")
    WriteJson(artifact / "receipt.json", receipt)
    print(json.dumps({"receipt": str(artifact / "receipt.json"), "roots": len(records), "record_bytes": len(raw)}))


if __name__ == "__main__":
    Main()
