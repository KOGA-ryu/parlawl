# offline engine-line puzzle import

ParlAWL can load an offline JSONL pack produced by the esports probability-lab
`esports-probability-lab/puzzle-candidate/v1` contract. In **Settings**, choose
**Import Engine-Line Pack** and select a direct regular `.jsonl` file.

## accepted records

The importer accepts only exact v1 `puzzle_candidate` records that declare the
status `engine_validated`. That status is a producer claim, not authentication.
Before the current queue changes, the complete file must pass all of these
checks:

- 64 MiB pack, 1 MiB line, 100,000-record, and nesting limits
- strict UTF-8 JSON with recursive duplicate-key rejection
- exact required fields and types at every contract object
- canonical JSON compatible with Python `json.dumps(..., ensure_ascii=False,
  sort_keys=True, separators=(",", ":"))`, including Python float spelling
- recomputed `puzzle_id` and `record_id` SHA-256 semantic identities
- normalized FEN, side-to-move agreement, and a complete legal replay of every
  UCI ply in `solution_uci`
- engine and validation evidence required by the v1 `engine_validated` state
- consistency checks for repeated fields in the known
  `tactical_deep_validation` profile, including its position, candidate, run,
  best-move, and PV cross-links

ParlAWL deliberately applies a stricter bounded consumer profile than the base
Python domain object: at most 1,024 solution plies, 256 themes, 512 evidence
rows, 512 entries in a text or feature map, 48 JSON nesting levels, and integer
fields no larger than `2^53 - 1`. A record outside those limits is not imported,
even if a more permissive producer can represent it.

Exact repeated records are idempotent. Reusing an identity with conflicting
content, or supplying multiple validated record versions for one `puzzle_id`,
rejects the whole pack. Empty packs are not loaded.

## runtime boundary

A successful import atomically replaces the in-memory puzzle queue and disables
live Lichess top-up. Imported records retain their provider, schema, record ID,
and raw canonical source record. They explicitly disallow Lichess PGN hydration,
so a Chess.com or other provider game ID is never treated as a Lichess ID.

The selected JSONL pack remains the authority. ParlAWL does not turn it into a
database provenance row, save a copy of the pack, or create a puzzle-attempt row
merely by opening the file. When an imported solve actually begins, the local
append-only journal retains the exact canonical source record needed to bind
that attempt. The retained record supports later consistency checks and
terminal-attempt publication; it is not a substitute for the pack and does not
restore the imported queue after restart. Reopen the pack when that queue is
needed again.

Imported records do not invent a human rating or difficulty. They are available
under either trainer difficulty filter and display their rating as hidden. The
UI labels them as imported engine lines whose records declare
`engine_validated`. ParlAWL verifies the schema, self-consistent content IDs,
known tactical cross-links, and move legality. It does not authenticate who made
the file, rerun Stockfish, or establish engine optimality, uniqueness, or forced
play. Unknown producer-specific evidence remains supplied data rather than a
verified semantic claim.

Choosing **Reload puzzles** later replaces the imported queue with the explicit
live Lichess flow.

## local solve journal

For exact imported engine-line puzzles, ParlAWL writes solve activity to a local
append-only SQLite journal. An attempt
has an immutable instance record and ordered, hash-linked events. A terminal
attempt has a separate immutable publication record. The journal rejects
updates and deletes, broken sequence or hash links, and replacement of an
existing identity. Hashes demonstrate internal consistency only; they do not
authenticate the player or the pack producer.

Each accepted journal batch commits before the matching in-memory transition or
automatic navigation. A crash can nevertheless leave a committed attempt open.
That open attempt remains explicitly incomplete: ParlAWL does not silently turn
it into a failed or abandoned result after restart, and it cannot be exported as
a terminal v1 attempt.

The versioned `parlawl-attempt-clock-v1` policy compares elapsed monotonic time
with wall-clock time from puzzle exposure. Any backward wall-clock movement, or
absolute drift greater than 5,000 ms in either direction, locally invalidates
the attempt and excludes it from publication. A one-millisecond synthetic start
allocation used only to distinguish same-tick retries is explicitly exempt.

Only solved or failed completed attempts bound to an exact retained imported
engine-line record are eligible for `esports-probability-lab/puzzle-attempt/v1`
publication. Local fixtures, live puzzles, and other non-imported puzzles are
not admitted to this ledger. Open, abandoned, invalid, and crash-interrupted
imported attempts remain local and are excluded.
This boundary prevents a local interaction from being presented as evidence
against a source record the application does not possess exactly.

In **Settings**, choose **Export Solve History** to publish the eligible
records explicitly. The exporter creates a new regular file with owner-only
`0600` permissions and refuses to overwrite any existing path, including a
symlink. A failed write is not reported as a successful publication. Export is
local and user-initiated: ParlAWL does not upload attempts or use them to train a
model automatically. The original JSONL pack remains the puzzle-record
authority after export.

Solver and app-session IDs are opaque pseudonyms, not anonymous identities.
The solver ID persists in local application settings and can link a person's
exports over time; the session ID is newly generated for each app run and is
shared by that run's attempts and retries. Neither field accepts an email,
person name, device name, or filesystem path. The local retained source record
can still contain public player/game provenance supplied by the imported pack.
Exported v1 attempt rows contain opaque IDs and attempt facts, not the raw
retained puzzle record.

Opening an annotated replay synchronously abandons an active imported solve
attempt before the workspace changes; v1 has no pause/resume state. If that
journal write fails, the replay does not open. Returning resets the puzzle and
exposure clock, while database persistence remains lazy until the next solve
interaction.

## offline acceptance check

The repository includes legal Python-generated cross-language golden fixtures
at `tests/fixtures/engine_validated_puzzle_v1.jsonl` and
`tests/fixtures/engine_validated_tactical_profile_v1.jsonl`. The second fixture
exercises the known tactical cross-links. A larger local export can also be
exercised without network access:

```bash
PARLAWL_ACCEPTANCE_PUZZLES=/absolute/path/to/puzzles.jsonl \
  ./build/tests/test_unit_engine_validated_puzzle_pack
```
