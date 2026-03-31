# parlawl

`parlawl` is a local Qt desktop chess workstation for puzzle solving, move-history review, Stockfish-backed review, and saved analysis reporting inside one unified app shell.

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
