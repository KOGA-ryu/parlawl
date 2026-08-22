#!/usr/bin/env python3
"""Emit ParlAWL's LOCAL task-kinds fixture pair. This is not the golden pair.

Read this before assuming otherwise, because an earlier version of this file
claimed to be the golden pair and was not:

* The **golden pair** of contract §8 is `market_puzzle_pack_v1.{visible,sealed}
  .jsonl`, produced by Arc's real `python/dojo` compiler and copied into
  `tests/fixtures/` byte-for-byte. That is the only cross-language artifact —
  Python encoder on one side, C++ loader on the other. This script must never
  write those filenames.
* The pair written **here** is `market_task_kinds_v1.{visible,sealed}.jsonl`,
  hand-built by this second encoder, carrying one record per task kind.

It exists because no Arc-compiled pack can cover the three task kinds at once:
a pack spec carries a single `task_kind`, and Arc's `compile.py` refuses
`trade_line` outright ("trade_line needs a declared rule to replay and this
compiler has none"). So the loader's three shapes are exercised locally, and
the encoder agreement is proven against Arc's bytes instead. Being a second
canonical-JSON encoder, this file agrees with the loader only about itself —
that is the price of the coverage, and it is why the name says so.

Deliberately included spellings, because they are where encoders diverge:
`381.0`, `-0.0`, `1e+20`, and `null` fields that are absent rather than zeroed.

Usage:  python3 tools/generate_market_task_kinds_fixture.py [output-dir]
"""

from __future__ import annotations

import hashlib
import json
import math
import pathlib
import sys

GRAIN = "1d"
VISIBLE_BARS = 24
CONTINUATION_BARS = 8
HORIZON = 5
PRICE_DECIMALS = 6
INTERVAL_LEVEL = 0.8
BOUND_LOWER = -95.0
BOUND_UPPER = 400.0

EVIDENCE_GRADE = (
    "NOT EVIDENCE. A puzzle pack is training material compiled from the corpus. "
    "Its scoring key is a DECLARED RULE'S line replayed on the continuation, not "
    "the optimal line and not a claim that the rule has an edge."
)

COMPILER = {
    "built_at_utc": "2026-08-21T04:15:00Z",
    "compiler_id": "arc.dojo.compile",
    "compiler_version": "1",
    "git_commit": "40739c8a1f2b4c6d8e0a2b4c6d8e0a2b4c6d8e0a",
    "git_dirty": False,
}

SOURCE = {
    "adjustment_table_sha256": "b" * 64,
    "corpus_dataset_version": "bars_1d/2026-06-01",
    "scan_citation": (
        "selected 500 from 62,412,880 candidate cells by rule 'attention_v1' "
        "[scan 20260821T041500Z-attention_v1-42e15520, commit 40739c8]"
    ),
    "scan_manifest_id": "20260821T041500Z-attention_v1-42e15520",
}


def canonical(value) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def digest(value) -> str:
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def lcg(seed: int):
    state = seed

    def nxt() -> float:
        nonlocal state
        state = (state * 6364136223846793005 + 1442695040888963407) % (2**64)
        return (state >> 11) / float(2**53)

    return nxt


def raw_series(seed: int, count: int, start: float) -> list[tuple[float, float, float, float, float]]:
    """A deterministic OHLCV walk. No real market data lives in a fixture."""
    nxt = lcg(seed)
    rows = []
    close = start
    for _ in range(count):
        open_price = close
        drift = (nxt() - 0.48) * 0.03
        close = max(1.0, open_price * (1.0 + drift))
        high = max(open_price, close) * (1.0 + nxt() * 0.012)
        low = min(open_price, close) * (1.0 - nxt() * 0.012)
        volume = 800_000.0 + nxt() * 900_000.0
        rows.append((open_price, high, low, close, volume))
    return rows


def normalize(rows, anchor_close: float, median_volume: float, holes: set[int]):
    """`close_T == 100.0` exactly; volume in window-median units.

    A missing field is `null`, never `0` and never carried forward.
    """
    bars = []
    for index, (o, h, l, c, v) in enumerate(rows):
        if index in holes:
            # A halt is a real thing to have to read.
            bars.append([index, None, None, None, None, None, None])
            continue
        scale = 100.0 / anchor_close
        row = [
            index,
            round(o * scale, PRICE_DECIMALS),
            round(h * scale, PRICE_DECIMALS),
            round(l * scale, PRICE_DECIMALS),
            round(c * scale, PRICE_DECIMALS),
            round(v / median_volume, PRICE_DECIMALS),
            round(v / median_volume / 240.0, PRICE_DECIMALS),
        ]
        bars.append(row)
    return bars


def true_ranges(bars) -> list[float]:
    ranges = []
    for index in range(1, len(bars)):
        high = bars[index][2]
        low = bars[index][3]
        previous_close = bars[index - 1][4]
        ranges.append(
            max(high - low, abs(high - previous_close), abs(low - previous_close))
        )
    return ranges


def atr_pct_20(bars) -> float:
    """Wilder's SEED ATR over the LAST 20 bars: mean of TR[N-20 .. N-1].

    The contract's `atr_pct_20` reads only the final 21 bars. Matches
    `dojo/hud.py::atr_pct_20` and `libs/market/market_hud.cpp::atrPercent20`.
    """
    ranges = true_ranges(bars)[-20:]
    return 100.0 * (sum(ranges) / 20.0) / bars[-1][4]


def simple_return(bars, lookback: int) -> float:
    return 100.0 * (bars[-1][4] / bars[-1 - lookback][4] - 1.0)


def range_position_20(bars) -> float:
    window = bars[-20:]
    lowest = min(bar[3] for bar in window)
    highest = max(bar[2] for bar in window)
    return (bars[-1][4] - lowest) / (highest - lowest)


def hud_for(bars, session_breaks) -> list[dict]:
    stats = [
        ("atr_pct_20", atr_pct_20(bars), "percent"),
        ("range_position_20", range_position_20(bars), "ratio"),
        ("return_20", simple_return(bars, 20), "percent"),
        ("return_5", simple_return(bars, 5), "percent"),
        ("session_break_count", float(sum(1 for flag in session_breaks if flag)), "count"),
        # A float that must be spelled `381.0`, not `381`.
        ("volume_ratio_20", 381.0, "ratio"),
    ]
    return [{"stat_id": key, "unit": unit, "value": value} for key, value, unit in stats]


def plies_for(task_kind: str) -> list[dict]:
    if task_kind == "pattern_call":
        return [
            {"ply_index": 0, "ply_kind": "label"},
            {"ply_index": 1, "ply_kind": "confidence"},
        ]
    if task_kind == "anomaly_flag":
        return [
            {"ply_index": 0, "ply_kind": "verdict"},
            {"ply_index": 1, "ply_kind": "artifact_class"},
            {"ply_index": 2, "ply_kind": "confidence"},
        ]
    plies = [
        {"ply_index": 0, "ply_kind": "entry"},
        {"ply_index": 1, "ply_kind": "size_band"},
        {"ply_index": 2, "ply_kind": "bracket"},
    ]
    for offset in range(1, HORIZON + 1):
        plies.append({"bar_offset": offset, "ply_index": 2 + offset, "ply_kind": "follow_up"})
    return plies


def scoring_key_for(task_kind: str) -> dict:
    if task_kind == "pattern_call":
        return {
            "correct_label": "reversal",
            "label_derivation": (
                "sign(close[T+5]/close[T]-1) against +/-1.0 * atr_pct_20; "
                "|move| < 1 ATR => chop"
            ),
            "task_kind": "pattern_call",
        }
    if task_kind == "anomaly_flag":
        return {
            "artifact_class": "stale_print_repeat",
            "clean_provenance": None,
            "injection_spec": {
                "bars": [11, 12, 13],
                # A signed zero that must survive as `-0.0`.
                "magnitude": -0.0,
                "transform": "repeat_close_of_bar_10",
            },
            "planted": True,
            "task_kind": "anomaly_flag",
        }
    line = [
        {"key": "short", "ply_index": 0, "ply_kind": "entry"},
        {"key": "0.5R", "ply_index": 1, "ply_kind": "size_band"},
        {"key": {"stop_atr": 1.5, "target_atr": 3.0}, "ply_index": 2, "ply_kind": "bracket"},
    ]
    for offset in range(1, HORIZON + 1):
        line.append(
            {
                "bar_offset": offset,
                "key": "exit" if offset == HORIZON else "hold",
                "ply_index": 2 + offset,
                "ply_kind": "follow_up",
            }
        )
    return {
        "line": line,
        "line_outcome": {"exit_bar_offset": 4, "exit_reason": "target", "r_multiple": 1.82},
        "perfect_outcome": {
            "note": "best achievable within the horizon and grids",
            "r_multiple": 2.94,
        },
        "rule_declaration_digest": "c" * 64,
        "rule_id": "u2.fade_v1",
        "task_kind": "trade_line",
    }


def build_record(index: int, task_kind: str, theme: str, seed: int, rating_seed: int):
    visible_raw = raw_series(seed, VISIBLE_BARS, 40.0 + index * 7.0)
    continuation_raw = raw_series(seed + 977, CONTINUATION_BARS, visible_raw[-1][3])
    anchor = visible_raw[-1][3]
    volumes = sorted(row[4] for row in visible_raw)
    median_volume = volumes[len(volumes) // 2]

    # One bar carries volume it does not have. OHLC stays present so the
    # 20-period verified stats remain derivable from the window itself.
    visible_bars = normalize(visible_raw, anchor, median_volume, holes=set())
    visible_bars[3][5] = None
    visible_bars[3][6] = None

    # A fully halted bar after T, so the trade-line simulator has to carry a
    # position across a bar it cannot fill in.
    continuation_bars = normalize(
        continuation_raw, anchor, median_volume, holes={2}
    )

    visible_breaks = [(i % 9) == 8 for i in range(VISIBLE_BARS)]
    continuation_breaks = [(i % 5) == 4 for i in range(CONTINUATION_BARS)]

    window_digest = digest(visible_bars)
    realized = round(
        100.0 * (continuation_bars[HORIZON - 1][4] / visible_bars[-1][4] - 1.0), PRICE_DECIMALS
    )

    content = {
        "calibration_key": {
            "derivation": "100 * (close[T+5]/close[T] - 1) on the normalized continuation",
            "question_id": "return_pct_at_horizon_v1",
            "realized_value": realized,
        },
        "continuation": {
            "bar_count": CONTINUATION_BARS,
            "bars": continuation_bars,
            "grain": GRAIN,
            "session_break_after": continuation_breaks,
        },
        "difficulty_note": {
            "basis_id": "outcome_dispersion_v1",
            # Deliberately absurd on one record: `1e+20` is the float spelling
            # most likely to diverge between encoders.
            "value": 1e20 if task_kind == "anomaly_flag" else 0.71,
        },
        "outcome_theme": "false_breakout",
        "reveal_identity": {
            "decision_time_utc": "2021-02-08T21:00:00Z",
            "exchange": "XNAS",
            "instrument_class": "common_stock",
            "ticker": f"FIXT{index}",
        },
        "scoring_key": scoring_key_for(task_kind),
        "source_identity": {
            "bars_root_id": "bars_1d/2026-06-01",
            "partition_paths": [
                "partition_year=2021/partition_month=02/trade_date=2021-02-08"
            ],
            "source_continuation_digest": hashlib.sha256(
                f"continuation-{index}".encode("utf-8")
            ).hexdigest(),
            "source_window_digest": hashlib.sha256(
                f"window-{index}".encode("utf-8")
            ).hexdigest(),
        },
    }
    commitment = "market-continuation-v1:" + digest(content)
    puzzle_id = "market-puzzle-v1:" + digest(
        {
            "continuation_commitment": commitment,
            "grain": GRAIN,
            "response_horizon_bars": HORIZON,
            "task_kind": task_kind,
            "window_digest": window_digest,
        }
    )

    visible_body = {
        "calibration_question": {
            "bounds": {"lower": BOUND_LOWER, "upper": BOUND_UPPER},
            "interval_level": INTERVAL_LEVEL,
            "quantity": "return_pct_at_horizon",
            "question_id": "return_pct_at_horizon_v1",
            "unit": "percent",
        },
        "continuation_commitment": commitment,
        "display_symbol": f"SYM-{index:04d}",
        "hud": hud_for(visible_bars, visible_breaks),
        "puzzle_id": puzzle_id,
        "rating_seed": rating_seed,
        "rating_seed_basis": "atr_percentile_v1",
        "response_spec": {"horizon_bars": HORIZON, "plies": plies_for(task_kind)},
        "source": {
            "adjustment_table_sha256": SOURCE["adjustment_table_sha256"],
            "corpus_dataset_version": SOURCE["corpus_dataset_version"],
            "grain": GRAIN,
            "scan_manifest_id": SOURCE["scan_manifest_id"],
        },
        "task_kind": task_kind,
        "theme": theme,
        "window": {
            "bar_count": VISIBLE_BARS,
            "bars": visible_bars,
            "grain": GRAIN,
            "session_break_after": visible_breaks,
            "window_digest": window_digest,
        },
    }
    visible = dict(visible_body)
    visible["record_id"] = "market-puzzle-record-v1:" + digest(visible_body)
    visible["record_type"] = "market_puzzle"
    visible["schema"] = "arc/market-puzzle/v1"

    sealed_body = {"content": content, "puzzle_id": puzzle_id}
    sealed = dict(sealed_body)
    sealed["record_id"] = "market-continuation-record-v1:" + digest(sealed_body)
    sealed["record_type"] = "market_continuation"
    sealed["schema"] = "arc/market-continuation/v1"
    return visible, sealed


def main() -> int:
    out_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures")
    specs = [
        ("trade_line", "range_compression", 20260821, 1720),
        ("pattern_call", "post_halt_reopen", 20260822, 1480),
        ("anomaly_flag", "range_compression", 20260823, 2010),
    ]
    pairs = [build_record(i, *spec) for i, spec in enumerate(specs)]
    # Sorted by `puzzle_id` ascending. A hash order is independent of ticker,
    # date, outcome, task kind and planted-ness; any other order is a channel.
    pairs.sort(key=lambda pair: pair[0]["puzzle_id"])

    sealed_header_body = {
        "built_at_utc": COMPILER["built_at_utc"],
        "compiler": COMPILER,
        "evidence_grade": EVIDENCE_GRADE,
        "record_count": len(pairs),
        "record_type": "market_continuation_pack_header",
        "schema": "arc/market-continuation-pack/v1",
    }
    continuation_pack_id = "market-continuation-pack-v1:" + digest(sealed_header_body)
    sealed_header = dict(sealed_header_body)
    sealed_header["continuation_pack_id"] = continuation_pack_id

    counts = {"anomaly_flag": 0, "pattern_call": 0, "trade_line": 0}
    for visible, _ in pairs:
        counts[visible["task_kind"]] += 1

    header_body = {
        "anonymization": {
            "calendar_disclosed": False,
            "session_breaks_disclosed": True,
            "symbol_scheme": "opaque_pack_local_v1",
        },
        "calibration": {
            "bounds": {"lower": BOUND_LOWER, "upper": BOUND_UPPER},
            "interval_level": INTERVAL_LEVEL,
            "quantity": "return_pct_at_horizon",
            "question_template": "return_pct_at_horizon_v1",
            "unit": "percent",
        },
        "compiler": COMPILER,
        "continuation_bar_count": CONTINUATION_BARS,
        "continuation_pack_id": continuation_pack_id,
        "counts": {"by_task_kind": counts, "records": len(pairs)},
        "evidence_grade": EVIDENCE_GRADE,
        "grain": GRAIN,
        "hud_spec": {
            "stats": [
                "atr_pct_20",
                "range_position_20",
                "return_20",
                "return_5",
                "session_break_count",
                "volume_ratio_20",
            ],
            "verified_stats": ["atr_pct_20", "range_position_20", "return_20", "return_5"],
        },
        "normalization": {
            "price_anchor": 100.0,
            "price_decimals": PRICE_DECIMALS,
            "volume_basis": "window_median",
            "volume_decimals": PRICE_DECIMALS,
        },
        "rating": {"band": {"max": 3000, "min": 400}, "basis_id": "atr_percentile_v1"},
        "record_type": "market_puzzle_pack_header",
        "response_horizon_bars": HORIZON,
        "schema": "arc/market-puzzle-pack/v1",
        "source": SOURCE,
        "task_spec": {
            "anomaly_flag": {
                "artifact_classes": [
                    "halt_gap_zero_filled",
                    "split_unadjusted",
                    "stale_print_repeat",
                    "test_symbol_print",
                    "time_shifted_block",
                    "wash_volume_burst",
                ]
            },
            "pattern_call": {"labels": ["chop", "continuation", "reversal"]},
            "trade_line": {
                "entries": ["long", "pass", "short"],
                "follow_up_actions": ["add", "exit", "hold", "tighten"],
                "size_bands": ["0", "0.25R", "0.5R", "1R"],
                "stop_atr_multiples": [0.5, 1.0, 1.5, 2.0, 3.0],
                "target_atr_multiples": [1.0, 2.0, 3.0, 4.0, 6.0],
            },
        },
        "themes": [
            {"knowable_at_t": True, "theme": "post_halt_reopen"},
            {"knowable_at_t": True, "theme": "range_compression"},
        ],
        "visible_bar_count": VISIBLE_BARS,
    }
    header = dict(header_body)
    header["pack_id"] = "market-puzzle-pack-v1:" + digest(header_body)

    visible_lines = [canonical(header)] + [canonical(pair[0]) for pair in pairs]
    sealed_lines = [canonical(sealed_header)] + [canonical(pair[1]) for pair in pairs]

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "market_task_kinds_v1.visible.jsonl").write_text(
        "\n".join(visible_lines) + "\n", encoding="utf-8"
    )
    (out_dir / "market_task_kinds_v1.sealed.jsonl").write_text(
        "\n".join(sealed_lines) + "\n", encoding="utf-8"
    )
    for line in visible_lines + sealed_lines:
        assert canonical(json.loads(line)) == line, "fixture line is not canonical"
        assert len(line.encode("utf-8")) <= 1024 * 1024
    print(header["pack_id"])
    print(continuation_pack_id)
    for visible, _ in pairs:
        print(visible["task_kind"], visible["puzzle_id"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
