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
