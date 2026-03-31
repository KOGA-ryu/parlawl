# Edge Case Audit

Date: 2026-03-30

This audit is findings-first. It reflects the current tree after the recent live puzzle cache, live batch parsing, and opening propagation fixes.

## Top 10 Watchlist

1. Full move list can be visible while board review remains puzzle-centric after the puzzle start.
2. Source-game PGN hydration failure still degrades to partial history without a strong UI warning.
3. Source-game PGN cache is temp-backed convenience storage, not durable canonical storage.
4. `.env` token discovery still depends on launch cwd, which makes desktop launch behavior inconsistent.
5. Stockfish is still an external runtime dependency, not a bundled app dependency.
6. Worker/event packets can contain contradictory decisive-break fields and only some surfaces warn about it.
7. Assistant inference is stored next to evidence in the same persistence model, which risks canonical/inference confusion.
8. Lichess rate-limit coordination is centralized for batch fetches, but source-game PGN fetches are still outside that coordinator.
9. Report view and coach summary can drift if they resolve the same fact through different fallback orders.
10. The app still has multiple truth layers for source context: puzzle analysis seed, persisted source game row, PGN cache, and rendered summaries.

## Recurring Bug Patterns

- Upstream data exists, but one downstream surface uses a weaker fallback first.
- Fallbacks make the app keep working while hiding that something failed.
- Partial hydration is treated as “good enough” state instead of a visibly degraded state.
- One machine’s launch context leaks into product behavior.
- Runtime caches solve UX pain, but they start looking like canonical storage if not labeled carefully.
- Structured packet fields and human-readable summaries can drift unless both read from one shared resolver.

## Edge Cases

### 1. Source PGN exists but UI can still degrade to partial history
- Category: source data and propagation
- Severity: major
- Symptom: move list shows `start` plus a last-move stub instead of the full game, even though the puzzle lineage is known.
- Likely cause: the active puzzle lacks `analysisSeed.sourceGamePgn`, and `ensureCurrentPuzzleSourceHistory(...)` falls back silently when PGN hydration fails.
- Exact code areas:
  - [puzzle_runner_window.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_runner_window.cpp)
  - [puzzle_panels.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_panels.cpp)
  - [session_controller.cpp](/Users/kogaryu/dev/parlawl/libs/puzzle_runner/session_controller.cpp)
- How to reproduce:
  1. Load a puzzle whose source PGN is not already embedded.
  2. Make the `/game/export/...` fetch fail or clear the temp PGN cache.
  3. Open the move list.
- User-visible impact: the app looks like it “knows” the source game, but the visible history is only partial context.
- Detection method: check `Status / Log` for `source history unavailable: ...` while the move list still renders a plausible fallback.
- Recommended guardrail: add an explicit UI badge such as `partial source history` when PGN hydration is unavailable.
- Recommended test: UI/read-model test asserting that hydration failure sets an explicit degraded-state indicator.
- Status: open

### 2. Full source-game move list and board review cursor do not model the same timeline
- Category: move list / board review behavior
- Severity: major
- Symptom: move list shows the complete sourced game, but board review controls remain puzzle-review-centric after the puzzle start.
- Likely cause: move-list rendering uses full `sourceGamePgn`, while session/review state is still built around the puzzle start and subsequent solve path.
- Exact code areas:
  - [puzzle_panels.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_panels.cpp)
  - [game_state_store.cpp](/Users/kogaryu/dev/parlawl/libs/puzzle_runner/game_state_store.cpp)
  - [session_controller.cpp](/Users/kogaryu/dev/parlawl/libs/puzzle_runner/session_controller.cpp)
- How to reproduce:
  1. Load a live API puzzle with full `game.pgn`.
  2. Inspect the full move list.
  3. Try to interpret board stepping as if it matches the entire move list timeline.
- User-visible impact: the app can look desynced or “wrong” even though both surfaces are behaving as designed.
- Detection method: compare current review ply vs move-list highlight around the puzzle start boundary.
- Recommended guardrail: label the move list or review controls explicitly as `full source game` vs `active puzzle review`.
- Recommended test: interaction test around puzzle start showing that the board and highlighted row use a documented transition point.
- Status: open

### 3. Opening truth can drift between source PGN, persisted source game, and fallback family
- Category: source data and propagation
- Severity: moderate
- Symptom: one surface shows the real opening while another still shows `opening family candidate` or `unknown`.
- Likely cause: different surfaces resolve opening from different facts.
- Exact code areas:
  - [pgn_utils.cpp](/Users/kogaryu/dev/parlawl/libs/puzzle_runner/pgn_utils.cpp)
  - [puzzle_info_summary_builder.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/puzzle_info_summary_builder.cpp)
  - [report_formatter.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/report_formatter.cpp)
  - [session_controller.cpp](/Users/kogaryu/dev/parlawl/libs/puzzle_runner/session_controller.cpp)
- How to reproduce:
  1. Use a puzzle with PGN headers containing `Opening` and `ECO`.
  2. Compare Puzzle Info, report view, exported packet, and raw evidence text.
- User-visible impact: the app can look inconsistent even when the underlying source data is complete.
- Detection method: compare rendered opening across Puzzle Info, report formatter, and persisted/exported source-game fields.
- Recommended guardrail: keep one canonical opening resolver and route all human-facing surfaces through it.
- Recommended test: end-to-end test from hydrated PGN to persisted report summary.
- Status: partially fixed

### 4. Contradictory decisive-break fields can survive into the saved packet
- Category: evidence vs inference boundary
- Severity: major
- Symptom: packet says the played move and best move are the same, while the divergence summary still treats that played line as the collapse.
- Likely cause: event-level retained-break fields and move-level critical-move fields are normalized by separate logic paths.
- Exact code areas:
  - [worker_protocol.h](/Users/kogaryu/dev/parlawl/libs/orchestration/worker_protocol.h)
  - [report_formatter.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/report_formatter.cpp)
  - [puzzle_info_summary_builder.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/puzzle_info_summary_builder.cpp)
- How to reproduce:
  1. Open a report where `retainedBreakPlayedMove == retainedBreakBestMove`.
  2. Compare retained-break summary to critical-move rows.
- User-visible impact: trust in the analysis packet drops, and any coach summary risks sounding fabricated.
- Detection method: cross-check retained-break fields against the first decisive critical move.
- Recommended guardrail: reject or explicitly flag impossible retained-break combinations at save/import time.
- Recommended test: normalization test that fails when retained-break and move-row facts disagree.
- Status: partially fixed

### 5. Raw evidence and coach summary are separate layers that can disagree
- Category: evidence vs inference boundary
- Severity: moderate
- Symptom: Raw Evidence expander and six-section coach summary tell different stories for the same run.
- Likely cause: the summary is derived prose while raw evidence is assembled from individual packet fields, and they do not always share one resolver.
- Exact code areas:
  - [puzzle_info_summary_builder.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/puzzle_info_summary_builder.cpp)
  - [puzzle_panels.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_panels.cpp)
- How to reproduce:
  1. Load a packet with thin or contradictory fields.
  2. Compare the six-section summary with Raw Evidence text.
- User-visible impact: summary feels opinionated or “too certain” relative to the packet.
- Detection method: compare resolved opening / critical mistake / tactical theme in both views.
- Recommended guardrail: centralize all resolved display facts before rendering raw-summary and coach-summary views.
- Recommended test: snapshot test for summary plus raw evidence using a contradictory packet fixture.
- Status: open

### 6. Batch-fetch rate limiting is coordinated, but source-game PGN fetches are not
- Category: external dependency and network risk
- Severity: major
- Symptom: live puzzle supply behaves correctly under `429`, but source PGN hydration can still hit Lichess independently.
- Likely cause: `PuzzleSupplyCoordinator` governs batch fetches only; `ensureCurrentPuzzleSourceHistory(...)` creates a fresh `LichessClient` and fetches PGN directly.
- Exact code areas:
  - [puzzle_supply_coordinator.cpp](/Users/kogaryu/dev/parlawl/libs/lichess/puzzle_supply_coordinator.cpp)
  - [puzzle_runner_window.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_runner_window.cpp)
  - [lichess_client.cpp](/Users/kogaryu/dev/parlawl/libs/lichess/lichess_client.cpp)
- How to reproduce:
  1. Use live puzzle batches.
  2. Force PGN hydration misses for multiple puzzles.
  3. Observe source-game fetches happening outside the shared cooldown path.
- User-visible impact: rate-limit behavior is less predictable than the UI implies.
- Detection method: compare log lines for batch cooldown vs source-game fetch failures.
- Recommended guardrail: either fold source-game fetches into the same coordinator or surface them as a separate rate-limited dependency.
- Recommended test: simulated 429 on PGN fetch path with explicit UI/log behavior assertions.
- Status: open

### 7. Persisted live batches survive restart, but only if a token is still present
- Category: persistence and cache correctness
- Severity: moderate
- Symptom: a previously loaded live queue disappears on restart if the token field is unavailable, even though the cached batch exists.
- Likely cause: startup restore is gated by `usingLivePuzzleSupply()`.
- Exact code areas:
  - [puzzle_runner_window.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_runner_window.cpp)
  - [puzzle_supply_coordinator.cpp](/Users/kogaryu/dev/parlawl/libs/lichess/puzzle_supply_coordinator.cpp)
- How to reproduce:
  1. Load a live batch.
  2. Remove/clear token input or launch in a context where `.env` is not discovered.
  3. Restart the app.
- User-visible impact: persisted queue behavior feels inconsistent across launch contexts.
- Detection method: compare startup with and without token resolution after a cached live batch exists.
- Recommended guardrail: decide whether cached live batches are usable without a current token and enforce that rule explicitly in UI text.
- Recommended test: startup restore test with cached live batch and no token.
- Status: open

### 8. Source-game PGN cache is temp-backed and can be mistaken for durable storage
- Category: persistence and cache correctness
- Severity: moderate
- Symptom: source history appears stable until temp cleanup or OS eviction removes cached PGNs.
- Likely cause: `SourceGamePgnCache` writes to `QStandardPaths::TempLocation`, not app data.
- Exact code areas:
  - [source_game_pgn_cache.cpp](/Users/kogaryu/dev/parlawl/libs/lichess/source_game_pgn_cache.cpp)
- How to reproduce:
  1. Hydrate PGN for a source game.
  2. Clear temp files or use a new machine/session context.
  3. Reopen the same puzzle.
- User-visible impact: full source history seems reliable until it silently is not.
- Detection method: compare behavior before and after temp cache removal.
- Recommended guardrail: label PGN cache as convenience-only, or move it to app data if it must be treated as durable.
- Recommended test: startup behavior after clearing temp cache with a persisted puzzle queue.
- Status: open

### 9. `.env` token resolution depends on launch cwd
- Category: packaging and desktop-app seams
- Severity: major
- Symptom: token works in dev-terminal launch from repo root but fails in Finder/app-style launch.
- Likely cause: `.env` lookup uses `QDir::current().absoluteFilePath(".env")`, which depends on the current working directory.
- Exact code areas:
  - [puzzle_runner_window.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_runner_window.cpp)
- How to reproduce:
  1. Put `LICHESS_API_TOKEN` only in `.env`.
  2. Launch from repo root vs app bundle/Finder.
  3. Compare `Reload puzzles`.
- User-visible impact: the app appears flaky or machine-specific.
- Detection method: compare token detection across launch contexts.
- Recommended guardrail: prefer saved settings or real environment variables for runtime; treat `.env` as development-only.
- Recommended test: launch-context test or direct resolver unit test with cwd variation.
- Status: open

### 10. Stockfish discovery is improved but still machine-dependent
- Category: packaging and desktop-app seams
- Severity: major
- Symptom: desktop app opens, but engine review and analysis fail on a machine without `stockfish` on `PATH` or Homebrew path.
- Likely cause: Stockfish is discovered from `PATH`, then `/opt/homebrew/bin/stockfish`; nothing is bundled.
- Exact code areas:
  - [puzzle_runner_window.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_runner_window.cpp)
  - [stockfish_review_controller.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/stockfish_review_controller.cpp)
- How to reproduce:
  1. Launch on a machine without Stockfish installed.
  2. Open Engine Review or run analysis.
- User-visible impact: app looks launchable but major functionality is unavailable later.
- Detection method: invalid Stockfish path smoke test on a clean machine.
- Recommended guardrail: explicit first-run engine availability check and dedicated unavailable-state copy.
- Recommended test: UI/integration test for missing-engine startup and engine-review behavior.
- Status: open

### 11. Assistant inference is persisted inside the same tactical-event row as evidence
- Category: evidence vs inference boundary
- Severity: moderate
- Symptom: external assistant commentary can start to look like part of canonical extracted evidence.
- Likely cause: `assistant_inference_status`, labels, and markdown live on `tactical_events`.
- Exact code areas:
  - [analysis_repository.cpp](/Users/kogaryu/dev/parlawl/libs/storage/analysis_repository.cpp)
  - [worker_protocol.h](/Users/kogaryu/dev/parlawl/libs/orchestration/worker_protocol.h)
- How to reproduce:
  1. Import assistant inference.
  2. Reopen the run and compare raw evidence surfaces.
- User-visible impact: evidence/inference separation can become conceptual rather than enforced.
- Detection method: inspect saved report/export and see assistant fields beside evidence fields.
- Recommended guardrail: keep assistant data visually and structurally separate in exports and summaries.
- Recommended test: export/import roundtrip asserting assistant fields never replace objective evidence fields.
- Status: open

### 12. Worker protocol parsing is brittle to schema drift
- Category: persistence and cache correctness
- Severity: moderate
- Symptom: small worker payload changes can break desktop parsing hard.
- Likely cause: `requiredString(...)` is used for a large number of fields without versioned tolerant parsing.
- Exact code areas:
  - [worker_protocol.h](/Users/kogaryu/dev/parlawl/libs/orchestration/worker_protocol.h)
- How to reproduce:
  1. Omit or rename one worker response field.
  2. Parse the response in the desktop app.
- User-visible impact: analysis import can fail entirely instead of degrading gracefully.
- Detection method: fixture mutation tests against worker responses.
- Recommended guardrail: preserve strictness for critical ids, but tolerate optional derived fields with explicit defaults and warnings.
- Recommended test: schema-compat test with one older and one thinner worker payload.
- Status: open

### 13. Startup still initializes fixtures before restoring a cached live queue
- Category: UI state truthfulness
- Severity: low
- Symptom: startup path begins from fixtures and then swaps to cached live puzzles.
- Likely cause: `SessionController::initialize(...)` still loads fixtures, then the window restores the cached live batch.
- Exact code areas:
  - [session_controller.cpp](/Users/kogaryu/dev/parlawl/libs/puzzle_runner/session_controller.cpp)
  - [puzzle_runner_window.cpp](/Users/kogaryu/dev/parlawl/apps/desktop/puzzle_runner_window.cpp)
- How to reproduce:
  1. Have a cached live batch.
  2. Start the app and watch initial state/logs closely.
- User-visible impact: possible brief state flicker and unnecessary dual-source mental model.
- Detection method: startup trace or UI automation around first visible state.
- Recommended guardrail: support direct startup initialization from a provided queue source.
- Recommended test: startup integration test for cached-queue restore without fixture intermediate state.
- Status: partially fixed

### 14. Report view can still sound more certain than the underlying packet
- Category: UI state truthfulness
- Severity: moderate
- Symptom: summary/report phrasing can imply clear causality even when packet confidence is thin or contradictory.
- Likely cause: formatter text is more fluent than the packet’s actual certainty level.
- Exact code areas:
  - [report_formatter.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/report_formatter.cpp)
  - [puzzle_info_summary_builder.cpp](/Users/kogaryu/dev/parlawl/libs/reporting/puzzle_info_summary_builder.cpp)
- How to reproduce:
  1. Load a report with thin candidate evidence or contradictory break fields.
  2. Compare warnings vs fluent summary prose.
- User-visible impact: the app can “sound right” while the underlying evidence is shaky.
- Detection method: review summary language in runs with warnings.
- Recommended guardrail: bind stronger hedging language to confidence and warning conditions.
- Recommended test: snapshot test for contradictory packet surfaces using warning-aware wording.
- Status: open

## Fallbacks That Should Become Explicit Warnings

- Partial source history fallback in the move list.
- Cached live batch restore when remote fetch is unavailable.
- Missing `Opening` header falling back to `ECO` or `openingFamily`.
- Source-game PGN cache miss requiring live hydration.
- Engine unavailable or invalid path during review.
- Token discovered only via cwd-dependent `.env`.

## Invariants That Should Be Enforced In Code

- If `sourceGame.pgnText` exists and contains `Opening`, no human-facing opening field may resolve to `unknown`.
- If `retainedBreakPlayedMove == retainedBreakBestMove`, any decisive-collapse summary must be flagged as inconsistent.
- If a full source-game move list is rendered, the UI must explicitly indicate where puzzle review begins.
- A cached live batch must never silently disappear because a weaker startup source ran first.
- External assistant fields must never overwrite or reinterpret objective evidence fields.
- Any network path that can `429` should either share coordination or visibly declare that it does not.

## Tests Still Missing

- Startup restore test for cached live batch with no token present.
- UI/read-model test for explicit `partial source history` state.
- Integration test for source-game PGN hydration failure and warning surfacing.
- Startup/launch-context test for `.env` token discovery vs app-bundle launch.
- Missing-Stockfish UI/integration test on a clean environment.
- Worker schema-compat test with optional derived fields absent.
- Review/move-list synchronization test around puzzle-start boundary.

## Production Checks To Run Before Shipping

- Launch from the built `.app` bundle, not just terminal binary.
- Verify puzzle reload with token from saved settings, not only `.env`.
- Verify missing-Stockfish startup path and engine-review error copy.
- Verify one live batch load, app restart, cached queue restore, and next-puzzle continuity.
- Verify one source-history hydration miss shows an explicit degraded state.
- Verify one contradictory packet shows a visible warning in both report and coach-summary surfaces.
