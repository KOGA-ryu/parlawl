from __future__ import annotations

import json
from typing import Any

try:
    from workers.analysis_py.critical_move_formatting import (
        choose_conflict_winner,
        collapse_sequence_fields,
        critical_move_record,
        divergence_summary,
        format_collapse_sequence_summary,
        format_omission_reason_summary,
        format_retained_break_summary,
        local_sequence_confidence,
        normalize_omission_reason,
        omission_reason_counts,
        primary_break_candidate,
        primary_break_reason,
        retained_break_fields,
        retained_role_summary,
        role_priority,
        roles_are_adjacent_conflict,
    )
except ModuleNotFoundError:
    from critical_move_formatting import (
        choose_conflict_winner,
        collapse_sequence_fields,
        critical_move_record,
        divergence_summary,
        format_collapse_sequence_summary,
        format_omission_reason_summary,
        format_retained_break_summary,
        local_sequence_confidence,
        normalize_omission_reason,
        omission_reason_counts,
        primary_break_candidate,
        primary_break_reason,
        retained_break_fields,
        retained_role_summary,
        role_priority,
        roles_are_adjacent_conflict,
    )


DECISIVE_SWING_CP = 300
LAST_HOLDING_DELTA_CP = 140
PREVENTATIVE_DELTA_CP = 100
MISSED_COUNTERPLAY_DELTA_CP = 120
DEFENSIBLE_FLOOR_CP = -120
COUNTERPLAY_FLOOR_CP = 40


def move_snapshot(move_record: dict[str, Any], field: str, snapshot_from_fields) -> dict[str, Any]:
    if field == "eval_after_played_cp":
        mate_flag = move_record.get("played_mate_flag", "unknown")
    elif field == "eval_after_best_cp":
        mate_flag = move_record.get("best_mate_flag", "unknown")
    elif field == "eval_before_cp":
        mate_flag = move_record.get("before_mate_flag", "unknown")
    else:
        mate_flag = move_record.get("mate_flag", "unknown")
    return snapshot_from_fields(move_record.get(field), mate_flag)


def move_numeric_eval(move_record: dict[str, Any], field: str, snapshot_from_fields) -> int | None:
    return move_snapshot(move_record, field, snapshot_from_fields).get("numeric")


def choose_decisive_blunder(window_analysis: list[dict[str, Any]], collapse_side: str) -> dict[str, Any] | None:
    decisive_candidates = [
        move for move in window_analysis
        if move["mate_flag"] != "none"
        or (move["eval_delta_cp"] is not None and move["eval_delta_cp"] >= DECISIVE_SWING_CP)
    ]
    if not decisive_candidates:
        return None
    collapse_candidates = [move for move in decisive_candidates if move["side"] == collapse_side]
    scored_candidates = collapse_candidates if collapse_candidates else decisive_candidates
    return max(
        scored_candidates,
        key=lambda move: (
            1 if move["decisive_swing"] == "mate_swing" else 0,
            move["eval_delta_cp"] or -1,
            move["ply"],
        ),
    )


def choose_last_holding_defense(
    window_analysis: list[dict[str, Any]],
    collapse_side: str,
    decisive_ply: int | None,
    used_plies: set[int],
    snapshot_from_fields,
) -> dict[str, Any] | None:
    upper_bound = decisive_ply if decisive_ply is not None else 10**9
    candidates = [
        move for move in window_analysis
        if move["side"] == collapse_side
        and move["ply"] < upper_bound
        and move["ply"] not in used_plies
        and move["eval_delta_cp"] is not None
        and move["eval_delta_cp"] >= LAST_HOLDING_DELTA_CP
        and (
            move_numeric_eval(move, "eval_after_best_cp", snapshot_from_fields) is not None
            and move_numeric_eval(move, "eval_after_best_cp", snapshot_from_fields) >= DEFENSIBLE_FLOOR_CP
        )
        and move["only_move_status"] in {"only_move", "near_only_move", "forced_defense_resource"}
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda move: move["ply"])


def choose_preventative_resource(
    window_analysis: list[dict[str, Any]],
    collapse_side: str,
    cutoff_ply: int,
    used_plies: set[int],
    snapshot_from_fields,
) -> dict[str, Any] | None:
    candidates = [
        move for move in window_analysis
        if move["side"] == collapse_side
        and move["ply"] <= cutoff_ply - 2
        and move["ply"] not in used_plies
        and move["eval_delta_cp"] is not None
        and move["eval_delta_cp"] >= PREVENTATIVE_DELTA_CP
        and (
            move_numeric_eval(move, "eval_after_best_cp", snapshot_from_fields) is not None
            and move_numeric_eval(move, "eval_after_best_cp", snapshot_from_fields) >= DEFENSIBLE_FLOOR_CP
        )
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda move: (move["ply"], move["eval_delta_cp"]))


def choose_missed_counterplay(
    window_analysis: list[dict[str, Any]],
    collapse_side: str,
    used_plies: set[int],
    snapshot_from_fields,
) -> dict[str, Any] | None:
    candidates = [
        move for move in window_analysis
        if move["side"] == collapse_side
        and move["ply"] not in used_plies
        and move["eval_delta_cp"] is not None
        and move["eval_delta_cp"] >= MISSED_COUNTERPLAY_DELTA_CP
        and (
            move_numeric_eval(move, "eval_after_best_cp", snapshot_from_fields) is not None
            and move_numeric_eval(move, "eval_after_best_cp", snapshot_from_fields) >= COUNTERPLAY_FLOOR_CP
        )
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda move: (move["eval_delta_cp"], move["ply"]))


def detect_critical_moves(
    window_analysis: list[dict[str, Any]],
    collapse_side: str,
    mapped_start_ply: int,
    snapshot_from_fields,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    emitted: list[dict[str, Any]] = []
    used_plies: set[int] = set()
    candidate_entries: list[dict[str, Any]] = []
    omitted_adjacent: list[dict[str, Any]] = []

    def alternative_clause(move: dict[str, Any]) -> str:
        count = move.get("stronger_alternative_count", 0)
        if count <= 0:
            return "the played move matched the strongest analysed local candidate"
        if count <= 1:
            return "the played move was materially inferior to one stronger local alternative"
        return f"the played move was materially inferior to {count} stronger local alternatives"

    decisive = choose_decisive_blunder(window_analysis, collapse_side)
    if decisive is not None:
        candidate_entries.append(
            {
                "role": "decisive_blunder",
                "move": decisive,
                "why": f"local engine comparison found the largest decisive swing or mate-related collapse in the tactical window; {alternative_clause(decisive)}",
            }
        )
        used_plies.add(decisive["ply"])

    last_holding = choose_last_holding_defense(
        window_analysis,
        collapse_side,
        decisive["ply"] if decisive is not None else None,
        used_plies,
        snapshot_from_fields,
    )
    if last_holding is not None:
        candidate_entries.append(
            {
                "role": "last_holding_defense",
                "move": last_holding,
                "why": f"a stronger move in the local window preserved a materially better defensive evaluation and was near-only or only-move level; {alternative_clause(last_holding)}",
            }
        )
        used_plies.add(last_holding["ply"])

    preventative_cutoff = last_holding["ply"] if last_holding is not None else (decisive["ply"] if decisive is not None else mapped_start_ply)
    preventative = choose_preventative_resource(window_analysis, collapse_side, preventative_cutoff, used_plies, snapshot_from_fields)
    if preventative is not None:
        candidate_entries.append(
            {
                "role": "preventative_resource",
                "move": preventative,
                "why": f"an earlier local move avoided the later collapse while keeping the evaluation inside a defensible band; {alternative_clause(preventative)}",
            }
        )
        used_plies.add(preventative["ply"])

    missed_counterplay = choose_missed_counterplay(window_analysis, collapse_side, used_plies, snapshot_from_fields)
    if missed_counterplay is not None:
        candidate_entries.append(
            {
                "role": "missed_counterplay",
                "move": missed_counterplay,
                "why": f"the local window contained a clearly stronger forcing or active resource that the played move missed; {alternative_clause(missed_counterplay)}",
            }
        )
        used_plies.add(missed_counterplay["ply"])

    primary_break_ply = decisive["ply"] if decisive is not None else None

    retained_candidates: list[dict[str, Any]] = []
    for candidate in sorted(candidate_entries, key=lambda item: (item["move"]["ply"], -role_priority(item["role"]))):
        conflicting = next(
            (
                retained
                for retained in retained_candidates
                if roles_are_adjacent_conflict(retained, candidate, primary_break_ply)
            ),
            None,
        )
        if conflicting is None:
            retained_candidates.append(candidate)
            continue

        winner, loser, reason = choose_conflict_winner(conflicting, candidate)
        normalized_reason = normalize_omission_reason(winner, loser, reason, primary_break_ply)
        omitted_adjacent.append(
            {
                "role": loser["role"],
                "ply": loser["move"]["ply"],
                "reason": normalized_reason,
            }
        )
        winner["why"] = f"{winner['why']} Retained after adjacent-ply conflict resolution: {normalized_reason}."
        if winner is candidate:
            retained_candidates = [winner if retained is conflicting else retained for retained in retained_candidates]

    for candidate in sorted(retained_candidates, key=lambda item: item["move"]["ply"]):
        emitted.append(critical_move_record("__event__", candidate["role"], candidate["move"], candidate["why"]))

    if emitted:
        primary_candidate = primary_break_candidate(retained_candidates, primary_break_ply)
        sequence_fields = collapse_sequence_fields(primary_break_ply, retained_candidates, len(omitted_adjacent))
        omission_counts = omission_reason_counts(omitted_adjacent)
        break_fields = retained_break_fields(primary_candidate) if primary_candidate is not None else {
            "format_version": "rbsv1",
            "role": "",
            "played_move": "",
            "best_move": "",
            "divergence_type": "",
            "compact_sequence": "",
        }
        divergence = divergence_summary(primary_candidate) if primary_candidate is not None else {
            "type": "tactical_resource_lost",
            "severity": "low",
            "compact_summary": "local divergence",
            "evidence_notes": "",
            "summary": "",
        }
        return emitted, {
            "primary_break_ply": primary_break_ply or retained_candidates[0]["move"]["ply"],
            "primary_break_reason": primary_break_reason(primary_candidate) if primary_candidate is not None else "local_break",
            "retained_break_format_version": break_fields["format_version"],
            "retained_break_role": break_fields["role"],
            "retained_break_played_move": break_fields["played_move"],
            "retained_break_best_move": break_fields["best_move"],
            "retained_break_compact_sequence": break_fields["compact_sequence"],
            "retained_break_summary": (
                format_retained_break_summary(break_fields, primary_break_ply or retained_candidates[0]["move"]["ply"])
                if primary_candidate is not None
                else ""
            ),
            "best_vs_played_divergence_summary": divergence["summary"],
            "divergence_type": divergence["type"],
            "divergence_severity": divergence["severity"],
            "divergence_compact_summary": divergence["compact_summary"],
            "divergence_evidence_notes": divergence["evidence_notes"],
            "local_sequence_confidence": local_sequence_confidence(primary_candidate, len(omitted_adjacent)) if primary_candidate is not None else "low",
            "collapse_sequence_format_version": sequence_fields["format_version"],
            "collapse_sequence_type": sequence_fields["type"],
            "collapse_sequence_summary": format_collapse_sequence_summary(sequence_fields),
            "retained_role_summary": retained_role_summary(retained_candidates),
            "omitted_adjacent_candidate_count": len(omitted_adjacent),
            "omission_reason_summary": format_omission_reason_summary(omission_counts),
            "omission_reason_counts_json": json.dumps(omission_counts, sort_keys=True),
            "omitted_adjacent_candidates": omitted_adjacent,
        }

    fallback = max(
        window_analysis,
        key=lambda move: (move["eval_delta_cp"] or -1, move["ply"]),
        default=None,
    )
    if fallback is None:
        raise ValueError("evidence extraction failure: no analysable moves were found in the local puzzle window")

    fallback_role = "decisive_blunder" if fallback["decisive_swing"] in {"decisive", "mate_swing"} else "last_holding_defense"
    fallback_record = critical_move_record(
        "__event__",
        fallback_role,
        fallback,
        f"fallback local engine comparison emitted the strongest available move difference inside the tactical window; {alternative_clause(fallback)}",
    )
    fallback_candidate = {"role": fallback_role, "move": fallback}
    divergence = divergence_summary(fallback_candidate)
    break_fields = retained_break_fields(fallback_candidate)
    sequence_fields = collapse_sequence_fields(fallback["ply"], [fallback_candidate], 0)
    return [fallback_record], {
        "primary_break_ply": fallback["ply"],
        "primary_break_reason": "collapse_trigger" if fallback_role == "decisive_blunder" else "narrow_missed_defense",
        "retained_break_format_version": break_fields["format_version"],
        "retained_break_role": break_fields["role"],
        "retained_break_played_move": break_fields["played_move"],
        "retained_break_best_move": break_fields["best_move"],
        "retained_break_compact_sequence": break_fields["compact_sequence"],
        "retained_break_summary": format_retained_break_summary(break_fields, fallback["ply"]),
        "best_vs_played_divergence_summary": divergence["summary"],
        "divergence_type": divergence["type"],
        "divergence_severity": divergence["severity"],
        "divergence_compact_summary": divergence["compact_summary"],
        "divergence_evidence_notes": divergence["evidence_notes"],
        "local_sequence_confidence": "medium",
        "collapse_sequence_format_version": sequence_fields["format_version"],
        "collapse_sequence_type": sequence_fields["type"],
        "collapse_sequence_summary": format_collapse_sequence_summary(sequence_fields),
        "retained_role_summary": f"{fallback_role}@{fallback['ply']}",
        "omitted_adjacent_candidate_count": 0,
        "omission_reason_summary": "none",
        "omission_reason_counts_json": "{}",
        "omitted_adjacent_candidates": [],
    }
