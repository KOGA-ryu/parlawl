# market-puzzle-pack-v1

The offline contract by which Arc's `python/dojo/` hands ParlAWL a deck of
market decision reps, and the law that keeps a rep honest: **the window ends at
the decision time T, and nothing after T is reachable until the reveal.**

This document is the sibling of `docs/engine_validated_puzzle_import.md`
(ParlAWL) — same shape, same bounded strict-JSON discipline, same
"self-consistent, not authenticated" honesty. Read that one first if you have
not. Where this contract diverges from it, the divergence is stated with its
reason.

The results side lives in `market_solve_results_v1.md`. This document is the
producer side only.

- Canonical copy: `/Users/kogaryu/dev/Arc/docs/market_puzzle_pack_v1.md`
- Mirror: `/Users/kogaryu/dev/parlawl/docs/market_puzzle_pack_v1.md`

---

## 1. The look-ahead law, stated once

A market puzzle is a decision made at a moment T with only what was knowable at
T. Everything about this contract that looks over-engineered is downstream of
one sentence:

> **Every byte ParlAWL can render before the reveal must be a function of the
> visible window, the pack constants, and nothing else.**

Not "should be". Not "the compiler tries to". The visible file is separated
from the continuation file, the continuation is behind a type the UI cannot
name, and the fields that would leak the future through a side channel —
theme names, rating seeds, ply counts, record order, price level, absolute
dates, source digests — are each individually closed below, with the leak
named.

Arc has held this law on the read side for a long time (`arcpy/store.py`: no
API accepts an absolute timestamp; every query is bounded by `clock.now`, and
the bound is the bucket's END, not its start). The dojo is where the law gets
hardest, because the compiler *must* look ahead — it needs the continuation to
build the answer. The structural answer is in §7.

---

## 2. Files and framing

A pack is **two files**, produced together, shipped together:

| file | contents | who may read it |
| --- | --- | --- |
| `<name>.visible.jsonl` | pack header + one visible record per puzzle | the loader, then the UI |
| `<name>.sealed.jsonl` | sealed header + one continuation record per puzzle | the loader, then the vault, then the reveal |

Both are JSONL, both are canonical, both obey the framing of the engine-line
pack, which ParlAWL already enforces in
`libs/puzzle_runner/engine_validated_puzzle_pack.cpp`:

- 64 MiB per file, 1 MiB per line, 100,000 records per file
- strict UTF-8, no BOM, no NUL
- strict JSON with recursive duplicate-key rejection, nesting <= 48
- canonical JSON byte-identical to Python
  `json.dumps(..., ensure_ascii=False, sort_keys=True, separators=(",", ":"))`,
  including Python float spelling (`1e+20`, `-0.0`, `381.0`)
- every line must round-trip: `canonical_json(parse(line)) == line`

Two framing rules are **new** relative to the chess pack:

1. **Line 1 of each file is a header record.** The chess pack is headerless;
   this one is not. A missing header is a rejection, never a default. The
   header is what makes the pack's constants (grain, bar counts, response
   horizon, enumerations, normalization) checkable rather than inferred
   per-record — and an inferred constant is a constant an attacker chooses.
2. **Records are sorted by `puzzle_id` ascending, and the two files are 1:1 in
   that order.** `puzzle_id` is a hash, so the order is semantically inert.
   Any other order — chronological, by ticker, by outcome, by whatever the
   compiler's loop happened to produce — is a channel: an operator who notices
   that the planted-anomaly reps cluster at the front has learned the answer
   without looking at a bar. See §6 decision D5.

Both files import atomically or not at all. A visible file whose sealed
partner is missing, mismatched, or short one record does not load — there is no
"blitz-only" mode that runs without the key, because a rep whose answer cannot
be scored is not a rep, it is a screensaver.

---

## 3. Pack header (visible file, line 1)

`record_type: "market_puzzle_pack_header"`.

```jsonc
{
  "schema": "arc/market-puzzle-pack/v1",
  "record_type": "market_puzzle_pack_header",
  "pack_id": "market-puzzle-pack-v1:<sha256 hex>",
  "continuation_pack_id": "market-continuation-pack-v1:<sha256 hex>",
  "compiler": {
    "compiler_id": "arc.dojo.compile",
    "compiler_version": "1",
    "git_commit": "<40 hex>",
    "git_dirty": false,
    "built_at_utc": "2026-08-21T00:00:00Z"
  },
  "counts": {
    "records": 240,
    "by_task_kind": {"anomaly_flag": 0, "pattern_call": 120, "trade_line": 120}
  },
  "grain": "1d",
  "visible_bar_count": 120,
  "continuation_bar_count": 40,
  "response_horizon_bars": 5,
  "normalization": {
    "price_anchor": 100.0,
    "price_decimals": 6,
    "volume_basis": "window_median",
    "volume_decimals": 6
  },
  "anonymization": {
    "symbol_scheme": "opaque_pack_local_v1",
    "calendar_disclosed": false,
    "session_breaks_disclosed": true
  },
  "themes": [
    {"theme": "post_halt_reopen", "knowable_at_t": true},
    {"theme": "range_compression", "knowable_at_t": true}
  ],
  "task_spec": {
    "pattern_call": {"labels": ["chop", "continuation", "reversal"]},
    "trade_line": {
      "entries": ["long", "pass", "short"],
      "size_bands": ["0", "0.25R", "0.5R", "1R"],
      "stop_atr_multiples": [0.5, 1.0, 1.5, 2.0, 3.0],
      "target_atr_multiples": [1.0, 2.0, 3.0, 4.0, 6.0],
      "follow_up_actions": ["add", "exit", "hold", "tighten"]
    },
    "anomaly_flag": {
      "artifact_classes": [
        "halt_gap_zero_filled", "split_unadjusted", "stale_print_repeat",
        "test_symbol_print", "time_shifted_block", "wash_volume_burst"
      ]
    }
  },
  "hud_spec": {
    "stats": ["atr_pct_20", "range_position_20", "return_5", "return_20",
              "session_break_count", "volume_ratio_20"],
    "verified_stats": ["atr_pct_20", "range_position_20", "return_5", "return_20"]
  },
  "calibration": {
    "question_template": "return_pct_at_horizon_v1",
    "quantity": "return_pct_at_horizon",
    "unit": "percent",
    "interval_level": 0.8,
    "bounds": {"lower": -95.0, "upper": 400.0}
  },
  "rating": {"basis_id": "atr_percentile_v1", "band": {"min": 400, "max": 3000}},
  "source": {
    "scan_manifest_id": "20260821T041500Z-attention_v1-42e15520",
    "scan_citation": "selected 500 from 62,412,880 candidate cells ... by rule 'attention_v1' [scan ..., commit 40739c8]",
    "corpus_dataset_version": "bars_1d/2026-06-01",
    "adjustment_table_sha256": "<64 hex>"
  },
  "evidence_grade": "NOT EVIDENCE. A puzzle pack is training material compiled from the corpus. Its scoring key is a DECLARED RULE'S line replayed on the continuation, not the optimal line and not a claim that the rule has an edge."
}
```

`pack_id = "market-puzzle-pack-v1:" + sha256(canonical(header - {"pack_id"}))`.

`continuation_pack_id` is copied from the sealed header and must match it
exactly. The sealed header does **not** name the visible pack — that would be a
hash cycle. The visible side names the sealed side; the per-record commitments
of §5 do the fine-grained binding in the other direction.

The loader checks `counts` against what it actually parsed, and refuses a
header whose `by_task_kind` sums to something other than `records`.

### 3.1 Sealed header (sealed file, line 1)

`record_type: "market_continuation_pack_header"`. Carries
`continuation_pack_id` (computed over its own remaining fields),
`record_count`, `compiler`, `built_at_utc`, and the same `evidence_grade`
sentence. Nothing else — every semantic constant lives on the visible side, so
there is one place to read the pack's rules.

---

## 4. Visible record

`record_type: "market_puzzle"`. One per puzzle, sorted by `puzzle_id`.

```jsonc
{
  "schema": "arc/market-puzzle/v1",
  "record_type": "market_puzzle",
  "puzzle_id": "market-puzzle-v1:<sha256 hex>",
  "record_id": "market-puzzle-record-v1:<sha256 hex>",
  "continuation_commitment": "market-continuation-v1:<sha256 hex>",

  "task_kind": "trade_line",
  "theme": "range_compression",
  "rating_seed": 1720,
  "rating_seed_basis": "atr_percentile_v1",

  "display_symbol": "SYM-0473",
  "window": {
    "grain": "1d",
    "bar_count": 120,
    "bars": [
      [0, 99.412300, 100.884000, 99.100200, 100.005500, 1.243000, 0.884000],
      [1, 100.005500, 100.771000, 98.902000, 99.318700, 0.912000, 1.010000]
    ],
    "session_break_after": [false, false, true],
    "window_digest": "<64 hex>"
  },
  "hud": [
    {"stat_id": "atr_pct_20", "value": 2.418000, "unit": "percent"},
    {"stat_id": "range_position_20", "value": 0.312000, "unit": "ratio"}
  ],
  "response_spec": {
    "horizon_bars": 5,
    "plies": [
      {"ply_index": 0, "ply_kind": "entry"},
      {"ply_index": 1, "ply_kind": "size_band"},
      {"ply_index": 2, "ply_kind": "bracket"},
      {"ply_index": 3, "ply_kind": "follow_up", "bar_offset": 1},
      {"ply_index": 4, "ply_kind": "follow_up", "bar_offset": 2},
      {"ply_index": 5, "ply_kind": "follow_up", "bar_offset": 3},
      {"ply_index": 6, "ply_kind": "follow_up", "bar_offset": 4},
      {"ply_index": 7, "ply_kind": "follow_up", "bar_offset": 5}
    ]
  },
  "calibration_question": {
    "question_id": "return_pct_at_horizon_v1",
    "quantity": "return_pct_at_horizon",
    "unit": "percent",
    "interval_level": 0.8,
    "bounds": {"lower": -95.0, "upper": 400.0}
  },
  "source": {
    "scan_manifest_id": "20260821T041500Z-attention_v1-42e15520",
    "corpus_dataset_version": "bars_1d/2026-06-01",
    "adjustment_table_sha256": "<64 hex>",
    "grain": "1d"
  }
}
```

### 4.1 Bars

Each bar is a fixed 7-tuple: `[bar_index, open, high, low, close, volume,
trade_count]`. Array-of-arrays rather than array-of-objects because at 512 bars
the key repetition is most of the line budget, and the shape is fixed by
contract anyway.

**There are no timestamps.** `bar_index` is 0..`bar_count-1`; the last bar is
T, and T is *inclusive* — the decision is made with bar T's close known, which
is the same "bucket has finished" bound `arcpy/store.py` holds. Absolute dates
appear nowhere in the visible file. `session_break_after[i] == true` means a
session boundary follows bar `i`, which preserves the *shape* of overnight gaps
without dating them; the header's `session_breaks_disclosed` says whether the
array is populated at all.

Prices are normalized so the last close is exactly `100.0`:
`round(price / close_T * 100.0, 6)`. Volume is `round(volume / median_volume,
6)` over the visible window. `trade_count` is normalized the same way. The
rounded values *are* the values — there is no unrounded form anywhere, which
makes the digest stable and the arithmetic reproducible in any language that
has doubles and Python float spelling.

A bar with a missing field is `null`, never `0` and never carried forward.
This is the loader-side face of `scout/loader.py`'s law: **absent is masked,
never zero.** A puzzle whose visible window contains a null OHLC is legal (a
halt is a real thing to have to read); a puzzle whose window is majority-null
is a compiler bug and the compiler refuses it.

`window_digest = sha256(canonical(bars))` over exactly the array as shipped.
It is not a source digest (see D3) — it is the answer to "did the UI render
what the record says", and it is echoed back in the results contract.

### 4.2 HUD

`hud` is an array of `{stat_id, value, unit}`, `stat_id` drawn from the
header's `hud_spec.stats`. Unknown stat ids are **refused**, not passed
through as supplied data.

This is stricter than the chess sibling, deliberately. `engine_validated_puzzle_import.md`
says "unknown producer-specific evidence remains supplied data rather than a
verified semantic claim" — correct there, because unknown evidence sits in a
detail panel. Here the HUD is *what the operator decides on*. A wrong or
unrecognized HUD number does not sit inertly; it trains a reflex against a
number nobody checked. So: closed enumeration, and the four stats in
`hud_spec.verified_stats` are **recomputed by ParlAWL from the visible bars**
and must agree to 1e-6 or the record is rejected. Their derivations:

| stat_id | derivation over the normalized visible window |
| --- | --- |
| `return_5` | `100 * (close[N-1]/close[N-6] - 1)` |
| `return_20` | `100 * (close[N-1]/close[N-21] - 1)` |
| `atr_pct_20` | Wilder ATR(20) over the last 20 bars, expressed as `100 * ATR / close[N-1]` |
| `range_position_20` | `(close[N-1] - min(low[N-20..N-1])) / (max(high[N-20..N-1]) - min(low[N-20..N-1]))` |

All four are scale-free, which is what lets them survive normalization. Any
HUD stat denominated in dollars would be a deanonymization channel and is
forbidden by construction: the visible window has no dollars in it.

### 4.3 Themes

`theme` is one string from the header's `themes` list, and every entry in that
list carries `knowable_at_t: true`. This is not decoration. `earnings_gap` is a
theme (the gap already happened at T). `false_breakout` is **not** a theme —
it names the outcome, and shipping it on the visible side hands the operator
the answer in a word. The compiler is responsible for the classification; the
loader enforces membership and the `knowable_at_t` flag. Outcome language lives
in the sealed record's `outcome_theme`, which is reveal material.

### 4.4 Response spec

`response_spec.plies` is the exact ordered list of questions ParlAWL will ask,
so it can lay out its widgets before the operator starts the clock.

`horizon_bars` is **pack-constant** and equals the header's
`response_horizon_bars` for every record. If it varied per puzzle, ply count
would leak how long the rule's trade lasts — a five-ply puzzle and a
one-ply puzzle are different answers before a single bar is read. The
continuation may (and usually does) carry more bars than the horizon, because
the reveal wants to show what happened next; the *questions* stop at the
horizon.

Per task kind:

- `pattern_call` — plies: `label` (from `task_spec.pattern_call.labels`), then
  `confidence` (a probability in [0,1], for Brier scoring).
- `trade_line` — plies: `entry`, `size_band`, `bracket` (a `{stop_atr,
  target_atr}` pair chosen from the header's grids), then one `follow_up` ply
  per horizon bar, each an action from `follow_up_actions`. `pass` is a real
  entry and is scored as one; declining a fight is the discipline the
  roadmap's counterweight names, and a rep that cannot express it teaches the
  opposite.
- `anomaly_flag` — plies: `verdict` (`planted` | `clean`), then
  `artifact_class` (required iff `planted`, from the header's enumeration),
  then `confidence`.

### 4.5 Rating seed

`rating_seed` is an integer inside the header's band, with `rating_seed_basis`
naming the formula that produced it. It is displayed as a **seed**, never as a
difficulty or a rating, in the same voice ParlAWL already uses for imported
engine lines ("declares `engine_validated`; not independently verified").

Permitted bases in v1 — all functions of the visible window or of pack
constants alone:

- `flat_seed_v1` — every puzzle 1500. The honest default when nothing is
  measured.
- `atr_percentile_v1` — the window's `atr_pct_20` mapped through the scan
  universe's percentile curve (which the citation names) onto the band.

A basis whose inputs include the continuation is **forbidden on the visible
side.** The obvious tempting one — seed by outcome dispersion, so that wild
continuations rate as hard — is a clean look-ahead leak: a high seed would
announce a violent future before the first bar was read. Outcome-derived
difficulty commentary is legal in the sealed record as `difficulty_note`, where
it is reveal material like everything else.

The seed's only job is to order a queue on day one, before any results exist.
The real difficulty is measured later, by Arc, from ingested results — see
`market_solve_results_v1.md`.

---

## 5. Sealed record

`record_type: "market_continuation"`. One per puzzle, same order.

```jsonc
{
  "schema": "arc/market-continuation/v1",
  "record_type": "market_continuation",
  "puzzle_id": "market-puzzle-v1:<sha256 hex>",
  "record_id": "market-continuation-record-v1:<sha256 hex>",
  "content": {
    "reveal_identity": {
      "ticker": "XYZ",
      "decision_time_utc": "2021-02-08T21:00:00Z",
      "exchange": "XNAS",
      "instrument_class": "common_stock"
    },
    "continuation": {
      "grain": "1d",
      "bar_count": 40,
      "bars": [[0, 100.005500, 101.220000, 99.800000, 100.912000, 1.402000, 0.940000]],
      "session_break_after": [false]
    },
    "scoring_key": { "...": "see 5.2" },
    "calibration_key": {
      "question_id": "return_pct_at_horizon_v1",
      "realized_value": 3.842100,
      "derivation": "100 * (close[T+5]/close[T] - 1) on the normalized continuation"
    },
    "outcome_theme": "false_breakout",
    "difficulty_note": {"basis_id": "outcome_dispersion_v1", "value": 0.71},
    "source_identity": {
      "source_window_digest": "<64 hex>",
      "source_continuation_digest": "<64 hex>",
      "bars_root_id": "bars_1d/2026-06-01",
      "partition_paths": ["partition_year=2021/partition_month=02/trade_date=2021-02-08"]
    }
  }
}
```

`continuation_commitment = "market-continuation-v1:" + sha256(canonical(content))`.
The commitment covers `content` only — not `puzzle_id`, not the envelope — so
there is no cycle with `puzzle_id`, which is computed *from* the commitment
(§6, D2). The loader recomputes the commitment for every sealed record and
requires it to equal the paired visible record's `continuation_commitment`, in
both directions, 1:1, no orphans on either side.

### 5.1 Why the source digests are sealed

`source_window_digest` is a digest over the **real, un-normalized** corpus bars
— exactly `arcpy.analysis.volatility.evidence_digest_for_bars` in spirit, so
an auditor holding the corpus can prove the puzzle was built from the rows it
names.

It cannot live on the visible side. Anyone holding the corpus (which is the
whole point of Arc) can precompute that digest for every ticker-date window in
the decade and reverse the anonymization in one join. A citation that
deanonymizes is not a citation, it is a key. So the visible record's `source`
block carries only pack-level, non-identifying provenance — scan manifest id,
dataset version, adjustment table digest, grain — and the identifying half of
the citation travels sealed, arriving with the reveal.

The reveal is where the history lesson happens, and that is where it belongs:
zero context while deciding, full provenance after.

Sealing the digest removes the cheapest key. It does not close the join — the
visible bars are themselves a join key against the same corpus, which is stated
as residual risk in D6 and is not fixable while the pack ships bar shapes. Read
this section as "do not hand out a one-hop index", not as "the window is now
unidentifiable".

### 5.2 Scoring keys

**`pattern_call`**
```jsonc
{"task_kind": "pattern_call",
 "correct_label": "reversal",
 "label_derivation": "sign(close[T+5]/close[T]-1) against +/-1.0 * atr_pct_20; |move| < 1 ATR => chop"}
```

**`trade_line`** — the rule's per-ply line, plus what the line actually
returned:
```jsonc
{"task_kind": "trade_line",
 "rule_id": "u2.fade_v1",
 "rule_declaration_digest": "<64 hex>",
 "line": [
   {"ply_index": 0, "ply_kind": "entry",      "key": "short"},
   {"ply_index": 1, "ply_kind": "size_band",  "key": "0.5R"},
   {"ply_index": 2, "ply_kind": "bracket",    "key": {"stop_atr": 1.5, "target_atr": 3.0}},
   {"ply_index": 3, "ply_kind": "follow_up",  "bar_offset": 1, "key": "hold"},
   {"ply_index": 7, "ply_kind": "follow_up",  "bar_offset": 5, "key": "exit"}
 ],
 "line_outcome": {"r_multiple": 1.82, "exit_reason": "target", "exit_bar_offset": 4},
 "perfect_outcome": {"r_multiple": 2.94, "note": "best achievable within the horizon and grids"}}
```

`rule_declaration_digest` binds the key to the exact declared rule in Arc that
produced it, so a regrade can prove which rule was being trained against.

The honesty clause, and it is the load-bearing one: **the scoring key is the
declared rule's line replayed on the continuation. It is not the right answer,
not the optimal answer, and not evidence that the rule has an edge.** ParlAWL
must label it in those words wherever it is shown, exactly as it labels an
imported engine line as producer-declared rather than ParlAWL-verified. A
puzzle deck compiled from a rule that turns out to be worthless is still a
valid deck of reps against that rule; what it is not is a claim.

`perfect_outcome` exists so the results contract can carry human-vs-rule and
human-vs-perfect separately — the implementation-shortfall decomposition the
roadmap's execution dojo asks for.

**`anomaly_flag`**
```jsonc
{"task_kind": "anomaly_flag",
 "planted": true,
 "artifact_class": "stale_print_repeat",
 "injection_spec": {"bars": [41, 42, 43], "transform": "repeat_close_of_bar_40", "magnitude": 0.0},
 "clean_provenance": null}
```

For a clean record, `planted: false`, `artifact_class: null`,
`injection_spec: null`, and `clean_provenance` names the quality findings the
compiler checked to assert cleanliness (`bars_quality_findings/`). Both shapes
carry the same key set with nulls, so record length does not sort planted from
clean at a glance.

The artifact classes are not invented. They are the hazards this repository
already documents and defends against, turned into training targets:
`halt_gap_zero_filled` (`scout/loader.py`'s zero-fill damage),
`split_unadjusted` (`arcpy/adjust.py`'s read-time adjustment),
`test_symbol_print` (`scout/universe.py`'s `ZVZZT`/`NTEST` family with their
$499,999.995 prints), `stale_print_repeat`, `wash_volume_burst`,
`time_shifted_block`. Training the operator's cheater radar on the exact
contaminations the pipeline is built to catch is the cheapest possible
curriculum, because the answer key already exists in the quality findings.

---

## 6. Decisions, with what was rejected

### D1 — How continuation segregation actually works

**Chosen: two files + a per-record commitment hash + a loader-enforced vault
whose unseal requires a committed terminal journal event.**

Three layers, each closing a different hole:

1. *File*: the continuation is not in the record the UI receives. There is no
   field to accidentally bind to a label.
2. *Type*: the loader produces `MarketPuzzleVisible` (what panels get, with no
   continuation member at any depth) and `SealedContinuationVault` (owned by
   the session controller, never by a widget). `Vault::open(puzzleId, ticket)`
   is the only accessor.
3. *Ticket*: `RevealTicket` has a private constructor with
   `MarketAttemptRepository` as its only friend, and is minted only in the
   append receipt of a batch that committed a terminal event. The vault
   additionally re-verifies the ticket's `attempt_instance_id` and terminal
   `event_hash` against the journal before opening — so the guard is testable
   at runtime (forge a ticket, assert refusal), not only at compile time.

Two acceptance tests keep it structural rather than aspirational: a
source-scan test asserting that no translation unit under `apps/desktop/`
includes the continuation header, and a runtime test asserting the vault
refuses to open before the terminal event and after a forged ticket.

Rejected:

- *One record, UI just doesn't read the field.* The struct is reachable
  wherever the puzzle is, so the guarantee decays to a code-review convention
  the first time someone adds a debug tooltip. The chess side already learned
  this: recorded alternatives "are never promoted into `PuzzleDefinition`" —
  the boundary is a type, not a habit.
- *Obfuscation / encryption in the same record.* The key ships with the pack,
  so it is theater. Worse, theater invites the reasoning "it is encrypted, so
  it is fine to keep near the UI".
- *Separate file with no commitment.* A swapped or hand-edited sealed file
  would let a solver grade themselves generously, and neither ParlAWL nor Arc
  could tell. The commitment makes tampering a load-time rejection, before a
  single rep is served.

**The honest limit**, in the voice this repo uses for hashes: verifying the
commitment requires the continuation in the same process's memory, and the
sealed file is a text file on the operator's disk. This design stops the UI
from leaking the future and stops a tampered pack from scoring; it does not
stop a determined operator with a text editor from cheating themselves. Like
the journal's hash chain, it establishes internal consistency, not honesty of
the person at the keyboard.

### D2 — How content ids are computed

**Chosen: semantic ids over canonical JSON, `prefix:sha256hex`, exactly the
chess scheme, with the continuation commitment inside the puzzle identity.**

```
window_digest           = sha256(canonical(window.bars))
continuation_commitment = "market-continuation-v1:"  + sha256(canonical(sealed.content))
puzzle_id               = "market-puzzle-v1:"        + sha256(canonical({
                              task_kind, grain, window_digest,
                              response_horizon_bars, continuation_commitment}))
record_id               = "market-puzzle-record-v1:" + sha256(canonical(
                              visible_record - {record_id, record_type, schema}))
pack_id                 = "market-puzzle-pack-v1:"   + sha256(canonical(header - {pack_id}))
```

Including the commitment in `puzzle_id` is the direct analogue of the chess
`puzzle_id` covering `{fen_without_fullmove, solution_uci}` — position *and*
answer. It means an identity cannot be reused with a different answer, and it
is still computable from the visible line alone, because the commitment is a
field on the visible record. ParlAWL therefore recomputes every id from the
bytes it read, the way it already does for engine-line records, and the
"multiple validated versions of one `puzzle_id`" rejection carries over
unchanged.

Rejected:

- *Identity over the visible window only.* Two puzzles with the same window and
  different task kinds collide; worse, the answer becomes swappable under a
  stable id.
- *Producer-assigned UUIDs.* Nothing to recompute means nothing to verify,
  and the whole import discipline is recomputation.
- *Identity including `display_symbol` or `rating_seed`.* Both are presentation
  choices; re-anonymizing or re-seeding a pack would mint new ids for the same
  reps and break the results history that cites them.

### D3 — Bar count bounds per puzzle

**Chosen: `visible_bar_count` and `continuation_bar_count` are pack-header
constants, identical for every record. `20 <= visible <= 512`,
`1 <= continuation <= 512`, `continuation >= response_horizon_bars`. One pack
is one grain.**

The floor is 20 because the verified HUD stats are 20-period and must be
computable from the window itself rather than from history the operator cannot
see. The ceiling is 512 because a normalized bar canonical-encodes to roughly
90–140 bytes, so 512 bars is ~70 KB — comfortably inside the 1 MiB line budget
with room for HUD, response spec and citation, even under a pathological
encoding.

Constant rather than per-puzzle for the same reason the response horizon is
constant: any per-record variation is a channel, and a fixed set of visible
per-record fields makes the leak audit finite. After this decision the audit
reads: *the only things that vary per record are the bars, the HUD, the theme,
the seed, the display symbol, and the commitment* — six things to argue about
instead of a schema.

A candidate whose history is too short for a full window is **rejected at
compile time, not truncated.** A short window would announce "this ticker
recently listed", which is both a leak and a distinct regime.

Rejected:

- *Per-puzzle bar counts.* Window length correlates with grain, regime and
  data availability; it is a free hint.
- *No ceiling, bounded only by the 1 MiB line.* The chess sibling's stricter
  consumer profile exists precisely so a permissive producer cannot force the
  consumer into an unbounded shape. Same reasoning, same answer.
- *Mixed grains in one pack.* Normalization, HUD periods and the ATR grids all
  mean different things at 1m and 1d. One pack, one grain.

### D4 — How ratings seed

**Chosen: a declared, visible-window-only seed with a named basis, labelled a
seed and never a rating.** See §4.5.

The inventory finding behind this: **ParlAWL has no rating engine at all.**
There is no Elo, no Glicko, nothing that updates. `PuzzleMetadata::rating` is a
display integer, imported records set it to 0 with `ratingHidden = true`, and
`ratingRangeFor()` in `libs/domain/puzzle_round.h` buckets the *source game's
players*, not the solver. So there is nothing to reuse and nothing to be
compatible with; the question is only what the pack should claim.

Rejected:

- *An absolute producer-supplied difficulty rating.* It would assert a
  measurement ParlAWL cannot check and Arc has not made. The chess importer's
  rule — "imported records do not invent a human rating or difficulty" — is
  right, and inventing one here would be the same laundering in a new domain.
- *No rating at all* (the chess answer, `rating = 0, ratingHidden = true`).
  Honest but wasteful: with no ordering the first hundred reps are dealt at
  random, and easy and impossible reps arrive interleaved. A seed labelled as a
  seed costs nothing and is replaced by measurement as soon as measurement
  exists.
- *Seeding from outcome dispersion.* Look-ahead. Covered in §4.5; kept as
  sealed `difficulty_note`.

### D5 — Record order

**Chosen: sorted by `puzzle_id` ascending in both files.** A hash order is
independent of ticker, date, outcome, task kind and planted-ness. Rejected:
compiler-loop order (leaks the compiler's traversal, which is usually
chronological or by ticker) and explicit shuffling with a shipped seed (a
shipped seed is a reproducible permutation, i.e. the same order plus a
decoder). Blitz mode may still deal in its own session-local shuffle; the file
order simply carries no information to begin with.

### D6 — Anonymization scheme

**Chosen: pack-local opaque symbols from a compiler-side permutation whose
seed is not shipped; no absolute dates; prices and volumes normalized to the
window.**

`display_symbol` is `SYM-<4 digits>`, assigned by index over a permutation the
compiler holds and does not emit. Rejected: `sha256(ticker)[:8]` or any
deterministic function of the real symbol — the tradable universe is ~25,000
names, so a dictionary attack is milliseconds, and a "hashed" symbol is a
plaintext symbol with extra steps.

Normalization to `close_T == 100.0` rather than shipping real prices with only
the symbol hidden: absolute price level is itself identifying (a $3,000 stock
is a short list), and round-number behaviour, tick patterns and split-adjusted
artefacts all survive symbol removal. Once prices are unit-free, every HUD
stat and every scoring grid has to be expressed in returns, ratios or ATR
multiples — which is where they belonged anyway, and which is why the
`trade_line` bracket is declared in ATR multiples rather than dollars.

**Residual risk, accepted and not mitigated: a corpus holder can re-identify
any window from its bar shapes alone.** Every mitigation above defeats a
*lookup* — reading the ticker, hashing it back, pricing the name, dating the
window from its calendar. None of them defeats a *join*. The normalization
`round(price / close_T * 100, 6)` is a per-window affine map, so the sequence
of close ratios in the visible record is invariant under it: an operator who
holds the corpus (which is every Arc operator, and the corpus is the point of
Arc) normalizes each candidate ticker-date window the same way and matches. At
six decimals, a 60-bar ratio sequence is unique against the ~62M candidate
cells of a decade by a wide margin; the volume-against-median series confirms
it independently. Ticker and date come back exactly, with no work worth
calling an attack.

This cannot be fixed inside this contract. The bar shapes *are* the puzzle;
hiding, quantizing or jittering them enough to break the join would either
destroy what is being solved or corrupt the very stats the HUD verifies. So the
honest statement of what anonymization buys is:

- **Closed:** casual recognition, dictionary attacks on a hashed symbol, price
  level, calendar/date channels, one-hop indexes such as
  `source_window_digest` (§5.1), and re-identification by anyone who does
  *not* hold the corpus.
- **Open, by design:** re-identification by a corpus holder via a shape join.

Which is to say the pack is anonymized against a solver, not against an
auditor, and it is safe to hand out exactly as far as the corpus is. Any
future decision to ship packs to holders of the corpus who are *not* trusted
with the corpus has to reopen this section, not assume it.

---

## 7. Arc side — `python/dojo/`, and its charter

`python/dojo/` is a **sibling of `arcpy` and `scout`, not a submodule of
`arcpy`** — the same placement decision, made for the same reason, and it is
load-bearing rather than tidy.

> `arcpy` is evidence machinery: clock-bounded reads, look-ahead guards,
> digests, seals, ledgers. `scout` proposes and never attests. `dojo` compiles
> training material and measures a person. None of what `dojo` produces is
> evidence about a market. `dojo` imports `arcpy`; **`arcpy` must never import
> `dojo`.**

Greppable, as `scout` does it:

```python
#: Read by nothing; present so that the answer is greppable and unambiguous.
__dojo_output_is_evidence__ = False
```

The distinction from `scout` is worth stating because it is easy to blur.
Scout output is *not evidence about the market yet* — a replay on the event
engine can promote it. Dojo output is **not evidence about the market at all,
ever**: a pack is training material, and a results ingest is a measurement of
an operator's calibration. An operator's hit rate is a fact about the operator.
It can never be cited as a fact about an edge, and the moment it is, the
roadmap's fourth laundering seam has opened.

### 7.1 The two-reader rule

The compiler must look ahead — it needs the continuation. The structural answer
is that it never holds both at once:

```
visible_reader   = BarStore(root, Clock(T),   grain=g)   ->  visible file
continuation_reader = BarStore(root, Clock(T + H + tail), grain=g)  ->  sealed file
```

Two readers, two clocks, two output files, and **no object in `dojo.compile`
holds a visible window and a continuation in the same value.** The visible
record is built, digested and committed to bytes before the continuation reader
is constructed. This is the compile-time face of the same law
`arcpy/store.py` holds at read time, and it is what makes the look-ahead test
possible: perturb the continuation, assert the visible file is byte-identical.

### 7.2 Package surface

| module | job |
| --- | --- |
| `dojo/pack.py` | the record types and the canonical encoder — the single source of truth for the wire format |
| `dojo/anonymize.py` | normalization, symbol permutation, session-break extraction |
| `dojo/hud.py` | the HUD derivations, including the four ParlAWL re-verifies |
| `dojo/inject.py` | anomaly planting and its `injection_spec` |
| `dojo/compile.py` | corpus -> pack, under the two-reader rule |
| `dojo/results.py` | ingest `market-solve-results-v1`, regrade from the pack |
| `dojo/calibration.py` | coverage, Winkler interval score, Brier |
| `dojo/build_manifest.py` | declare-before-compile, finalize-with-counts |

### 7.3 Citation discipline

A pack cites its sources, and the citation is a first-class field, not a
comment. `dojo/build_manifest.py` mirrors `scout/manifest.py`'s protocol —
declare the build parameters before compiling, finalize with counts, refuse a
second write into a finished directory, refuse a build from a tree with no
commit unless a waiver sentence is supplied and carried in every citation.

It is a **separate** manifest kind from a scan manifest. A pack build is not a
scan; writing a scan manifest for it would pollute the scan record with rows
that were never scored by a scan rule. Instead the pack build **cites** the
scan manifest it drew candidates from, by `scan_id` and by
`ScanManifest.citation()` text, both of which travel in the pack header where
ParlAWL can display them.

The full citation chain a reveal can print: pack header -> scan manifest id and
citation sentence -> corpus dataset version and adjustment table digest ->
sealed `source_identity` (ticker, decision time, source window digest,
partition paths). Every link recomputable, and the identifying half only after
the reveal.

### 7.4 Calibration scoring, and one correction to the roadmap

`docs/research_factory_roadmap.md` says "every rep ends with an 80% interval,
Brier-scored". Two different instruments are being conflated, and the contract
separates them:

- **Intervals** (the 80% band) are scored by **coverage** (did the realized
  value fall inside, aggregated over many reps toward 0.80) and by the
  **Winkler interval score** (width plus a miss penalty). Brier does not apply
  to an interval.
- **Brier** applies to the categorical confidences: `pattern_call`'s label
  confidence and `anomaly_flag`'s verdict confidence.

Both are carried in the results contract, both are computed locally by ParlAWL
for immediate feedback, and both are recomputed authoritatively by
`dojo/calibration.py` on ingest.

---

## 8. ParlAWL side — what generalizes, what is chess-bound

Read from the code, not assumed.

### Generalizes as-is (extract and share, do not fork)

| what | where | note |
| --- | --- | --- |
| `StrictJsonParser`, `appendCanonicalJson`, `pythonFloat`, `pythonStringLess`, `semanticId`, `canonicalJson` | anon namespace in `libs/puzzle_runner/engine_validated_puzzle_pack.cpp` | the Python-compatible canonical-JSON core; entirely domain-free. Extract to a shared `libs/puzzle_runner/strict_json.{h,cpp}` |
| `exactKeys`, `requiredString`, `requiredInteger`, `validateTextMap`, `validateUtc`, `validateEvidenceArray` | same file | generic field checkers |
| pack framing in `fromJsonLines` | same file | 64 MiB / 1 MiB / 100k / UTF-8-no-BOM / idempotent duplicates / conflicting-id rejection — all reusable verbatim |
| `parlawl::attempts` types | `libs/domain/puzzle_attempt_ledger.h` | `RetainedPuzzleRecord`, `AttemptInstance`, `AttemptEventInput`, `TerminalAttemptInput`, `AttemptAppendBatch`, `AttemptAppendReceipt`, `PuzzleAttemptSink` — fully generic. Only the four schema string constants are chess-bound |
| the append-only journal *mechanics* | `schemas/023_*.sql`, `libs/storage/puzzle_attempt_repository.cpp` | hash chain, genesis hash, sequence triggers, one-terminal rule, review-events-after-terminal rule |
| `writeNewPrivateFile`, `openDirectoryWithoutSymlinks` | `puzzle_attempt_repository.cpp` | the `openat`/`O_NOFOLLOW` / `0600` / `linkat` / `fsync` publication dance. **Extract, never copy** — two divergent copies of this is how one of them ends up wrong |
| `PuzzleAttemptRepository::{canonicalJson, semanticId, pythonUtc}` | same | generic |
| the clock policy | `SessionController::attemptClockDrifted` etc. | `parlawl-attempt-clock-v1` (wall vs monotonic, 5,000 ms, the 1 ms same-tick exemption) applies unchanged, and matters *more* where latency is scored |
| queue mechanics | `SessionController::{replacePuzzles, appendPuzzles}`, filtered indices, visible window, refill | generic modulo the puzzle type |
| exposure bookkeeping | `m_lastAttemptStartByPuzzleId` | becomes `prior_exposure_count` in the results contract |

### Chess-bound (do not attempt to generalize)

`libs/puzzle_runner/chess_position.*` (FEN, legality, replay), `pgn_utils`,
`PuzzleEngine` (ply-by-ply UCI matching, `userSide` from FEN), `validateFen`,
`validateSourceGame` (white/black names and ratings),
`validateKnownTacticalProfile`, `validateThemes`' evidence cross-links,
`annotated_replay_pack.*`, `replay_session.*`, all of `libs/lichess/`,
`PuzzleRound::{ratingRangeFor, materialScoreFromFen}`, and
`PuzzleMetadata`'s white/black fields.

### Does not exist and must be built new

- Any rating machinery (D4). There is none.
- Latency measurement per ply. The existing journal records
  `elapsed_milliseconds` per event, which is the right hook, but nothing
  currently exposes a per-ply deadline or a rush mode.
- Interval and confidence capture, and their local scoring.
- The vault and the reveal ticket (D1).
- Chart rendering for OHLCV. `apps/desktop/` draws a board.

### The one thing that must be shared with Arc

A **golden fixture pair**, committed in both repos and byte-identical:
`tests/fixtures/market_puzzle_pack_v1.visible.jsonl` and
`market_puzzle_pack_v1.sealed.jsonl` in ParlAWL, generated by
`python/tests/fixtures/` in Arc. This is the same cross-language golden pattern
that already keeps `engine_validated_puzzle_v1.jsonl` honest, and it is the
single artifact that proves the Python encoder and the C++ loader agree on
canonical float spelling, key ordering and every digest in §6/D2. Every
`1e+20` / `-0.0` / `381.0` edge case in the chess fixture belongs in this one
too.

**One direction only, and there is exactly one encoder.** The bytes are
compiled by Arc's `dojo` (`build_golden_pack` in `python/tests/test_dojo.py`,
which pins compiler identity, build instant, sampling seed and symbol
permutation) and *copied* into ParlAWL unchanged. ParlAWL never generates them.
The first implementation of this contract got that wrong in the way that is
easy to miss: both repos committed a file at that path, each repo's test
round-tripped its own copy, both were green, and the two files were entirely
different packs from two independently written Python encoders — which is the
"two divergent copies" hazard this section exists to prevent, arrived at by
obeying the section's letter. Since the repos cannot read each other at test
time, the byte-identity is carried by **the SHA-256 of each file, pinned as a
constant in both repos** (Arc:
`test_the_golden_fixture_matches_the_digest_parlawl_pins`; ParlAWL:
`goldenPairIsTheBytesArcCommitted`). Regenerating the pair fails both tests
until the copy and both pairs of constants move together.

**What the golden pair does and does not cover.** A pack header carries a
single `task_kind`, and Arc's compiler declines `trade_line` outright — it
replays a *declared rule*, and no rule is declared for a bracket — so the
golden pair is a `pattern_call` pack and no Arc-generated pair can ever cover
the three shapes at once. ParlAWL therefore also commits
`tests/fixtures/market_task_kinds_v1.{visible,sealed}.jsonl`: a local,
hand-built pack with one record per task kind, used to exercise the loader's
three shapes. It is deliberately named so that it cannot be mistaken for the
golden pair, and it proves nothing about cross-language agreement — it is a
second encoder agreeing with itself.

So the cross-language guarantee, stated exactly:

| Task kind | Python encoder ↔ C++ loader, on the same bytes |
|---|---|
| `pattern_call` | Yes — the golden pair, plus the real `attention_v1` pack via `tests/integration/test_integration_market_real_packs.cpp` |
| `anomaly_flag` | Yes — the real `anomaly_v1` pack via the same integration test (opt-in, `PARLAWL_REAL_PACK_DIR`) |
| `trade_line` | **No.** Arc cannot emit one. Loader behaviour is covered locally; agreement is not covered, and closing it means teaching the compiler a declared bracket rule (§7.4), not writing another fixture |

---

## 9. Acceptance checklist for an implementation of this contract

ParlAWL (Qt6/C++20 Widgets, no QML; every item gets Qt Test coverage; the
existing chess tests stay green):

1. Both files load atomically; either file missing, mismatched or short one
   record rejects the pack with the queue untouched.
2. Header required as line 1; `counts` checked against what was parsed.
3. Every id in D2 recomputed from the bytes read; any mismatch rejects.
4. `canonical_json(parse(line)) == line` for every line of both files.
5. Records sorted by `puzzle_id`, 1:1 across the files, no orphans.
6. `visible_bar_count` / `continuation_bar_count` / `response_horizon_bars`
   identical for every record and inside the D3 bounds.
7. The four `verified_stats` recomputed from the visible bars, 1e-6 tolerance.
8. Unknown `stat_id`, unknown `theme`, unknown label / size band / ATR multiple
   / follow-up action / artifact class -> rejection, not pass-through.
9. `theme.knowable_at_t` required true for every visible theme.
10. `rating_seed` inside the header band and its basis in the permitted set.
11. No translation unit under `apps/desktop/` includes the continuation header
    (source-scan test).
12. The vault refuses to open before a committed terminal event, and refuses a
    forged ticket.
13. Consumer profile bounds enforced: 512 bars, 64 HUD stats, 32 plies, 16
    themes, 48 nesting levels, integers <= 2^53 - 1.
14. Every surface that shows a scoring key labels it as the declared rule's
    line, not the right answer.

    Every one of these may be checked against ParlAWL's own local fixture
    (`market_task_kinds_v1.*`), which is the only pack carrying all three task
    kinds. Separately, and not substitutably: the Arc-generated golden pair
    (`market_puzzle_pack_v1.*`) must load through this loader with every digest
    recomputed, and must hash to the SHA-256 that Arc pins for the same bytes
    (the ParlAWL half of item 19).

Arc (`python/dojo/` chartered as non-evidence; scoped tests during the loop,
full suite once before reporting):

15. `arcpy` does not import `dojo` (import-graph test, as the scout boundary is
    kept).
16. Look-ahead test in the spirit of `python/tests/test_lookahead.py`: perturb
    every bar after T, assert the visible file is byte-identical.
17. Anonymization test: no real ticker, no absolute date, no un-normalized
    price anywhere in the visible file; `close_T == 100.0` exactly. (Note what
    this does *not* claim: D6 states as residual risk that a corpus holder
    re-identifies the window from its bar shapes regardless.)
18. No visible field is a function of continuation data (asserted per field for
    theme, rating seed, HUD, response spec).
19. Golden fixture round-trips against a fresh compile **and** hashes to the
    SHA-256 pinned in ParlAWL, which pins the same two constants against its
    copied bytes. "Round-trips against its own copy" in each repo separately
    does not satisfy this item and once hid two entirely different packs; see
    §8, "The one thing that must be shared with Arc".
20. Build manifest declares before compiling and finalizes with counts; a
    second write into a finished directory is refused.
