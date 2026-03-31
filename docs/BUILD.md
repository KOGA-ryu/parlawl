# build

Requirements:

- CMake 3.24+
- C++20 compiler
- Qt 6 with `Core`, `Widgets`, `Sql`, `Network`, `Test`
- Python 3
- `python-chess`
- Stockfish

Install Qt on macOS with Homebrew if needed:

```bash
brew install qt
brew install stockfish
```

Create a local Python environment for the worker:

```bash
python3 -m venv .venv
./.venv/bin/pip install python-chess
```

Configure and build:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt/lib/cmake -DPARLAWL_BUILD_TESTS=ON
cmake --build build
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Launch the desktop app:

```bash
open -a /Users/kogaryu/dev/parlawl/build/apps/desktop/parlawl.app
```

Or run the bundle executable directly:

```bash
/Users/kogaryu/dev/parlawl/build/apps/desktop/parlawl.app/Contents/MacOS/parlawl
```

Runtime notes:

- the app launches into one unified puzzle-and-analysis desktop shell
- `Analyze` operates on the current trainer puzzle
- the app defaults the Python worker launcher to `./.venv/bin/python3` when that path exists
- the app first tries to discover `stockfish` on the current `PATH`, then falls back to `/opt/homebrew/bin/stockfish`
- the app defaults the database path to the Qt app data directory
- if the preferred app data directory is not writable, the app falls back to `QDir::tempPath()/parlawl/parlawl.sqlite3`
- live Lichess batch loading is explicit through `Reload puzzles` and respects a shared 60-second cooldown after `429`
- live Lichess puzzle batches and the active rate-limit cooldown are cached under the app data directory so they survive restart
- if a cached live batch exists, the app restores that queue on launch instead of dropping back to fixtures
- the current engine slice uses depth `10` and a puzzle window of `4` plies before and `4` plies after the mapped puzzle start
- the worker currently requests `multipv=3` to support ranked local candidate comparison
- `ctest` also runs a small Python worker-logic suite against the mate-aware comparison helpers in `workers/analysis_py/test_worker_logic.py`
- the current coherence pass also tests adjacent-ply role-selection and suppression rules in that Python worker-logic suite
- the current retained-break formatting pass also tests that `retained_break_summary` is derived from normalized structured retained-break fields
- the current event-summary normalization pass also tests `collapse_sequence_summary` and omission-reason normalization from structured local-window fields
- the current move-level normalization pass also tests structured critical-reason selection and compact continuation formatting
- the current ranked-alternative normalization pass also tests structured candidate-ranking and evidence-note selection
- the current candidate-display pass also tests compact candidate-entry formatting and report rendering for derived inspection views
- the current structural-evidence pass also tests deterministic king exposure, loose-piece, overload, luft, and pressure extraction helpers
- the current local-target pass also tests deterministic king-zone target, vulnerable-piece target, pressure-lane target, decisive-imbalance target, and local-target summary derivation helpers
- the current move-level structural-link pass also tests deterministic derivation of compact `critical_move` links back to retained event-level local targets
- the current structural-evidence v2 pass also tests deterministic pinned-piece, defender-removal exposure, king color-complex, target-zone imbalance, and derived `structural_v2_summary` helpers
- the current structural-evidence v3 pass also tests deterministic escape-geometry, flight-control, defensive escape fragility, and derived `structural_v3_summary` helpers
- the current structural-evidence v4 pass also tests deterministic attacker-coordination, defensive-network fragility, and derived `structural_v4_summary` helpers

Packaging notes:

- the desktop target is built as a macOS bundle target through Qt/CMake
- generated exports, build outputs, sqlite sidecars, Python cache artifacts, and local logs should not be committed
