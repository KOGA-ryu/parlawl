# parlawl

`parlawl` is a local Qt desktop chess workstation for puzzle solving, move-history review, Stockfish-backed review, and saved analysis reporting inside one unified app shell.

It can also open a read-only `annotated-game-replay-v1` export from the esports
evidence pipeline, scrub the independently legal-checked mainline, show supplied
derived annotations as unverified, and enter or leave legal-checked retained
alternatives without changing the real game.

The puzzle workspace can separately import an offline, strict v1 JSONL puzzle
pack whose records declare `engine_validated`. Loading it replaces the queue and
disables Lichess top-up for the session. ParlAWL checks the schema, content IDs,
known tactical cross-links, and legal move replay; it does not authenticate the
producer, rerun the engine, or prove optimality, uniqueness, or forced play.

## quick start

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt/lib/cmake -DPARLAWL_BUILD_TESTS=ON
cmake --build build
open -a /Users/kogaryu/dev/parlawl/build/apps/desktop/parlawl.app
```

## docs

- [docs/README.md](docs/README.md)
- [docs/BUILD.md](docs/BUILD.md)
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [docs/release_hardening_pass.md](docs/release_hardening_pass.md)
- [docs/final_pre_ship_verification.md](docs/final_pre_ship_verification.md)
- [docs/edge_case_audit.md](docs/edge_case_audit.md)
- [docs/annotated_replay_import.md](docs/annotated_replay_import.md)
- [docs/engine_validated_puzzle_import.md](docs/engine_validated_puzzle_import.md)
