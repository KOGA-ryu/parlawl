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

Import provenance is session-only. The selected JSONL pack remains the authority;
ParlAWL does not turn it into a database provenance row, save a copy of the pack,
or create a puzzle-attempt row while loading or solving it. Reopen the pack after
an app restart when that provenance is needed again.

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
