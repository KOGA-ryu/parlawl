# architecture

## runtime shape

1. the unified Qt desktop shell opens directly into the puzzle workstation
2. the left column owns board-first puzzle work:
   - board and evaluation bar
   - puzzle info / settings / engine review / analysis config tabs
3. the right column owns review and analysis surfaces:
   - move list
   - report view
   - status / log
   - recent runs
4. `libs/puzzle_runner/` owns solve-state truth, history stepping, and strict puzzle validation
5. `libs/lichess/` owns live Lichess fetches, source-game PGN caching, and live-supply coordination
6. `libs/orchestration/` owns full analysis execution against the Python worker
7. `libs/storage/` owns SQLite bootstrap, migrations, and repository operations
8. `libs/reporting/` owns human-readable report and puzzle-info summary formatting
9. `workers/analysis_py/` owns bounded Stockfish-backed evidence extraction and tactical event generation

## puzzle supply

Current puzzle supply modes:

- local fixture puzzles for deterministic smoke/testing
- explicit live Lichess batch loading through `Reload puzzles`

Live supply rules:

- one shared coordinator owns live puzzle requests
- live requests are explicit and rate-limited
- `429` starts a 60-second cooldown
- fetched live batches are cached in app data and survive restart
- cached live batches may be reused during cooldown and across sessions
- background top-up is suppressed during cooldown

## source-game history

- fixture and live Lichess puzzles already carry full source PGN
- hydrated PGN is cached locally and reused for move-list / review reconstruction
- move-history and board review share the same session/controller truth

## analysis boundary

- Parlawl extracts and stores evidence
- Parlawl does not claim behavioral truth about why a human missed a move
- assistant-side inference stays external and separate from worker evidence
- Stockfish puzzle solving is not part of strict validation
- Stockfish review and full analysis are supporting surfaces inside the same shell
