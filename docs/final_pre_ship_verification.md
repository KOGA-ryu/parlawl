# Final Pre-Ship Verification

Date: 2026-03-30

## Verified

- Desktop build succeeds from the current tree with:
  - `cmake --build build`
- Full automated test suite passes:
  - `19/19` tests passed
- The desktop app launches cleanly from the built binary:
  - `open -a /Users/kogaryu/dev/parlawl/build/apps/desktop/parlawl.app`
  - or `/Users/kogaryu/dev/parlawl/build/apps/desktop/parlawl.app/Contents/MacOS/parlawl`
- SQLite initialization and settings load are clean at startup.
- The unified shell remains the only desktop surface.
- Live Lichess batch loading now uses a shared coordinator with:
  - single-flight behavior
  - 60-second cooldown after `429`
  - cached fallback
  - no repeated hidden retry spam
- Engine review, report view, recent runs, and status/log remain wired into the unified shell.

## Runtime Reality

- Full source-game move history for live Lichess puzzles comes directly from `game.pgn` in the batch payload.
- Fixture puzzles continue to use embedded PGN data for complete source-game history.

## Remaining Risks

- Finder-style bundle launch is improved by `MACOSX_BUNDLE`, but this is still local-build packaging, not a full distribution/notarization pass.
- `.env` token discovery is still a development convenience and depends on launch context. Saved settings and environment variables are the more reliable runtime paths.
- The move list can show fuller source-game context than the board review cursor can navigate after the puzzle start. That is acceptable for now, but it is still a UX seam to watch.

## Deferred On Purpose

- Durable persisted PGN cache beyond the current temp-backed convenience cache
- Distribution packaging work beyond local bundle correctness
