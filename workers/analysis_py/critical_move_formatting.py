from __future__ import annotations

import json
from typing import Any
from uuid import uuid4


DECISIVE_SWING_CP = 300
SIGNIFICANT_SWING_CP = 120
ADJACENT_CONFLICT_PLIES = 1


def critical_reason_fields(role: str, move: dict[str, Any]) -> dict[str, str]:
    if "mate_against_mover" in move["mate_flag"]:
        return {
            "type": "mating_line_allowed",
            "severity": "critical",
            "compact_summary": "mate allowed",
        }
    if "mate_for_mover" in move.get("best_mate_flag", "none") or move["only_move_status"] == "forced_mate_resource":
        return {
            "type": "mating_line_preserved",
            "severity": "critical",
            "compact_summary": "mate preserved",
        }
    if move["only_move_status"] == "forced_defense_resource":
        return {
            "type": "forced_defense_resource",
            "severity": "critical",
            "compact_summary": "forced defense missed",
        }
    if move["only_move_status"] == "only_move":
        return {
            "type": "only_move_resource",
            "severity": "major",
            "compact_summary": "only move missed",
        }
    if role == "preventative_resource":
        return {
            "type": "preventative_hold_available",
            "severity": "moderate",
            "compact_summary": "preventative hold available",
        }
    if move["only_move_status"] == "near_only_move":
        return {
            "type": "defensible_line_lost",
            "severity": "moderate",
            "compact_summary": "defensible line lost",
        }
    if move.get("stronger_alternative_count", 0) > 1:
        return {
            "type": "stronger_alternative_missed",
            "severity": "major" if (move["eval_delta_cp"] or 0) >= SIGNIFICANT_SWING_CP else "moderate",
            "compact_summary": "stronger alternative missed",
        }
    if move["eval_delta_cp"] is not None and move["eval_delta_cp"] >= DECISIVE_SWING_CP:
        return {
            "type": "eval_collapse_trigger",
            "severity": "major",
            "compact_summary": f"{move['eval_delta_cp']} cp collapse",
        }
    return {
        "type": "tactical_resource_lost",
        "severity": "moderate" if (move["eval_delta_cp"] or 0) >= SIGNIFICANT_SWING_CP else "minor",
        "compact_summary": (
            f"{move['eval_delta_cp']} cp lost"
            if move["eval_delta_cp"] is not None
            else "tactical resource lost"
        ),
    }


def continuation_compact_fields(reason: dict[str, str], move: dict[str, Any] | None = None) -> dict[str, str]:
    mapping = {
        "mating_line_allowed": ("mate avoided", "mate appears"),
        "mating_line_preserved": ("mate preserved", "mate lost"),
        "forced_defense_resource": ("forced defense held", "forced defense missed"),
        "only_move_resource": ("only move held", "only move missed"),
        "stronger_alternative_missed": ("stronger line retained", "stronger line missed"),
        "defensible_line_lost": ("defensible line kept", "defensible line lost"),
        "eval_collapse_trigger": ("cp collapse avoided", reason["compact_summary"]),
        "preventative_hold_available": ("preventative hold kept", "preventative hold missed"),
        "tactical_resource_lost": ("tactical resource kept", "tactical resource lost"),
    }
    best, played = mapping.get(reason["type"], ("best line retained", "played line lost"))
    if move is not None and move.get("played_move") == move.get("best_move"):
        if reason["type"] == "mating_line_allowed":
            best = played = "mate appears"
        elif reason["type"] == "mating_line_preserved":
            best = played = "mate preserved"
        elif reason["type"] == "forced_defense_resource":
            best = played = "forced defense held"
        elif reason["type"] == "only_move_resource":
            best = played = "only move held"
        elif reason["type"] == "stronger_alternative_missed":
            best = played = "stronger line retained"
        elif reason["type"] == "defensible_line_lost":
            best = played = "defensible line kept"
        elif reason["type"] == "eval_collapse_trigger":
            best = played = reason["compact_summary"]
        elif reason["type"] == "preventative_hold_available":
            best = played = "preventative hold kept"
        elif reason["type"] == "tactical_resource_lost":
            best = played = "tactical resource kept"
    return {
        "format_version": "cmv1",
        "best": best,
        "played": played,
    }


def format_why_critical(reason: dict[str, str]) -> str:
    return f"{reason['type']} | {reason['compact_summary']}"


def candidate_ranking_fields(move: dict[str, Any]) -> dict[str, str]:
    if (
        "mate" in move["mate_flag"]
        or move.get("best_mate_flag", "none") != "none"
        or move["only_move_status"] == "forced_mate_resource"
    ):
        return {
            "type": "mate_driven_choice",
            "severity": "critical",
            "compact_summary": "mate-driven choice",
        }
    if move["only_move_status"] in {"forced_defense_resource", "forced_mate_resource"}:
        return {
            "type": "forced_defense_choice",
            "severity": "critical",
            "compact_summary": "forced defense choice",
        }
    if move["only_move_status"] == "only_move":
        return {
            "type": "unique_best",
            "severity": "major",
            "compact_summary": "unique best move",
        }
    if move["only_move_status"] == "near_only_move":
        return {
            "type": "near_unique_best",
            "severity": "major",
            "compact_summary": "near-unique best move",
        }
    if move["eval_delta_cp"] is not None and move["eval_delta_cp"] >= DECISIVE_SWING_CP:
        return {
            "type": "played_collapse",
            "severity": "major",
            "compact_summary": f"{move['eval_delta_cp']} cp collapse",
        }
    if move.get("stronger_alternative_count", 0) > 1:
        return {
            "type": "several_strong_alternatives",
            "severity": "major" if (move["eval_delta_cp"] or 0) >= SIGNIFICANT_SWING_CP else "moderate",
            "compact_summary": f"{move.get('stronger_alternative_count', 0)} stronger alternatives",
        }
    return {
        "type": "played_close_but_inferior",
        "severity": "minor" if (move["eval_delta_cp"] or 0) < SIGNIFICANT_SWING_CP else "moderate",
        "compact_summary": "played move stayed close but inferior",
    }


def evidence_note_fields(role: str, move: dict[str, Any], reason: dict[str, str]) -> dict[str, str]:
    if reason["type"] == "mating_line_allowed":
        return {"type": "mate_allowed", "severity": "critical", "compact": "mate allowed"}
    if reason["type"] == "mating_line_preserved":
        return {"type": "mate_preserved", "severity": "critical", "compact": "mate preserved"}
    if reason["type"] == "forced_defense_resource":
        return {"type": "forced_defense_missed", "severity": "critical", "compact": "forced defense missed"}
    if reason["type"] == "only_move_resource":
        return {"type": "unique_best_missed", "severity": "major", "compact": "unique best missed"}
    if reason["type"] == "preventative_hold_available" or role == "preventative_resource":
        return {"type": "preventative_hold_missed", "severity": "moderate", "compact": "preventative hold missed"}
    if reason["type"] == "defensible_line_lost":
        return {"type": "defensible_line_lost", "severity": "moderate", "compact": "defensible line lost"}
    if reason["type"] == "stronger_alternative_missed":
        return {
            "type": "stronger_alternatives_missed",
            "severity": "major" if (move["eval_delta_cp"] or 0) >= SIGNIFICANT_SWING_CP else "moderate",
            "compact": f"{move.get('stronger_alternative_count', 0)} stronger alternatives missed",
        }
    if reason["type"] == "eval_collapse_trigger":
        return {
            "type": "collapse_triggered",
            "severity": "major",
            "compact": f"{move['eval_delta_cp']} cp collapse" if move["eval_delta_cp"] is not None else "collapse triggered",
        }
    return {"type": "tactical_resource_lost", "severity": reason["severity"], "compact": "tactical resource lost"}


def format_candidate_ranking_summary(fields: dict[str, str]) -> str:
    return f"{fields['type']} | {fields['compact_summary']}"


def critical_move_record(event_id: str, role: str, move: dict[str, Any], why_critical: str) -> dict[str, Any]:
    reason = critical_reason_fields(role, move)
    continuation = continuation_compact_fields(reason, move)
    ranking = candidate_ranking_fields(move)
    evidence_note = evidence_note_fields(role, move, reason)
    return {
        "critical_move_id": str(uuid4()),
        "event_id": event_id,
        "role": role,
        "ply": move["ply"],
        "side": move["side"],
        "played_move": move["played_move"],
        "best_move": move["best_move"],
        "why_critical": format_why_critical(reason),
        "eval_before_cp": move["eval_before_cp"],
        "eval_after_played_cp": move["eval_after_played_cp"],
        "eval_after_best_cp": move["eval_after_best_cp"],
        "eval_delta_cp": move["eval_delta_cp"],
        "decisive_swing": move["decisive_swing"],
        "analysis_depth": move["analysis_depth"],
        "mate_flag": move["mate_flag"],
        "critical_reason_type": reason["type"],
        "critical_reason_severity": reason["severity"],
        "critical_reason_compact_summary": reason["compact_summary"],
        "continuation_format_version": continuation["format_version"],
        "best_continuation_compact": continuation["best"],
        "played_continuation_compact": continuation["played"],
        "pv_uci": move["pv_uci"],
        "pv_san": move["pv_san"],
        "best_continuation_summary": move["best_continuation_summary"],
        "played_continuation_summary": move["played_continuation_summary"],
        "only_move_status": move["only_move_status"],
        "only_move_margin_cp": move["only_move_margin_cp"],
        "only_move_reasoning": move["only_move_reasoning"],
        "candidate_moves_json": move["candidate_moves_json"],
        "candidate_ranking_type": ranking["type"],
        "candidate_ranking_severity": ranking["severity"],
        "candidate_ranking_compact_summary": ranking["compact_summary"],
        "candidate_ranking_summary": format_candidate_ranking_summary(ranking),
        "evidence_note_type": evidence_note["type"],
        "evidence_note_severity": evidence_note["severity"],
        "evidence_note_compact": evidence_note["compact"],
        "stronger_alternative_count": move["stronger_alternative_count"],
    }


def role_priority(role: str) -> int:
    priorities = {
        "decisive_blunder": 4,
        "last_holding_defense": 3,
        "missed_counterplay": 2,
        "preventative_resource": 1,
    }
    return priorities.get(role, 0)


def only_move_priority(status: str) -> int:
    priorities = {
        "forced_mate_resource": 5,
        "forced_defense_resource": 4,
        "only_move": 3,
        "near_only_move": 2,
        "multiple_viable": 1,
        "unknown": 0,
    }
    return priorities.get(status, 0)


def candidate_support_key(candidate: dict[str, Any]) -> tuple[int, int, int, int, int]:
    return (
        1 if candidate["move"]["decisive_swing"] == "mate_swing" else 0,
        only_move_priority(candidate["move"].get("only_move_status", "unknown")),
        candidate["move"].get("eval_delta_cp") or -1,
        candidate["move"].get("stronger_alternative_count", 0),
        candidate["move"]["ply"],
    )


def roles_are_adjacent_conflict(left: dict[str, Any], right: dict[str, Any], primary_break_ply: int | None) -> bool:
    left_ply = left["move"]["ply"]
    right_ply = right["move"]["ply"]
    if abs(left_ply - right_ply) > ADJACENT_CONFLICT_PLIES:
        return False

    pair = {left["role"], right["role"]}
    if "preventative_resource" in pair:
        other = right if left["role"] == "preventative_resource" else left
        return left_ply >= (primary_break_ply or other["move"]["ply"]) - 1 if left["role"] == "preventative_resource" else (
            right_ply >= (primary_break_ply or left["move"]["ply"]) - 1
        )

    return pair in (
        {"decisive_blunder", "last_holding_defense"},
        {"decisive_blunder", "missed_counterplay"},
        {"last_holding_defense", "missed_counterplay"},
    )


def choose_conflict_winner(left: dict[str, Any], right: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any], str]:
    if role_priority(left["role"]) != role_priority(right["role"]):
        winner = left if role_priority(left["role"]) > role_priority(right["role"]) else right
        loser = right if winner is left else left
        reason = "lower_priority_same_window"
        return winner, loser, reason

    if candidate_support_key(left) != candidate_support_key(right):
        winner = left if candidate_support_key(left) > candidate_support_key(right) else right
        loser = right if winner is left else left
        reason = "weaker_support_same_role" if left["role"] == right["role"] else "adjacent_duplicate"
        return winner, loser, reason

    winner = left if left["move"]["ply"] >= right["move"]["ply"] else right
    loser = right if winner is left else left
    reason = "adjacent_duplicate"
    return winner, loser, reason


def retained_role_summary(retained: list[dict[str, Any]]) -> str:
    return ", ".join(f"{candidate['role']}@{candidate['move']['ply']}" for candidate in retained)


def collapse_sequence_type(
    primary_break_ply: int | None,
    retained: list[dict[str, Any]],
) -> str:
    if len(retained) <= 1:
        return "single_break"

    if primary_break_ply is None:
        return "multi_step_local_sequence"

    if any(candidate["role"] == "preventative_resource" and candidate["move"]["ply"] < primary_break_ply for candidate in retained):
        return "preventative_then_break"
    if any(candidate["role"] == "last_holding_defense" and candidate["move"]["ply"] < primary_break_ply for candidate in retained):
        return "defense_then_break"
    if any(
        candidate["role"] == "missed_counterplay"
        and candidate["move"]["ply"] <= primary_break_ply
        and candidate["move"]["ply"] != primary_break_ply
        for candidate in retained
    ):
        return "counterplay_then_break"
    return "multi_step_local_sequence"


def collapse_sequence_fields(
    primary_break_ply: int | None,
    retained: list[dict[str, Any]],
    omitted_count: int,
) -> dict[str, Any]:
    role_list = retained_role_summary(retained)
    break_ply = primary_break_ply or (retained[0]["move"]["ply"] if retained else 0)
    return {
        "format_version": "csv1",
        "type": collapse_sequence_type(primary_break_ply, retained),
        "role_list": role_list,
        "break_ply": break_ply,
        "omitted_count": omitted_count,
    }


def format_collapse_sequence_summary(fields: dict[str, Any]) -> str:
    return (
        f"{fields['type']} | break@{fields['break_ply']} | "
        f"roles {fields['role_list'] or 'none'} | omitted {fields['omitted_count']}"
    )


def normalize_omission_reason(
    winner: dict[str, Any],
    loser: dict[str, Any],
    reason: str,
    primary_break_ply: int | None,
) -> str:
    if (
        loser["role"] == "preventative_resource"
        and primary_break_ply is not None
        and loser["move"]["ply"] >= primary_break_ply - 1
    ):
        return "preventative_too_close_to_break"
    if (
        reason == "lower_priority_same_window"
        and winner["role"] == "decisive_blunder"
        and loser["move"]["ply"] > winner["move"]["ply"]
    ):
        return "redundant_consequence_ply"
    return reason


def omission_reason_counts(omitted: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for candidate in omitted:
        reason = candidate["reason"]
        counts[reason] = counts.get(reason, 0) + 1
    return dict(sorted(counts.items()))


def format_omission_reason_summary(counts: dict[str, int]) -> str:
    if not counts:
        return "none"
    return " | ".join(f"{reason}={count}" for reason, count in counts.items())


def primary_break_candidate(retained: list[dict[str, Any]], primary_break_ply: int | None) -> dict[str, Any] | None:
    if primary_break_ply is not None:
        for candidate in retained:
            if candidate["move"]["ply"] == primary_break_ply:
                return candidate
    for role in ("decisive_blunder", "last_holding_defense", "missed_counterplay", "preventative_resource"):
        for candidate in retained:
            if candidate["role"] == role:
                return candidate
    return retained[0] if retained else None


def primary_break_reason(candidate: dict[str, Any]) -> str:
    role = candidate["role"]
    move = candidate["move"]
    if role == "decisive_blunder":
        if move["decisive_swing"] == "mate_swing":
            return "collapse_trigger"
        return "collapse_trigger"
    if role == "last_holding_defense":
        return "narrow_missed_defense"
    if role == "preventative_resource":
        return "failed_preventative_hold"
    if role == "missed_counterplay":
        return "missed_active_resource"
    return "local_break"


def divergence_summary(candidate: dict[str, Any]) -> dict[str, str]:
    move = candidate["move"]
    if "mate_against_mover" in move["mate_flag"]:
        return {
            "type": "mate_created",
            "severity": "critical",
            "compact_summary": "played line allows mate",
            "evidence_notes": "played continuation reaches a mating line while the best local continuation avoids it",
            "summary": "played line allows mate while the best local continuation avoids it",
        }
    if "mate_for_mover" in move["mate_flag"]:
        return {
            "type": "mate_avoided",
            "severity": "critical",
            "compact_summary": "best line preserves mate",
            "evidence_notes": "best local continuation preserves or accelerates a mating line beyond the played continuation",
            "summary": "best local continuation preserves or accelerates a mating line over the played continuation",
        }
    if move["only_move_status"] == "forced_defense_resource":
        return {
            "type": "forced_defense_missed",
            "severity": "critical",
            "compact_summary": "forced defense missed",
            "evidence_notes": "the best local continuation was the only defensive resource that avoided immediate tactical loss",
            "summary": "the best local continuation was the only defensive hold and the played line missed it",
        }
    if move["only_move_status"] == "only_move":
        return {
            "type": "only_move_missed",
            "severity": "major",
            "compact_summary": "only move missed",
            "evidence_notes": "the best local continuation was uniquely stronger than the next analysed local candidates",
            "summary": "the best local continuation was effectively the only local hold and the played line missed it",
        }
    if move["only_move_status"] == "near_only_move":
        return {
            "type": "defensible_position_lost",
            "severity": "major",
            "compact_summary": "narrow defense lost",
            "evidence_notes": "the best local continuation preserved a narrow defensible band while the played continuation fell away from it",
            "summary": "the best local continuation was a narrow local hold while the played line fell away from it",
        }
    if move.get("stronger_alternative_count", 0) > 1:
        return {
            "type": "stronger_alternatives_available",
            "severity": "major" if (move["eval_delta_cp"] or 0) >= SIGNIFICANT_SWING_CP else "moderate",
            "compact_summary": "multiple stronger alternatives existed",
            "evidence_notes": f"{move.get('stronger_alternative_count', 0)} stronger analysed local alternatives outperformed the played continuation",
            "summary": (
                f"the played line underperformed multiple stronger local continuations by about {move['eval_delta_cp']} cp"
                if move["eval_delta_cp"] is not None
                else "the played line underperformed multiple stronger local continuations"
            ),
        }
    if move["eval_delta_cp"] is not None and move["eval_delta_cp"] >= DECISIVE_SWING_CP:
        return {
            "type": "centipawn_collapse",
            "severity": "major",
            "compact_summary": f"{move['eval_delta_cp']} cp collapse",
            "evidence_notes": "the best local continuation preserved a materially stronger evaluation than the played continuation",
            "summary": f"the best local continuation held roughly {move['eval_delta_cp']} cp more than the played line",
        }
    return {
        "type": "tactical_resource_lost",
        "severity": "moderate" if (move["eval_delta_cp"] or 0) >= SIGNIFICANT_SWING_CP else "minor",
        "compact_summary": (
            f"{move['eval_delta_cp']} cp lost"
            if move["eval_delta_cp"] is not None
            else "local tactical resource lost"
        ),
        "evidence_notes": "the best and played local continuations diverged materially inside the retained break sequence",
        "summary": (
            f"the best local continuation held roughly {move['eval_delta_cp']} cp more than the played line"
            if move["eval_delta_cp"] is not None
            else "the best and played local continuations diverged materially inside the retained break sequence"
        ),
    }


def retained_break_compact_sequence(divergence: dict[str, str]) -> str:
    sequence_map = {
        "mate_created": "mate appears",
        "mate_avoided": "mate preserved",
        "forced_defense_missed": "forced defense missed",
        "only_move_missed": "only move missed",
        "stronger_alternatives_available": "stronger alternatives available",
        "centipawn_collapse": divergence["compact_summary"],
        "tactical_resource_lost": "tactical resource lost",
        "defensible_position_lost": "defensible line lost",
    }
    return sequence_map.get(divergence["type"], divergence["compact_summary"] or "local divergence")


def retained_break_fields(candidate: dict[str, Any]) -> dict[str, str]:
    divergence = divergence_summary(candidate)
    move = candidate["move"]
    return {
        "format_version": "rbsv1",
        "role": candidate["role"],
        "played_move": move["played_move"],
        "best_move": move["best_move"],
        "divergence_type": divergence["type"],
        "compact_sequence": retained_break_compact_sequence(divergence),
    }


def format_retained_break_summary(fields: dict[str, str], ply: int) -> str:
    return (
        f"{fields['role']}@{ply} | played {fields['played_move']} | "
        f"best {fields['best_move']} | {fields['compact_sequence']}"
    )


def retained_break_summary(candidate: dict[str, Any]) -> str:
    fields = retained_break_fields(candidate)
    return format_retained_break_summary(fields, candidate["move"]["ply"])


def local_sequence_confidence(candidate: dict[str, Any], omitted_count: int) -> str:
    move = candidate["move"]
    if move["decisive_swing"] == "mate_swing" and omitted_count == 0:
        return "high"
    if move["only_move_status"] in {"forced_mate_resource", "forced_defense_resource", "only_move"} and omitted_count == 0:
        return "high"
    if move["eval_delta_cp"] is not None and move["eval_delta_cp"] >= DECISIVE_SWING_CP and omitted_count <= 1:
        return "high"
    if move["eval_delta_cp"] is not None and move["eval_delta_cp"] >= SIGNIFICANT_SWING_CP:
        return "medium"
    return "low"
