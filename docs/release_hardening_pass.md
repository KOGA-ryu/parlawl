# release hardening pass

## removed

- dead desktop UI code paths that no longer participate in the unified shell:
  - `PromptPanel`
  - no-op metadata navigation helper
- generated local artifacts that should not live in the repo:
  - `assistant_packets/`
  - Python `__pycache__/`

## reorganized

- source-game PGN parsing is centralized in `libs/puzzle_runner/pgn_utils.*`
- local source-game PGN caching lives in `libs/lichess/source_game_pgn_cache.*`

## hardened

- desktop target is built as a macOS bundle target
- `stockfish` default path now prefers system discovery before the Homebrew fallback
- `.gitignore` now covers generated packets, sqlite sidecars, logs, app bundles, and Python cache output
- build/startup docs now reflect the unified shell and explicit live reload behavior
- local source-game history hydration is cached locally and covered by tests

## risks left

- the PGN cache is temp-backed for portability and test stability, so it is a convenience cache rather than durable canonical storage
- the move list can display the complete sourced game while the board review cursor remains puzzle-review-centric after the puzzle start

## intentionally deferred

- persistent SQLite-backed source-game PGN cache
- richer packaging assets like icons and notarization-specific bundle metadata
- deeper move-list/board unification beyond the existing puzzle-review cursor contract
