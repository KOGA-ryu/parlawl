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
10. `libs/strict_json/` owns the Python-compatible canonical-JSON core, the strict
    bounded parser, and the generic field checkers shared by every strict import
    boundary. It is domain-free and is linked by both `libs/puzzle_runner/` and
    `libs/market/`; the engine-line importer calls it rather than carrying its
    own copy
11. `libs/market/` owns the market-rep domain: bars, task and ply types, the four
    recomputed HUD derivations, per-ply and calibration scoring, the trade-line
    replay, the solver rating update, the `market-puzzle-pack-v1` import
    boundary, the sealed continuation vault, and the market queue and solve
    lifecycle. It links Qt Core only, like `libs/domain/`

## annotated replay boundary

- `libs/puzzle_runner/annotated_replay_pack.*` validates the exact, bounded
  `annotated-game-replay-v1` presentation shape and independently replays its
  UCI/FEN chains; supplied facts, narration, engine metadata, and semantic IDs
  are preserved but are not recomputed or authenticated by ParlAWL
- `libs/puzzle_runner/replay_session.*` owns an immutable mainline cursor and
  one disposable supplied engine branch with independently checked legality
  and exact restoration
- the desktop has an explicit annotated-replay mode that stops fresh engine
  review, hides non-replay tabs and controls, and guards puzzle input, live
  supply, settings, analysis execution, and database/report handlers
- recorded alternatives remain review lines; they are never promoted into
  `PuzzleDefinition` without a separate imported puzzle record whose producer
  declares `engine_validated`

## market workspace and the look-ahead law

`libs/market/market_puzzle_pack.*` is the market import boundary. It rides the
same discipline as the engine-line importer — bounded framing (64 MiB per file,
1 MiB per line, 100,000 records, strict UTF-8 without BOM or NUL), recursive
duplicate-key rejection, exact field sets, Python-compatible canonical
semantic-ID recomputation, and `canonical_json(parse(line)) == line` for every
line — with a consumer profile capping bars at 512, HUD stats at 64, plies at 32,
themes at 16, nesting at 48 levels and integers at `2^53 - 1`.

Two things are new relative to the chess boundary:

- **Both files or neither.** A pack is `<name>.visible.jsonl` plus
  `<name>.sealed.jsonl`. A visible file whose sealed partner is missing,
  mismatched or short one record does not load, and the queue is untouched.
  There is no blitz-only mode that runs without the key.
- **The look-ahead law is enforced by the type system, not by convention.** A
  puzzle's visible window ends at its decision time T. Segregation is three
  layers deep:
  1. *File* — the continuation is not in the record the loader hands the UI.
  2. *Type* — the loader produces `MarketPuzzleVisible`, which has no
     continuation member at any depth, and a `SealedContinuationVault` whose
     header cannot put `MarketContinuation` into scope (it is behind an opaque
     `Impl`). No translation unit under `apps/desktop/` includes
     `market_continuation.h`, `market_grader.h` or
     `sealed_continuation_vault_p.h`, and a source-scan test fails the suite if
     one ever does.
  3. *Ticket* — `RevealTicket` has a private constructor whose only friend is
     `MarketAttemptRepository`, which mints one only from a committed,
     exportable terminal event. The vault re-asks the journal to confirm the
     ticket's attempt and terminal event hash before it opens, so the guard is
     testable at runtime and not only at compile time.

The continuation leaves the vault only as a `MarketReveal`: identity, the bars
after T, the citation chain's identifying half, and the declared rule's line
rendered as text beside the sentence that says what the line is not.

Blitz and study differ in exactly two ways: study has no per-ply deadline and
allows post-reveal navigation. Neither mode has a peek, and there is no setting
that adds one. Zero context while deciding, history lesson after.

The four `verified_stats` (`atr_pct_20`, `range_position_20`, `return_5`,
`return_20`) are recomputed by ParlAWL from the visible bars and must agree to
1e-6, because the HUD is what the operator decides on. Unknown `stat_id`,
unknown theme, a theme that is not `knowable_at_t`, a rating seed outside the
declared band, a rating basis outside the permitted set, a varying response
horizon, and a ply shape that disagrees with its task kind are each a rejection
rather than pass-through.

A pack is training material, not evidence. Its scoring key is a declared rule's
line replayed on the continuation — not the right answer, not the optimal
answer, and not evidence that the rule has an edge. Every surface that shows a
key shows that sentence with it.

## market solve journal

`schemas/024_market_solve_journal_v1.sql` adds `market_attempt_puzzle_records`,
`market_solve_attempt_instances`, `market_solve_attempt_events` and
`market_solve_attempt_terminal_records` beside the `023` chess ledger rather
than widening it: SQLite cannot alter a CHECK in place, and rebuilding an
append-only ledger would destroy the guarantee its triggers exist to give. Every
`023` trigger has a `024` counterpart. Event kinds are the market set, and
`reveal_opened` is a review event admitted only after a terminal.

`libs/storage/append_only_export.*` is the extracted unit both repositories
call: the `openat`/`O_NOFOLLOW` directory walk, the `O_CREAT|O_EXCL` /
`0600` / `fsync` / `linkat` publication dance, the canonical encoder, the
semantic-ID helper and the Python UTC spelling. It was extracted rather than
copied; the chess repository was refactored onto it in the same change, so
`test_unit_puzzle_attempt_repository` exercises both paths.

Scoring is deliberately not part of the terminal record. A score is a function
of the sealed key, and the key does not open until the terminal is committed, so
the terminal event carries the raw answers and the display-integrity block while
the graded half arrives with `reveal_opened`; **Export Market Solve History**
joins them and computes `result_id` over the whole record. An ungraded rep
exports as ungraded rather than as zeros that would read as a perfect miss.
Only `completed` and `timed_out` terminals export. A timed-out rep is kept and
counted — dropping the reps where the operator froze is the most flattering
possible selection bias.

`parlawl-attempt-clock-v1` applies unchanged and matters more here, because
latency is the headline measurement: a backward wall-clock movement or drift
above 5,000 ms invalidates the rep locally and excludes it from export.

## puzzle supply

Current puzzle supply modes:

- local fixture puzzles for deterministic smoke/testing
- explicit live Lichess batch loading through `Reload puzzles`
- explicit offline v1 JSONL loading through `Import Engine-Line Pack` for records
  that declare `engine_validated`

Live supply rules:

- one shared coordinator owns live puzzle requests
- live requests are explicit and rate-limited
- `429` starts a 60-second cooldown
- fetched live batches are cached in app data and survive restart
- cached live batches may be reused during cooldown and across sessions
- background top-up is suppressed during cooldown
- importing an engine-line pack checks every record before one atomic in-memory
  queue replacement and disables remote top-up until an explicit live reload

`libs/puzzle_runner/engine_validated_puzzle_pack.*` is the import boundary. It
applies bounded strict JSON parsing, exact nested field/type checks,
Python-compatible canonical semantic-ID recomputation, known tactical-profile
cross-link checks, and independent legal solution replay. Its stricter consumer
profile caps solutions at 1,024 plies, themes at 256, evidence and maps at 512
entries, nesting at 48 levels, and integers at `2^53 - 1`. These checks establish
internal consistency, not producer authenticity or engine optimality. Provider
and hydration fields remain attached to the runtime puzzle so non-Lichess source
IDs cannot trigger Lichess PGN fetches.

Imported pack provenance is session-only. The selected JSONL remains the
authority; importing does not persist a pack copy, database provenance row, or
puzzle-attempt row merely because the file was opened. When a solve attempt
actually begins, the attempt journal may retain the exact canonical imported
record needed to bind and later verify that attempt. This retained record is not
a replacement for the pack and cannot restore the imported queue after restart.
The runtime record must therefore be described as a producer-declared engine
line, never as ParlAWL-authenticated engine evidence or proof of optimality,
uniqueness, or forced play.

## solve-attempt journal and publication boundary

`libs/storage/` owns a local append-only SQLite solve journal for exact imported
engine-line puzzles. On the first solve interaction it retains the exact source
record, then records a distinct attempt
instance and its ordered, hash-linked events. Terminal publication records are
also append-only. Database triggers reject update, delete, replacement, missing
parent records, sequence gaps, broken previous hashes, and duplicate terminal
records. These identities and chains establish internal consistency; they do
not authenticate the player, the producer, or the machine that wrote them.

One user action is appended as one database transaction before its corresponding
runtime transition or automatic navigation is exposed. A transaction therefore
commits completely or not at all. An abrupt process or machine failure can
still leave an otherwise valid attempt open. ParlAWL preserves that open state
as incomplete evidence: it does not infer a failure or abandonment after a
crash, and an open attempt is not eligible for publication.

`parlawl-attempt-clock-v1` compares wall time since exposure with the monotonic
elapsed timer. Any backward wall movement or an absolute difference over 5,000
ms invalidates the attempt locally and makes it ineligible for publication. The
one-millisecond start adjustment that disambiguates same-tick retries is not a
clock anomaly.

Only an attempt that is terminal as solved or failed and is bound to the exact
retained record from an imported engine-line pack can be materialized as an
`esports-probability-lab/puzzle-attempt/v1` record. Local fixtures and live
Lichess puzzles are not admitted to this journal. Open, abandoned, invalid, and
crash-interrupted imported attempts stay local and are excluded from the v1
export. Review navigation does not create a new solve attempt. Entering an
annotated replay synchronously abandons an active attempt before switching
modes because v1 has no pause/resume state; returning resets the puzzle and
starts a fresh exposure baseline without writing until the next interaction.

The Settings action **Export Solve History** is an explicit local
publication step. Publication reserves a new regular destination with
owner-only `0600` permissions and refuses an existing destination, including a
symlink, rather than overwriting it. A failed write must not leave a successful
looking export. Nothing is uploaded, submitted to a model, or used for training
automatically. The original imported JSONL pack remains the puzzle-record
authority even when an exact record has been retained beside an attempt.

The solver and app-session identifiers are opaque pseudonyms, not anonymity.
The solver pseudonym persists in local `QSettings`; one new session pseudonym is
shared across every attempt and retry in a single app run. These fields never
contain an email, name, device identifier, or path, but their stability makes
them linkable. Retained canonical puzzle records may include public player/game
provenance from the selected pack. The exported attempt rows include only the
source content IDs and solve facts, never the raw source record.

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
