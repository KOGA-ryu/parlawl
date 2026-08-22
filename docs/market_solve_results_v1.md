# market-solve-results-v1

What ParlAWL exports after market puzzle reps, and how Arc's `python/dojo/`
ingests it. The consumer side of `market_puzzle_pack_v1.md`.

This contract rides the **existing** solve-journal discipline rather than
inventing a second one: append-only, hash-linked, completed-only, `0600`,
never overwritten. Section 4 says exactly where it hooks, file by file.

- Canonical copy: `/Users/kogaryu/dev/Arc/docs/market_solve_results_v1.md`
- Mirror: `/Users/kogaryu/dev/parlawl/docs/market_solve_results_v1.md`

---

## 1. What a result is, and is not

A result record says: *this solver, in this session, was shown exactly these
bytes, answered these plies in this many milliseconds each, gave this 80%
interval, and ParlAWL scored it this way against the pack's sealed key.*

It is **not** a claim about the market. It is a measurement of a person against
training material. `market_puzzle_pack_v1.md` §7 states the charter; it applies
with full force here, because this is the record most likely to be misread:

> An operator's hit rate is a fact about the operator. It can never be cited as
> a fact about an edge.

Nor is it authenticated. The hash chain and the content digests establish
internal consistency — the same sentence the chess journal uses — not that the
person at the keyboard did not consult a chart in another window.

---

## 2. Export shape

One JSONL file, same framing as the pack: strict UTF-8 without BOM or NUL,
canonical JSON per line (Python `json.dumps(..., ensure_ascii=False,
sort_keys=True, separators=(",", ":"))`), 1 MiB per line, 64 MiB per file,
100,000 records. Line 1 is a header; the rest are results, sorted by
`result_id`.

The 64 MiB / 100,000 / 1 MiB bounds are the ones
`libs/storage/puzzle_attempt_repository.cpp` already enforces on the chess
export (`kMaximumExportBytes`, `kMaximumExportRecords`, `kMaximumLineBytes`).
Reused, not re-chosen.

### 2.1 Header

```jsonc
{
  "schema": "arc/market-solve-results/v1",
  "record_type": "market_solve_results_header",
  "exporter": {
    "interface": "ParlAWL",
    "attempt_policy": "parlawl-terminal-attempt-v1",
    "clock_policy": "parlawl-attempt-clock-v1",
    "scoring_policy": "parlawl-market-scoring-v1",
    "calibration_policy": "parlawl-market-calibration-v1"
  },
  "exported_at_utc": "2026-08-21T18:04:11Z",
  "counts": {"records": 412, "by_mode": {"rush": 380, "study": 32}},
  "packs_referenced": ["market-puzzle-pack-v1:<64 hex>"]
}
```

`packs_referenced` is the join key for ingest: Arc must hold each named pack to
regrade. A results file naming a pack Arc does not have is ingested as
*unregradable* and kept in a separate stratum, never silently trusted.

The exporter block is the existing privacy allowlist
(`exactExportMetadata()` today returns exactly `{attempt_policy, interface}`)
extended by three policy ids. It stays an exact allowlist — the repository
already refuses a terminal whose metadata is not byte-equal to the expected
object, and that check carries over.

### 2.2 Result record

```jsonc
{
  "schema": "arc/market-solve-result/v1",
  "record_type": "market_solve_result",
  "result_id": "market-solve-result-v1:<64 hex>",
  "logical_result_id": "market-solve-logical-v1:<64 hex>",

  "pack_id": "market-puzzle-pack-v1:<64 hex>",
  "puzzle_id": "market-puzzle-v1:<64 hex>",
  "puzzle_record_id": "market-puzzle-record-v1:<64 hex>",
  "task_kind": "trade_line",

  "solver_id": "parlawl-solver-v1:<uuid>",
  "session_id": "parlawl-session-v1:<uuid>",
  "mode": "rush",
  "prior_exposure_count": 0,

  "started_at_utc": "2026-08-21T17:58:02Z",
  "observed_at_utc": "2026-08-21T17:58:31Z",
  "duration_milliseconds": 28914,
  "outcome": "answered",

  "displayed": {
    "record_sha256": "<64 hex>",
    "window_digest": "<64 hex>",
    "hud_digest": "<64 hex>",
    "bar_count": 120,
    "deadline_milliseconds": 45000
  },

  "responses": [
    {"ply_index": 0, "ply_kind": "entry",     "response": "short",
     "exposed_at_ms": 0,     "answered_at_ms": 6120,  "latency_ms": 6120},
    {"ply_index": 1, "ply_kind": "size_band", "response": "0.5R",
     "exposed_at_ms": 6120,  "answered_at_ms": 8004,  "latency_ms": 1884},
    {"ply_index": 2, "ply_kind": "bracket",
     "response": {"stop_atr": 1.5, "target_atr": 3.0},
     "exposed_at_ms": 8004,  "answered_at_ms": 12440, "latency_ms": 4436},
    {"ply_index": 3, "ply_kind": "follow_up", "bar_offset": 1, "response": "hold",
     "exposed_at_ms": 12440, "answered_at_ms": 14002, "latency_ms": 1562}
  ],

  "scores": [
    {"ply_index": 0, "key": "short", "match": "exact",   "score": 1.0},
    {"ply_index": 1, "key": "0.5R",  "match": "exact",   "score": 1.0},
    {"ply_index": 2, "key": {"stop_atr": 1.5, "target_atr": 3.0},
     "match": "exact", "score": 1.0},
    {"ply_index": 3, "key": "hold",  "match": "miss",    "score": 0.0}
  ],
  "line_score": {
    "plies_exact": 3, "plies_total": 8,
    "human_r_multiple": 1.11, "rule_r_multiple": 1.82, "perfect_r_multiple": 2.94,
    "shortfall_vs_rule": 0.71, "shortfall_vs_perfect": 1.83
  },

  "confidence": null,

  "calibration": {
    "question_id": "return_pct_at_horizon_v1",
    "lower": -4.0, "upper": 6.5, "interval_level": 0.8,
    "exposed_at_ms": 22100, "answered_at_ms": 28914, "latency_ms": 6814,
    "realized_value": 3.8421,
    "covered": true,
    "winkler_score": 10.5
  },

  "reveal": {
    "revealed_at_utc": "2026-08-21T17:58:39Z",
    "reveal_latency_ms": 8021,
    "terminal_event_hash": "<64 hex>"
  },

  "metadata": {
    "attempt_policy": "parlawl-terminal-attempt-v1",
    "calibration_policy": "parlawl-market-calibration-v1",
    "clock_policy": "parlawl-attempt-clock-v1",
    "interface": "ParlAWL",
    "scoring_policy": "parlawl-market-scoring-v1"
  }
}
```

Field notes that carry weight:

**`outcome`** is `answered` | `timed_out`. Unlike a chess puzzle there is no
"wrong move ends it" — every ply is answerable and `pass` is a real answer. A
rush rep with a per-ply deadline can expire, and that is a distinct terminal
with the plies that were entered and nulls after; the timeout is data (it says
the operator froze), not a failure to be discarded. `abandoned` and
`invalidated` are terminals in the journal but are **not exportable**, exactly
as on the chess side.

**`displayed`** is the honesty block, and it is three hashes because they answer
three different questions. `record_sha256` is the sha256 of the visible pack
line as read from disk — *which record*. `window_digest` is recomputed by
ParlAWL over the bars it actually put on screen and must equal the record's —
*the UI did not truncate, resample or reorder*. `hud_digest` is over the HUD
array as rendered — *the operator decided on these numbers*. A result whose
`window_digest` disagrees with its pack record is a ParlAWL defect and Arc
must quarantine it, not average it in.

**`latency_ms`** is monotonic elapsed since that ply's exposure. The
`parlawl-attempt-clock-v1` policy applies unchanged: any backward wall-clock
movement or absolute drift above 5,000 ms invalidates the attempt locally and
excludes it from export. Latency is the headline measurement of an aim-trainer,
so a rep with a suspect clock is worth less than nothing.

**`scores`** are ParlAWL's local grade, shipped alongside the raw responses and
tagged with `scoring_policy`. **Arc's regrade from the pack is authoritative.**
A disagreement is a defect signal — a version skew or a scoring bug — and
`dojo/results.py` reports it rather than picking a winner. Shipping both is the
point: raw responses alone lose the feedback the operator actually saw, and
scores alone cannot be re-derived.

**`confidence`** is the probability the operator assigned, present for
`pattern_call` and `anomaly_flag` (Brier), `null` for `trade_line`.

**`prior_exposure_count`** is how many times this solver has already seen this
`puzzle_id`. Once revealed, the identity is known and a second rep is
contaminated. Arc must treat repeat exposures as a **separate stratum, never
pooled** with first looks. ParlAWL's queue prefers unexposed puzzles;
`SessionController::m_lastAttemptStartByPuzzleId` is the existing hook.

**`reveal`** proves the reveal happened *after* the terminal, and carries the
`terminal_event_hash` the vault verified against (see
`market_puzzle_pack_v1.md` D1). A result with `reveal.revealed_at_utc` earlier
than `observed_at_utc` is malformed and is refused at export, not merely
flagged.

### 2.3 Identities

```
logical_result_id = "market-solve-logical-v1:" + sha256(canonical({
                        puzzle_id, session_id, solver_id, started_at_utc}))
result_id         = "market-solve-result-v1:"  + sha256(canonical(
                        record - {result_id, record_type, schema}))
```

Same two-level scheme as the chess terminal record
(`logicalAttemptContent` / `terminalContent` in
`libs/storage/puzzle_attempt_repository.cpp`): a *logical* id that is stable
across a retry of the same rep in the same session, and a *content* id that
changes if any answer changes. Reused deliberately — not re-derived.

---

## 3. Modes

| | `rush` | `study` |
| --- | --- | --- |
| identity while deciding | anonymized | anonymized |
| per-ply deadline | yes (`displayed.deadline_milliseconds`) | none |
| reveal | after terminal | after terminal |
| post-reveal navigation | none, next rep | full: step the continuation, read the citation chain |

**Study mode does not weaken the vault.** The only differences are the absence
of a deadline and the presence of post-reveal navigation. There is no peek, no
"just show me", no setting. Zero context while deciding, history lesson after —
in both modes.

---

## 4. Where this hooks into the existing journal

### 4.1 A new migration, parallel tables

`schemas/024_market_solve_journal_v1.sql` adds
`market_attempt_puzzle_records`, `market_solve_attempt_instances`,
`market_solve_attempt_events`, `market_solve_attempt_terminal_records` — the
same table shapes and the same complete trigger set as
`023_puzzle_attempt_ledger_v1.sql`, with two differences:

- `record_schema` CHECKs pin `'arc/market-puzzle/v1'` and
  `'arc/market-solve-result/v1'` instead of the esports puzzle-candidate and
  puzzle-attempt schemas.
- the `event_kind` enum is the market set: `attempt_started`,
  `ply_answered`, `ply_timed_out`, `calibration_answered`, `attempt_completed`,
  `attempt_timed_out`, `attempt_invalidated`, `attempt_abandoned`,
  `reveal_opened`, `retry_requested`. `reveal_opened` is a review event — it is
  only accepted *after* a terminal, by the same
  `solve_attempt_events_after_terminal` rule that today admits only
  `solution_revealed_review` and `retry_requested`.

Terminal kinds: `completed`, `timed_out` are exportable; `invalidated`,
`abandoned` are not.

**Rejected: widening the CHECK constraints on the 023 tables.** SQLite cannot
alter a CHECK in place; it would take a table rebuild, and rebuilding an
append-only ledger destroys exactly the guarantee the triggers exist to give.
The chess rows would have to be copied through a path with the triggers
disabled, and the migration itself would become the hole. Migrations here are
additive (`002_lock_schema_v0_1.sql` sets that expectation) and this one stays
additive.

**Rejected: one polymorphic table with a `family` column.** The `event_kind`
CHECK would then have to admit both families' kinds, so a chess attempt could
be written with `ply_answered` and nothing in the database would stop it. A
constraint that admits every value has stopped constraining. The cost of two
table sets is duplicated DDL; the cost of one is a lost invariant.

### 4.2 Shared C++, not a fork

`libs/domain/puzzle_attempt_ledger.h` is already generic — `RetainedPuzzleRecord`,
`AttemptInstance`, `AttemptEventInput`, `TerminalAttemptInput`,
`AttemptAppendBatch`, `AttemptAppendReceipt`, `PuzzleAttemptSink` carry no
chess. Only the four schema constants at the top are chess-bound. Add market
constants beside them and reuse the types unchanged.

A new `MarketAttemptRepository : parlawl::attempts::PuzzleAttemptSink` writes
the 024 tables. Before it is written, **extract** from
`libs/storage/puzzle_attempt_repository.cpp` into a shared internal unit
(`libs/storage/append_only_export.{h,cpp}`):

- `openDirectoryWithoutSymlinks` — the `openat` walk with `O_NOFOLLOW` on every
  component
- `writeNewPrivateFile` — `O_CREAT|O_EXCL|O_NOFOLLOW`, `S_IRUSR|S_IWUSR`,
  full-write loop, `fsync`, `linkat` publication, parent `fsync`, and the
  already-published-inode reasoning in its failure path
- `PuzzleAttemptRepository::{canonicalJson, semanticId, pythonUtc}`
- `exactOpaqueUuid`

Copying instead of extracting would put two versions of the symlink-refusing,
no-overwrite, atomic publication dance in the tree, and the second one would be
the one that is wrong. The chess repository must be refactored to call the
extracted unit in the same change, so both paths are exercised by the existing
`test_unit_puzzle_attempt_repository` from the first commit.

### 4.3 The discipline that carries over unchanged

Every one of these is inherited, not restated as a new rule:

1. **Retain on first interaction.** The exact canonical visible pack line is
   retained in `market_attempt_puzzle_records` when a solve actually begins —
   not when the pack is opened. Opening a pack creates no row.
2. **One user action, one transaction, committed before the runtime
   transition is exposed.** A ply is journaled before the next ply is
   rendered.
3. **Open attempts stay open.** A crash leaves a committed attempt with no
   terminal. ParlAWL does not infer a timeout or an abandonment after restart;
   the attempt is incomplete evidence and is not exportable.
4. **Completed-only publication.** Only an attempt whose latest event is an
   exportable terminal (`completed` or `timed_out`), bound to the exact
   retained record, becomes a `market-solve-result-v1` row.
5. **The clock policy.** `parlawl-attempt-clock-v1` verbatim, including the
   1 ms same-tick start adjustment exemption.
6. **Export is explicit and local.** A new Settings action **Export Market
   Solve History**, beside the existing **Export Solve History**. Nothing is
   uploaded, submitted to a model, or used for training automatically.
7. **New file, `0600`, no overwrite, no symlink.** The destination must be a
   new regular file; an existing path — including a symlink — is refused
   rather than replaced. A failed write must not leave a successful-looking
   export.
8. **Opaque pseudonyms, not anonymity.** `parlawl-solver-v1:<uuid>` persists in
   `QSettings` and links a person's exports over time;
   `parlawl-session-v1:<uuid>` is per app run. Neither accepts an email, name,
   device name or path. Stated plainly because market reps will accumulate far
   faster than chess ones.
9. **Mode switches abandon.** Entering a reveal-heavy study session, or an
   annotated replay, synchronously abandons an active rep first; v1 has no
   pause/resume state, and a paused latency measurement is not a latency
   measurement.

### 4.4 What is genuinely new on the ParlAWL side

- **Per-ply events.** The chess journal's `move_correct` / `move_rejected` are
  one-per-move; `ply_answered` is one-per-question, and it carries
  `exposed_at_ms` as well as `elapsed_milliseconds`, because per-ply latency is
  the measurement.
- **The deadline.** `ply_timed_out` and `attempt_timed_out` have no chess
  analogue.
- **`reveal_opened`** as a post-terminal review event, carrying the vault's
  ticket verification result.
- **Local scoring.** The chess side never scores — correctness is move
  equality. Here ParlAWL computes `scores`, `line_score` and the calibration
  numbers for immediate feedback, and labels them with a policy id so Arc's
  regrade has something to disagree with.

---

## 5. Ingest on the Arc side

`dojo/results.py`, under the same charter as the rest of `python/dojo/`:
`__dojo_output_is_evidence__ = False`, and `arcpy` never imports `dojo`.

Ingest is a five-step refusal ladder, not a parser:

1. **Framing and canonicality.** Same bounds as the pack; every line must
   satisfy `canonical_json(parse(line)) == line`.
2. **Identity.** Recompute `result_id` and `logical_result_id`; a mismatch is a
   rejected record, not a repaired one.
3. **Pack binding.** Look up `pack_id` and `puzzle_id`. A result naming a pack
   Arc does not hold is retained in an `unregradable` stratum. A result whose
   `displayed.record_sha256` does not equal the sha256 of the pack line, or
   whose `displayed.window_digest` does not equal the record's, is
   **quarantined** — that is a display-integrity failure and averaging it in
   would launder a rendering bug into a calibration curve.
4. **Regrade.** Recompute every ply score, the line R multiples, coverage,
   Winkler and Brier from the sealed key. Record ParlAWL's numbers beside
   Arc's; report disagreements, do not resolve them.
5. **Stratify.** First looks and repeat exposures
   (`prior_exposure_count > 0`) are separate strata. `rush` and `study` are
   separate strata. `timed_out` reps are kept and counted, never dropped —
   dropping the reps where the operator froze is the most flattering possible
   selection bias.

The ingest writes a build-manifest-style record — declare before ingesting,
finalize with counts, refuse a second write into a finished directory — so the
calibration curve can always answer "how many reps, over which packs, from
which export files". `scout/manifest.py` is the pattern; the counts that matter
here are reps ingested, reps quarantined, reps unregradable, and reps per
stratum.

### 5.1 Calibration scoring

As in `market_puzzle_pack_v1.md` §7.4, and repeated here because this is where
it is computed:

- The 80% interval is scored by **coverage** (aggregate toward 0.80) and the
  **Winkler interval score**. Not Brier.
- **Brier** applies to the categorical confidences on `pattern_call` and
  `anomaly_flag`.
- `trade_line` is scored by per-ply exact match and by the two shortfalls —
  human-vs-rule and human-vs-perfect — which is the implementation-shortfall
  decomposition the execution dojo asks for, for one operator.

Latency belongs in the same report as accuracy and never separately. An
operator who is 70% accurate in four seconds and 74% in forty has learned to
stall, and a report that shows only the accuracy column will congratulate them
for it.

---

## 6. Acceptance checklist

ParlAWL (Qt6/C++20 Widgets, no QML; Qt Test coverage per item; existing chess
tests stay green):

1. `024` migration applies on a database already carrying `023`; every 023
   trigger has a 024 counterpart, proven by attempting each violation.
2. The extracted `append_only_export` unit is called by **both** repositories,
   and `test_unit_puzzle_attempt_repository` still passes unchanged.
3. Export writes a new `0600` regular file, refuses an existing path, refuses a
   symlink, and leaves nothing behind on a failed write.
4. Only `completed` and `timed_out` terminals export; open, abandoned and
   invalidated attempts do not appear.
5. A clock rollback or >5,000 ms drift invalidates the rep and excludes it.
6. `result_id` / `logical_result_id` recompute from the exported bytes.
7. `displayed.window_digest` recomputed from the rendered bars equals the pack
   record's, asserted end-to-end on the golden fixture.
8. `reveal.revealed_at_utc >= observed_at_utc` enforced at export.
9. Every ply carries `exposed_at_ms`, and `latency_ms == answered_at_ms -
   exposed_at_ms` for every row.
10. `metadata` is byte-equal to the exact allowlist; anything else refuses the
    terminal.

Arc (scoped tests during the loop, full suite once before reporting):

11. Round-trip: a fixture export ingests, regrades, and reproduces ParlAWL's
    scores exactly on the golden fixture pair.
12. A tampered `displayed.record_sha256` quarantines rather than ingests.
13. A results file naming an absent pack lands in `unregradable`, not in the
    curve.
14. Repeat exposures and `timed_out` reps are present in the output strata,
    with counts.
15. Coverage, Winkler and Brier each verified against a hand-worked example.
16. The ingest manifest refuses a second write into a finished directory.
