# Quasar

**Quasar** is a dependency-free UCI chess engine written in C++20. It is a
compact but complete engine: a bitboard board representation, legal move
generation, a tapered handcrafted evaluation, an alpha-beta search with modern
pruning, a transposition table, time management, and a tested UCI interface.

The project is intended as a portfolio piece and as a readable base for
experimenting with chess programming. It builds with CMake and has no runtime
dependencies beyond the C++ standard library and the platform thread library.

## Highlights

- **Magic bitboards** for fast rook and bishop attacks, with precomputed magic
  constants and attack tables built at startup.
- **Complete legal move generation**, including castling, en passant and all
  four promotion pieces. The move maker supports incremental updates and
  rollback.
- **Perft-verified correctness** against all six standard perft positions. The
  test suite also checks magic attacks against slow ray generation on randomized
  occupancies.
- **Principal variation search** with iterative deepening, transposition-table
  cutoffs, null-move pruning, reverse futility pruning, late move reductions,
  killer/history move ordering, check extensions, and quiescence search with
  delta pruning.
- **Tapered evaluation** using material and piece-square tables, mobility, pawn
  structure (passed, isolated and doubled pawns), the bishop pair, rook files,
  king shelter and tempo. The tables are handcrafted rather than copied from
  another engine.
- **UCI interface** with an asynchronous search thread, `stop`, `ucinewgame`,
  `setoption name Hash`, clock-based time management, depth/node/movetime
  limits, and a search-info PV.
- **Built-in diagnostics**: perft, benchmark, position display, static
  evaluation, and an interactive console mode.
- **Portable**: GCC, Clang and MSVC are supported through the same CMake build.

## Build

Requirements:

- CMake 3.20 or newer
- A C++20 compiler
- Ninja, Make, or a Visual Studio generator

Linux / macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows with Visual Studio:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

With a single-config generator such as Ninja, the executable is
`build/quasar` (`build/quasar.exe` on Windows). With a multi-config generator,
it is under `build/Release/`.

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `QUASAR_BUILD_TESTS` | `ON` | Build the test executable and register it with CTest |
| `QUASAR_NATIVE` | `OFF` | Add `-march=native` for a machine-specific build |
| `QUASAR_SANITIZE` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang) |

Build the sanitizer configuration with:

```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DQUASAR_SANITIZE=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

## Playing

Quasar speaks the UCI protocol. Point any UCI-compatible GUI at the executable,
for example Cute Chess, Arena, or BanksiaGUI.

A minimal session:

```text
uci
isready
position startpos moves e2e4 e7e5 g1f3 b8c6
go depth 10
...
bestmove d2d4 ponder e5d4
quit
```

From the command line:

```bash
./build/quasar            # UCI mode
./build/quasar --repl     # interactive board and command prompt
./build/quasar bench 10   # built-in benchmark
./build/quasar perft 5    # perft from the start position
./build/quasar --help     # command-line help
```

The interactive console shows the board at startup and accepts the UCI commands
plus a few diagnostics:

| Command | Description |
| --- | --- |
| `d` | Display the current board, FEN and state |
| `eval` | Print the static evaluation from the side to move |
| `perft N` | Run perft from the current position |
| `bench [depth]` | Run the built-in benchmark (default depth 8) |
| `help` | Show the command list |
| `ucinewgame` | Reset the engine and the current position |
| `quit` | Exit |

## Verification

### Perft

All intermediate depths are checked, not only the deepest one. The numbers
below match the published reference values for the standard test positions:

| Position | Maximum depth | Nodes at maximum depth |
| --- | ---: | ---: |
| Start position | 5 | 4,865,609 |
| Kiwipete | 4 | 4,085,603 |
| Position 3 | 5 | 674,624 |
| Position 4 | 4 | 422,333 |
| Position 5 | 4 | 2,103,487 |
| Position 6 | 4 | 3,894,594 |

The test suite contains 16,000+ checks covering:

- perft for all six standard positions;
- magic rook/bishop attacks versus slow ray generation over randomized
  occupancies;
- FEN parsing and round-tripping;
- Zobrist hashing consistency across make/unmake;
- castling, en passant, promotions and special moves;
- checkmate, stalemate, mate scoring and basic tactical searches;
- evaluation bounds and side-to-move symmetry.

Run it with:

```bash
ctest --test-dir build --output-on-failure
```

### Benchmarks

Representative Release-build results on the development machine (GCC 16.2,
single thread, 64 MB hash, Windows):

| Workload | Nodes | Time | Approximate speed |
| --- | ---: | ---: | ---: |
| `bench 8` | 426,397 | ~0.4 s | ~1.0 MNPS |
| `bench 10` | 1,198,413 | ~1.5 s | ~0.8 MNPS |
| `perft 5` | 4,865,609 | ~0.3 s | ~16 MNPS |

Timings depend heavily on the compiler, CPU, and build options. Use
`QUASAR_NATIVE=ON` for a local machine-specific build.

## Strength

Quasar is a small, handcrafted engine, not a Stockfish competitor. Two
exploratory sanity matches were played at 100 ms per move with single-threaded
Stockfish using its strength limiter:

| Opponent setting | Games | Quasar score |
| --- | ---: | ---: |
| `UCI_Elo 1500` | 8 | 5.0/8 (62.5%) |
| `UCI_Elo 2000` | 8 | 4.0/8 (50.0%) |

These numbers are **not an official rating**. The sample is tiny, Stockfish's
limited-strength mode is approximate, and the openings were fixed. They are
useful only as evidence that the engine plays legal, coherent chess and that
nothing is obviously broken. A proper gauntlet against a pool of engines at
controlled time limits would be needed for a meaningful estimate.

No illegal moves were observed in the test matches.

## Architecture

```text
include/quasar/       public headers
src/
  bitboard.cpp        magic constants, attack tables, sliding attacks
  position.cpp        board state, FEN, make/unmake, repetition, legality
  movegen.cpp         pseudo-legal generation, legal filter, perft
  eval.cpp            tapered handcrafted evaluation
  search.cpp          iterative deepening PVS, pruning, quiescence, timing
  transposition.cpp   transposition table with mate-score adjustment
  uci.cpp             UCI loop, search thread, console commands
  bench.cpp           benchmark positions and reporting
  main.cpp            CLI entry point
tests/
  test_main.cpp       correctness and tactical regression tests
```

### Design notes

- **Representation.** Squares are numbered `a1 = 0` through `h8 = 63`.
  Bitboards are `std::uint64_t`; moves are packed into 16 bits; the
  transposition table stores 16-byte entries for cache locality.
- **Attack generation.** Rook and bishop attacks use masked occupancy and
  fixed magic multipliers. The 128 magic constants are embedded so engine
  startup stays fast; only the attack tables are rebuilt at startup.
- **Search.** The engine uses negamax with principal variation search. The
  transposition table stores depth, bound, best move and mate-adjusted scores.
  Null-move pruning, reverse futility pruning, late move reductions and
  quiescence search reduce the tree; check extensions keep forcing lines.
  Time management uses a soft limit to stop between iterations and a hard
  limit checked periodically during the search.
- **Evaluation.** Material and piece-square terms are tapered between
  middlegame and endgame values. Additional terms cover mobility, pawn
  structure, passed pawns, the bishop pair, rook files, king shelter and tempo.
- **Correctness.** Move generation is validated by perft, the attack tables are
  compared against a slow implementation, and incremental Zobrist keys are
  checked after every make and unmake in the hashing test.

## Limitations and roadmap

Current limitations:

- single-threaded search (no SMP);
- no pondering;
- handcrafted evaluation, no NNUE;
- no opening book or endgame tablebases;
- no MultiPV or `searchmoves` support;
- basic time management and no automated parameter tuning.

Possible next steps:

1. Add lazy SMP with a configurable `Threads` option.
2. Add an NNUE inference path and a small trained network.
3. Tune evaluation and search parameters with SPSA or CLOP.
4. Implement pondering, MultiPV and `searchmoves`.
5. Add optional Syzygy tablebase probing.
6. Build a larger regression suite and a proper engine gauntlet.

## Continuous integration

The GitHub Actions workflow in `.github/workflows/ci.yml` builds and tests the
engine on Linux, macOS and Windows, and runs a separate AddressSanitizer /
UndefinedBehaviorSanitizer job on Linux.

## License

MIT. See [LICENSE](LICENSE).
