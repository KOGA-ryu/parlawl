#!/usr/bin/env python3

from __future__ import annotations

import io
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from uuid import uuid4

import chess
import chess.engine
import chess.pgn

try:
    from workers.analysis_py.critical_move_formatting import (
        candidate_ranking_fields,
        collapse_sequence_fields,
        continuation_compact_fields,
        critical_move_record,
        critical_reason_fields,
        divergence_summary,
        evidence_note_fields,
        format_candidate_ranking_summary,
        format_collapse_sequence_summary,
        format_omission_reason_summary,
        format_retained_break_summary,
        format_why_critical,
        omission_reason_counts,
        primary_break_reason,
        retained_break_compact_sequence,
        retained_break_fields,
        retained_break_summary,
    )
    from workers.analysis_py.critical_move_selection import (
        detect_critical_moves as _detect_critical_moves,
        move_numeric_eval,
        move_snapshot,
    )
    from workers.analysis_py.structural_basics import (
        back_rank_and_luft_state,
        color_from_name,
        decisive_imbalance_target,
        imbalance_records,
        king_escape_count,
        king_exposure_fields,
        king_zone_squares,
        king_zone_target_fields,
        local_target_confidence,
        local_target_summary,
        loose_piece_records,
        overloaded_defender_records,
        overloaded_summary,
        pawn_shield_count,
        piece_token,
        pressure_lane_target_fields,
        pressure_type_from_lines,
        square_pressure_type,
        structural_feature_confidence,
        structural_feature_summary,
        summary_from_piece_records,
        target_square_summary,
        vulnerable_piece_target_fields,
    )
    from workers.analysis_py.structural_extensions import (
        attacker_coordination_fields,
        defender_removal_exposure_fields,
        defensive_network_fragility_fields,
        defensive_escape_fragility_fields,
        escape_geometry_fields,
        flight_control_fields,
        king_color_complex_fields,
        pinned_critical_piece_fields,
        structural_link_fields,
        structural_v4_confidence,
        structural_v4_summary,
        structural_v2_confidence,
        structural_v2_summary,
        structural_v3_confidence,
        structural_v3_summary,
        target_zone_imbalance_fields,
    )
    from workers.analysis_py.structural_packet import extract_structural_evidence
except ModuleNotFoundError:
    from critical_move_formatting import (
        candidate_ranking_fields,
        collapse_sequence_fields,
        continuation_compact_fields,
        critical_move_record,
        critical_reason_fields,
        divergence_summary,
        evidence_note_fields,
        format_candidate_ranking_summary,
        format_collapse_sequence_summary,
        format_omission_reason_summary,
        format_retained_break_summary,
        format_why_critical,
        omission_reason_counts,
        primary_break_reason,
        retained_break_compact_sequence,
        retained_break_fields,
        retained_break_summary,
    )
    from critical_move_selection import (
        detect_critical_moves as _detect_critical_moves,
        move_numeric_eval,
        move_snapshot,
    )
    from structural_basics import (
        back_rank_and_luft_state,
        color_from_name,
        decisive_imbalance_target,
        imbalance_records,
        king_escape_count,
        king_exposure_fields,
        king_zone_squares,
        king_zone_target_fields,
        local_target_confidence,
        local_target_summary,
        loose_piece_records,
        overloaded_defender_records,
        overloaded_summary,
        pawn_shield_count,
        piece_token,
        pressure_lane_target_fields,
        pressure_type_from_lines,
        square_pressure_type,
        structural_feature_confidence,
        structural_feature_summary,
        summary_from_piece_records,
        target_square_summary,
        vulnerable_piece_target_fields,
    )
    from structural_extensions import (
        attacker_coordination_fields,
        defender_removal_exposure_fields,
        defensive_network_fragility_fields,
        defensive_escape_fragility_fields,
        escape_geometry_fields,
        flight_control_fields,
        king_color_complex_fields,
        pinned_critical_piece_fields,
        structural_link_fields,
        structural_v4_confidence,
        structural_v4_summary,
        structural_v2_confidence,
        structural_v2_summary,
        structural_v3_confidence,
        structural_v3_summary,
        target_zone_imbalance_fields,
    )
    from structural_packet import extract_structural_evidence

DECISIVE_SWING_CP = 300
SIGNIFICANT_SWING_CP = 120
ONLY_MOVE_GAP_CP = 180
NEAR_ONLY_MOVE_GAP_CP = 90
THIRD_CHOICE_ONLY_MOVE_GAP_CP = 180
THIRD_CHOICE_NEAR_ONLY_MOVE_GAP_CP = 120
LAST_HOLDING_DELTA_CP = 140
PREVENTATIVE_DELTA_CP = 100
MISSED_COUNTERPLAY_DELTA_CP = 120
DEFENSIBLE_FLOOR_CP = -120
COUNTERPLAY_FLOOR_CP = 40
MATERIALLY_STRONGER_ALTERNATIVE_CP = 60
MATE_ADVANTAGE_PLIES = 2
FORCED_MATE_PLIES = 3
FORCED_DEFENSE_PLIES = 3
ADJACENT_CONFLICT_PLIES = 1
PV_MOVE_COUNT = 4


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def parse_headers(pgn_text: str) -> dict[str, str]:
    headers: dict[str, str] = {}
    for line in pgn_text.splitlines():
        line = line.strip()
        if not line.startswith("["):
            break
        match = re.match(r'^\[(\w+)\s+"(.*)"\]$', line)
        if match:
            headers[match.group(1)] = match.group(2)
    return headers


def derive_opening_family(opening_name: str) -> str:
    opening_name = opening_name.strip()
    if not opening_name:
        return "unknown"
    for separator in (":", ","):
        if separator in opening_name:
            return opening_name.split(separator, 1)[0].strip().lower().replace(" ", "_")
    return opening_name.lower().replace(" ", "_")


def derive_game_phase(initial_ply: int | None, move_count: int) -> str:
    ply = initial_ply if initial_ply is not None else move_count
    if ply <= 20:
        return "opening"
    if ply <= 60:
        return "middlegame"
    return "endgame"


def derive_material_balance(fen: str, side_to_move: str) -> str:
    board = fen.split(" ", 1)[0]
    values = {"p": 1, "n": 3, "b": 3, "r": 5, "q": 9}
    white = 0
    black = 0
    for char in board:
        lower = char.lower()
        if lower not in values:
            continue
        if char.isupper():
            white += values[lower]
        else:
            black += values[lower]
    diff = black - white if side_to_move == "black" else white - black
    if diff > 0:
        return f"{side_to_move}_up_{diff}"
    if diff < 0:
        return f"{side_to_move}_down_{abs(diff)}"
    return "equal"


def legacy_king_safety_fallback(headers: dict[str, str], opening_name: str) -> str:
    opening = opening_name.lower() or headers.get("Opening", "").lower()
    if "sicilian" in opening or "king's gambit" in opening:
        return "dynamic_king_exposure_risk"
    if "attack" in opening:
        return "attacking_king_pressure_candidate"
    return "stable_king"


def legacy_piece_activity_fallback(move_count: int) -> str:
    if move_count >= 10:
        return "constrained_piece_activity"
    return "no_clear_piece_activity"


def legacy_key_weakness_fallback(opening_name: str, best_move: str) -> str:
    best_move = (best_move or "").lower()
    if len(best_move) >= 4 and best_move[-2:] in {"h7", "h2", "g7", "g2", "f7", "f2"}:
        return "king_exposure"
    if "back_rank" in opening_name.lower():
        return "back_rank_weakness"
    return "no_clear_key_weakness"


def derive_key_weakness(
    fields: dict[str, Any],
    *,
    opening_name: str,
    best_move: str,
) -> str:
    structural_confidence = fields.get("structural_feature_confidence", "low")
    local_confidence = fields.get("local_target_confidence", "low")

    king_exposure_signal = fields.get("king_exposure_type") != "no_clear_exposure"
    back_rank_signal = fields.get("back_rank_state") == "back_rank_vulnerable"
    no_luft_signal = fields.get("luft_state") == "no_luft"
    king_exposure_type = fields.get("king_exposure_type")
    decisive_imbalance_signal = fields.get("decisive_imbalance_target") not in {None, "", "none"}
    vulnerable_piece_signal = fields.get("vulnerable_piece_target_type") != "no_clear_vulnerable_piece_target"
    overloaded_defender_signal = int(fields.get("overloaded_defender_count", 0) or 0) > 0
    pressure_lane_signal = fields.get("pressure_lane_target_type") != "no_clear_pressure_lane_target"

    target_zone_imbalance_signal = (
        fields.get("king_zone_target_type") != "no_clear_king_zone_target"
        and fields.get("king_square_pressure_type") in {
            "concentrated_square_pressure",
            "localized_square_pressure",
        }
    )
    weak_color_complex_signal = bool(fields.get("weak_color_complex_signal"))
    pinned_critical_piece_signal = bool(fields.get("pinned_critical_piece_signal"))
    defender_removal_exposure_signal = bool(fields.get("defender_removal_exposure_signal"))

    if back_rank_signal:
        return "back_rank_weakness"
    if no_luft_signal and pressure_lane_signal:
        return "no_luft"
    if king_exposure_type in {"exposed_open_file", "exposed_open_diagonal", "exposed_central_king"}:
        return "king_exposure"
    if weak_color_complex_signal or target_zone_imbalance_signal:
        return "king_exposure"

    secondary_signals: list[str] = []
    if decisive_imbalance_signal:
        secondary_signals.append("decisive_imbalance")
    if vulnerable_piece_signal:
        secondary_signals.append("vulnerable_piece")
    if overloaded_defender_signal or pinned_critical_piece_signal or defender_removal_exposure_signal:
        secondary_signals.append("overloaded_defender")
    if pressure_lane_signal:
        secondary_signals.append("pressure_lane")

    unique_secondary = unique_preserve_order(secondary_signals)
    if (
        king_exposure_type == "exposed_piece_shield_loss"
        and (
            len(unique_secondary) >= 3
            or (
                len(unique_secondary) >= 2
                and (pressure_lane_signal or overloaded_defender_signal)
            )
        )
        and (local_confidence == "high" or structural_confidence == "high")
    ):
        return "mixed_local_weakness"
    if unique_secondary and king_exposure_signal:
        return "king_exposure"
    if len(unique_secondary) >= 2 and (local_confidence == "high" or structural_confidence == "high"):
        return "mixed_local_weakness"
    if unique_secondary:
        return unique_secondary[0]
    if king_exposure_signal:
        return "king_exposure"

    if local_confidence == "low" and structural_confidence == "low":
        return legacy_key_weakness_fallback(opening_name, best_move)

    return "no_clear_key_weakness"


def parse_json_list(raw: str) -> list[str]:
    if not raw:
        return []
    try:
        value = json.loads(raw)
        if isinstance(value, list):
            return [str(item) for item in value]
    except json.JSONDecodeError:
        pass
    return []


def unique_preserve_order(values: list[str]) -> list[str]:
    seen: set[str] = set()
    ordered: list[str] = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        ordered.append(value)
    return ordered


def mate_flag_for_distance(mate_distance: int | None) -> str:
    if mate_distance is None:
        return "none"
    if mate_distance >= 0:
        return f"mate_for_mover_in_{mate_distance}"
    return f"mate_against_mover_in_{abs(mate_distance)}"


# All score comparisons in this worker use the mover perspective on the pre-move
# board being analysed. Positive is better for the mover. Mate scores outrank
# any centipawn score, and faster mate-for-mover outranks slower mate-for-mover.
def score_snapshot(score: chess.engine.PovScore | None, pov: chess.Color) -> dict[str, Any]:
    if score is None:
        return {
            "score_kind": "unknown",
            "cp": None,
            "mate_distance": None,
            "mate_flag": "unknown",
            "numeric": None,
        }

    relative = score.pov(pov)
    if relative.is_mate():
        mate_distance = int(relative.mate())
        if mate_distance >= 0:
            numeric = 1_000_000 - min(abs(mate_distance), 1000) * 1000
        else:
            numeric = -1_000_000 + min(abs(mate_distance), 1000) * 1000
        return {
            "score_kind": "mate",
            "cp": None,
            "mate_distance": mate_distance,
            "mate_flag": mate_flag_for_distance(mate_distance),
            "numeric": numeric,
        }

    cp_value = relative.score()
    return {
        "score_kind": "cp",
        "cp": cp_value,
        "mate_distance": None,
        "mate_flag": "none",
        "numeric": cp_value,
    }


def snapshot_from_fields(cp_value: int | None, mate_flag: str) -> dict[str, Any]:
    if mate_flag.startswith("mate_for_mover_in_"):
        mate_distance = int(mate_flag.removeprefix("mate_for_mover_in_"))
        return {
            "score_kind": "mate",
            "cp": None,
            "mate_distance": mate_distance,
            "mate_flag": mate_flag,
            "numeric": 1_000_000 - min(abs(mate_distance), 1000) * 1000,
        }
    if mate_flag.startswith("mate_against_mover_in_"):
        mate_distance = -int(mate_flag.removeprefix("mate_against_mover_in_"))
        return {
            "score_kind": "mate",
            "cp": None,
            "mate_distance": mate_distance,
            "mate_flag": mate_flag,
            "numeric": -1_000_000 + min(abs(mate_distance), 1000) * 1000,
        }
    if mate_flag == "none":
        return {
            "score_kind": "cp",
            "cp": cp_value,
            "mate_distance": None,
            "mate_flag": "none",
            "numeric": cp_value,
        }
    return {
        "score_kind": "unknown",
        "cp": cp_value,
        "mate_distance": None,
        "mate_flag": mate_flag or "unknown",
        "numeric": cp_value,
    }


def score_is_mate_for_mover(snapshot: dict[str, Any]) -> bool:
    return snapshot.get("score_kind") == "mate" and int(snapshot.get("mate_distance") or 0) >= 0


def score_is_mate_against_mover(snapshot: dict[str, Any]) -> bool:
    return snapshot.get("score_kind") == "mate" and int(snapshot.get("mate_distance") or 0) < 0


def score_gap(snapshot_a: dict[str, Any], snapshot_b: dict[str, Any]) -> int | None:
    if snapshot_a.get("numeric") is None or snapshot_b.get("numeric") is None:
        return None
    return int(snapshot_a["numeric"]) - int(snapshot_b["numeric"])


def cp_gap(snapshot_a: dict[str, Any], snapshot_b: dict[str, Any]) -> int | None:
    if snapshot_a.get("score_kind") != "cp" or snapshot_b.get("score_kind") != "cp":
        return None
    if snapshot_a.get("cp") is None or snapshot_b.get("cp") is None:
        return None
    return int(snapshot_a["cp"]) - int(snapshot_b["cp"])


def candidate_is_materially_stronger(candidate: dict[str, Any], played: dict[str, Any]) -> bool:
    candidate_snapshot = snapshot_from_fields(candidate.get("eval_cp"), candidate.get("mate_flag", "unknown"))
    played_snapshot = snapshot_from_fields(played.get("eval_cp"), played.get("mate_flag", "unknown"))
    gap = score_gap(candidate_snapshot, played_snapshot)
    if gap is None or gap <= 0:
        return False

    if candidate_snapshot["score_kind"] == "cp" and played_snapshot["score_kind"] == "cp":
        return gap >= MATERIALLY_STRONGER_ALTERNATIVE_CP

    if score_is_mate_for_mover(candidate_snapshot):
        if not score_is_mate_for_mover(played_snapshot):
            return True
        return abs(int(played_snapshot["mate_distance"])) - abs(int(candidate_snapshot["mate_distance"])) >= MATE_ADVANTAGE_PLIES

    if score_is_mate_against_mover(played_snapshot):
        if not score_is_mate_against_mover(candidate_snapshot):
            return True
        return abs(int(candidate_snapshot["mate_distance"])) - abs(int(played_snapshot["mate_distance"])) >= MATE_ADVANTAGE_PLIES

    return gap >= MATERIALLY_STRONGER_ALTERNATIVE_CP


def move_level_mate_flag(best_snapshot: dict[str, Any], played_snapshot: dict[str, Any]) -> str:
    if score_is_mate_for_mover(best_snapshot) and not score_is_mate_for_mover(played_snapshot):
        return best_snapshot["mate_flag"]
    if score_is_mate_against_mover(played_snapshot) and not score_is_mate_against_mover(best_snapshot):
        return played_snapshot["mate_flag"]
    if score_is_mate_for_mover(best_snapshot) and score_is_mate_for_mover(played_snapshot):
        return f"best_{best_snapshot['mate_flag']}_vs_played_{played_snapshot['mate_flag']}"
    if score_is_mate_against_mover(best_snapshot) and score_is_mate_against_mover(played_snapshot):
        return f"best_{best_snapshot['mate_flag']}_vs_played_{played_snapshot['mate_flag']}"
    if best_snapshot["score_kind"] == "mate" or played_snapshot["score_kind"] == "mate":
        return "mixed_mate_scores"
    return "none"


def decisive_swing_label(best_snapshot: dict[str, Any], played_snapshot: dict[str, Any]) -> str:
    if best_snapshot["score_kind"] == "mate" or played_snapshot["score_kind"] == "mate":
        return "mate_swing"
    delta = cp_gap(best_snapshot, played_snapshot)
    if delta is None:
        return "unknown"
    if delta >= DECISIVE_SWING_CP:
        return "decisive"
    if delta >= SIGNIFICANT_SWING_CP:
        return "significant"
    return "limited"


def tactical_candidates(themes: list[str], best_cp_delta: int | None) -> list[str]:
    lowered = [theme.lower() for theme in themes if theme.lower() not in {"middlegame", "opening", "endgame"}]
    if best_cp_delta is not None and best_cp_delta >= DECISIVE_SWING_CP:
        lowered.append("engine_detected_decisive_swing")
    return unique_preserve_order(lowered) or ["unknown"]


def derive_king_safety_state(
    fields: dict[str, Any],
    *,
    opening_name: str,
    headers: dict[str, str],
) -> str:
    structural_confidence = fields.get("structural_feature_confidence", "low")
    local_confidence = fields.get("local_target_confidence", "low")

    if fields.get("back_rank_state") == "back_rank_vulnerable":
        return "back_rank_danger"
    if fields.get("luft_state") == "no_luft" and (
        fields.get("king_line_pressure_type") != "no_clear_line_pressure"
        or fields.get("king_zone_target_type") != "no_clear_king_zone_target"
    ):
        return "no_luft_pressure"
    king_exposure_type = fields.get("king_exposure_type")
    if king_exposure_type in {"exposed_open_file", "exposed_open_diagonal", "exposed_central_king"}:
        return "king_exposure"
    if (
        fields.get("king_line_pressure_type") != "no_clear_line_pressure"
        or fields.get("king_square_pressure_type") in {"concentrated_square_pressure", "localized_square_pressure"}
        or fields.get("king_zone_target_type") != "no_clear_king_zone_target"
    ) and (local_confidence != "low" or structural_confidence != "low"):
        if (
            king_exposure_type == "exposed_piece_shield_loss"
            and fields.get("king_zone_target_type") == "no_clear_king_zone_target"
        ):
            return "pressured_king_zone"
    if king_exposure_type != "no_clear_exposure":
        return "king_exposure"
    if (
        fields.get("king_line_pressure_type") != "no_clear_line_pressure"
        or fields.get("king_square_pressure_type") in {"concentrated_square_pressure", "localized_square_pressure"}
        or fields.get("king_zone_target_type") != "no_clear_king_zone_target"
    ) and (local_confidence != "low" or structural_confidence != "low"):
        return "pressured_king_zone"
    if local_confidence == "low" and structural_confidence == "low":
        return legacy_king_safety_fallback(headers, opening_name)
    return "stable_king"


def derive_piece_activity(
    fields: dict[str, Any],
    *,
    critical_reason_type: str,
    candidate_ranking_type: str,
    move_count: int,
) -> str:
    structural_confidence = fields.get("structural_feature_confidence", "low")
    local_confidence = fields.get("local_target_confidence", "low")

    king_zone_signal = fields.get("king_zone_target_type") != "no_clear_king_zone_target"
    pressure_lane_signal = fields.get("pressure_lane_target_type") != "no_clear_pressure_lane_target"
    tactical_piece_signal = (
        fields.get("vulnerable_piece_target_type") != "no_clear_vulnerable_piece_target"
        or fields.get("decisive_imbalance_target") not in {None, "", "none"}
    )
    overloaded_defender_signal = int(fields.get("overloaded_defender_count", 0) or 0) > 0

    if (
        king_zone_signal
        and (
            fields.get("king_line_pressure_type") != "no_clear_line_pressure"
            or fields.get("king_square_pressure_type") in {"concentrated_square_pressure", "localized_square_pressure"}
            or critical_reason_type in {"mating_line_allowed", "mating_line_preserved", "eval_collapse_trigger"}
            or candidate_ranking_type in {"mate_driven_choice", "forced_defense_choice", "played_collapse"}
        )
        and not tactical_piece_signal
        and not overloaded_defender_signal
    ):
        return "active_king_zone_pressure"

    signals: list[str] = []
    if king_zone_signal and (
        fields.get("king_line_pressure_type") != "no_clear_line_pressure"
        or fields.get("king_square_pressure_type") in {"concentrated_square_pressure", "localized_square_pressure"}
        or critical_reason_type in {"mating_line_allowed", "mating_line_preserved", "eval_collapse_trigger"}
        or candidate_ranking_type in {"mate_driven_choice", "forced_defense_choice", "played_collapse"}
    ):
        signals.append("active_king_zone_pressure")
    if pressure_lane_signal:
        signals.append("active_pressure_lane")
    if tactical_piece_signal:
        signals.append("active_tactical_piece")
    if overloaded_defender_signal and (
        critical_reason_type in {"forced_defense_resource", "only_move_resource", "preventative_hold_available"}
        or candidate_ranking_type == "forced_defense_choice"
    ):
        signals.append("overloaded_defensive_piece")

    unique_signals = unique_preserve_order(signals)
    if len(unique_signals) >= 2 and (local_confidence == "high" or structural_confidence == "high"):
        return "mixed_local_activity"
    if unique_signals:
        return unique_signals[0]
    if local_confidence == "low" and structural_confidence == "low":
        return legacy_piece_activity_fallback(move_count)
    return "no_clear_piece_activity"


def legacy_structural_candidates_fallback(opening_name: str, key_weakness: str) -> list[str]:
    candidates: list[str] = []
    opening = opening_name.lower()
    if "sicilian" in opening:
        candidates.append("asymmetric_pawn_structure_candidate")
    if "italian" in opening:
        candidates.append("open_center_development_race_candidate")
    if key_weakness not in {"unknown", "no_clear_key_weakness"}:
        candidates.append(key_weakness)
    return unique_preserve_order(candidates) or ["unknown"]


def board_before_ply(game: chess.pgn.Game, moves: list[chess.Move], ply: int) -> chess.Board:
    board = game.board()
    stop = max(0, min(ply - 1, len(moves)))
    for index in range(stop):
        board.push(moves[index])
    return board

def structural_candidates_from_fields(
    fields: dict[str, Any],
    key_weakness: str,
    *,
    opening_name: str,
) -> list[str]:
    structural_confidence = fields.get("structural_feature_confidence", "low")
    local_confidence = fields.get("local_target_confidence", "low")
    candidates: list[str] = []

    if fields["king_exposure_type"] != "no_clear_exposure":
        candidates.append("king_exposure")
    if fields["back_rank_state"] == "back_rank_vulnerable":
        candidates.append("back_rank_weakness")
    elif fields["luft_state"] == "no_luft":
        candidates.append("no_luft")
    if fields["pressure_lane_target_type"] != "no_clear_pressure_lane_target":
        candidates.append("pressure_lane")
    if fields["vulnerable_piece_target_type"] != "no_clear_vulnerable_piece_target":
        candidates.append("vulnerable_piece")
    if fields["overloaded_defender_count"] > 0:
        candidates.append("overloaded_defender")
    if fields["decisive_imbalance_target"] != "none":
        candidates.append("decisive_imbalance")
    if fields["king_zone_target_type"] != "no_clear_king_zone_target":
        candidates.append("king_zone_target")
    if key_weakness not in {"unknown", "no_clear_key_weakness"}:
        candidates.append(key_weakness)

    candidates = unique_preserve_order(candidates)
    if candidates:
        return candidates
    if structural_confidence == "low" and local_confidence == "low":
        return legacy_structural_candidates_fallback(opening_name, key_weakness)
    return ["unknown"]


def load_game(pgn_text: str) -> tuple[chess.pgn.Game, list[chess.Move]]:
    game = chess.pgn.read_game(io.StringIO(pgn_text))
    if game is None:
        raise ValueError("pgn parse failure: no game found in source PGN")
    return game, list(game.mainline_moves())


def san_variation(board: chess.Board, pv: list[chess.Move]) -> str:
    if not pv:
        return ""
    try:
        return board.variation_san(pv)
    except ValueError:
        return ""


def side_name(turn: chess.Color) -> str:
    return "white" if turn == chess.WHITE else "black"


def fen_identity(fen: str) -> str:
    parts = fen.split(" ")
    return " ".join(parts[:4]) if len(parts) >= 4 else fen


def locate_puzzle_start(
    moves: list[chess.Move],
    initial_fen: str,
    initial_ply: int | None,
) -> dict[str, Any]:
    matches: list[int] = []
    target_identity = fen_identity(initial_fen)

    board = chess.Board()
    if fen_identity(board.fen()) == target_identity:
        matches.append(0)
    for ply, move in enumerate(moves, start=1):
        board.push(move)
        if fen_identity(board.fen()) == target_identity:
            matches.append(ply)

    if initial_ply is not None and 0 <= initial_ply <= len(moves):
        if initial_ply in matches:
            return {
                "mapping_status": "mapped",
                "mapped_start_ply": initial_ply,
                "mapping_method": "exact_initial_ply_match",
                "mapping_confidence": "high",
                "mapping_notes": "initialPly and initial_fen identity agreed during PGN replay",
                "candidate_plies": matches,
            }

    if len(matches) == 1:
        note = "unique initial_fen identity match found by PGN search"
        if initial_ply is not None and initial_ply != matches[0]:
            note = f"initialPly {initial_ply} did not match; unique initial_fen identity match found at ply {matches[0]}"
        return {
            "mapping_status": "mapped",
            "mapped_start_ply": matches[0],
            "mapping_method": "exact_fen_search_match",
            "mapping_confidence": "medium",
            "mapping_notes": note,
            "candidate_plies": matches,
        }

    if len(matches) > 1:
        raise ValueError(
            f"ambiguous mapping: initial_fen identity matched multiple PGN plies {matches}; worker will not choose a weak mapping"
        )

    raise ValueError("mapping failure: could not align puzzle initial_fen identity with the source PGN")


def analyse_board(
    engine: chess.engine.SimpleEngine,
    board: chess.Board,
    depth: int,
    pov: chess.Color,
    multipv: int,
) -> list[dict[str, Any]]:
    info = engine.analyse(board, chess.engine.Limit(depth=depth), multipv=multipv)
    infos = info if isinstance(info, list) else [info]

    analysed: list[dict[str, Any]] = []
    for entry in infos:
        pv = list(entry.get("pv", []))[:PV_MOVE_COUNT]
        best_move = pv[0].uci() if pv else "unknown"
        snapshot = score_snapshot(entry.get("score"), pov)
        analysed.append(
            {
                "best_move": best_move,
                "cp": snapshot["cp"],
                "mate_flag": snapshot["mate_flag"],
                "mate_distance": snapshot["mate_distance"],
                "score_kind": snapshot["score_kind"],
                "numeric": snapshot["numeric"],
                "depth": int(entry.get("depth", depth)),
                "pv_moves": pv,
                "pv_uci": " ".join(move.uci() for move in pv),
                "pv_san": san_variation(board, pv),
            }
        )
    return analysed


def only_move_diagnostics(candidates: list[dict[str, Any]]) -> tuple[str, int | None, str]:
    if len(candidates) < 2:
        return "unknown", None, "not enough analysed candidates for only-move diagnostics"

    best = snapshot_from_fields(candidates[0]["cp"], candidates[0]["mate_flag"])
    second = snapshot_from_fields(candidates[1]["cp"], candidates[1]["mate_flag"])
    third = snapshot_from_fields(candidates[2]["cp"], candidates[2]["mate_flag"]) if len(candidates) >= 3 else None

    if score_is_mate_for_mover(best):
        second_gap = score_gap(best, second)
        if not score_is_mate_for_mover(second):
            return "forced_mate_resource", None, "best move is the only local candidate that preserves a forced mate for the mover"
        if abs(int(second["mate_distance"])) - abs(int(best["mate_distance"])) >= FORCED_MATE_PLIES:
            return "forced_mate_resource", None, "best move forces mate materially faster than the next local candidate"
        if second_gap is not None and second_gap > 0:
            return "near_only_move", None, "multiple mating candidates exist, but the best move wins materially faster"
        return "multiple_viable", None, "multiple mating candidates remain comparably strong in the local window"

    if score_is_mate_against_mover(second):
        if not score_is_mate_against_mover(best):
            return "forced_defense_resource", None, "best move is the only local candidate that avoids a forced mate against the mover"
        if abs(int(best["mate_distance"])) - abs(int(second["mate_distance"])) >= FORCED_DEFENSE_PLIES:
            return "forced_defense_resource", None, "best move prolongs survival materially longer than the next local candidate in a forced-mate position"

    comparison_gaps: list[int] = []
    second_gap = score_gap(best, second)
    if second_gap is not None:
        comparison_gaps.append(second_gap)
    third_gap = score_gap(best, third) if third is not None else None
    if third_gap is not None:
        comparison_gaps.append(third_gap)
    if not comparison_gaps:
        return "unknown", None, "candidate scores were not comparable"

    limiting_cp_gap = cp_gap(best, second)
    if third is not None:
        third_cp_gap = cp_gap(best, third)
        if third_cp_gap is not None:
            limiting_cp_gap = third_cp_gap if limiting_cp_gap is None else min(limiting_cp_gap, third_cp_gap)

    if second_gap >= ONLY_MOVE_GAP_CP and (third_gap is None or third_gap >= THIRD_CHOICE_ONLY_MOVE_GAP_CP):
        return "only_move", limiting_cp_gap, "best move clearly outperforms the next local alternatives under the same mover-perspective score convention"
    if second_gap >= NEAR_ONLY_MOVE_GAP_CP and (third_gap is None or third_gap >= THIRD_CHOICE_NEAR_ONLY_MOVE_GAP_CP):
        return "near_only_move", limiting_cp_gap, "best move is materially stronger than the next local alternatives but not uniquely forced"
    return "multiple_viable", limiting_cp_gap, "multiple local candidates remain within the viable evaluation band"


def continuation_summary(label: str, move_uci: str, pv_san: str) -> str:
    if not move_uci or move_uci == "unknown":
        return ""
    if pv_san:
        return f"{label} {move_uci}: {pv_san}"
    return f"{label} {move_uci}"


def san_for_move(board: chess.Board, move: chess.Move) -> str:
    try:
        return board.san(move)
    except ValueError:
        return ""


def candidate_score_display(candidate: dict[str, Any]) -> str:
    if candidate["score_kind"] == "mate" and candidate["mate_distance"] is not None:
        return f"mate {int(candidate['mate_distance']):+d}"
    if candidate["eval_cp"] is not None:
        return f"cp {int(candidate['eval_cp']):+d}"
    return "n/a"


def candidate_display_fields(candidate: dict[str, Any]) -> dict[str, str]:
    tags: list[str] = []
    if candidate["is_best_move"]:
        tags.append("best")
    if candidate["is_played_move"]:
        tags.append("played")
    tag_fragment = f" | {','.join(tags)}" if tags else ""
    return {
        "format_version": "cdv1",
        "compact": (
            f"{candidate['rank']}. {candidate['move_san'] or candidate['move_uci']} | "
            f"{candidate_score_display(candidate)}{tag_fragment}"
        ),
    }


def candidate_display_summary(candidates: list[dict[str, Any]]) -> str:
    return " || ".join(candidate["candidate_display_compact"] for candidate in candidates)


def build_ranked_candidates(
    board: chess.Board,
    played_move: chess.Move,
    ranked: list[dict[str, Any]],
    depth: int,
    engine: chess.engine.SimpleEngine,
    pov: chess.Color,
) -> tuple[list[dict[str, Any]], dict[str, Any], int]:
    candidates: list[dict[str, Any]] = []
    played_candidate: dict[str, Any] | None = None

    for index, candidate in enumerate(ranked[:3], start=1):
        move_uci = candidate.get("best_move", "unknown")
        if move_uci == "unknown":
            continue
        move = chess.Move.from_uci(move_uci)
        after_board = board.copy(stack=False)
        after_board.push(move)
        after_analysis = analyse_board(engine, after_board, depth, pov, 1)[0]
        entry = {
            "move_uci": move_uci,
            "move_san": san_for_move(board, move),
            "eval_cp": after_analysis["cp"],
            "mate_flag": after_analysis["mate_flag"],
            "mate_distance": after_analysis["mate_distance"],
            "score_kind": after_analysis["score_kind"],
            "rank": index,
            "is_played_move": move == played_move,
            "is_best_move": index == 1,
            "pv_uci": candidate.get("pv_uci", ""),
            "pv_san": candidate.get("pv_san", ""),
            "continuation_summary": continuation_summary(
                "candidate",
                move_uci,
                candidate.get("pv_san", ""),
            ),
        }
        display = candidate_display_fields(entry)
        entry.update(
            {
                "candidate_display_format_version": display["format_version"],
                "candidate_display_compact": display["compact"],
            }
        )
        candidates.append(entry)
        if entry["is_played_move"]:
            played_candidate = entry

    if played_candidate is None:
        played_board = board.copy(stack=False)
        played_board.push(played_move)
        played_analysis = analyse_board(engine, played_board, depth, pov, 1)[0]
        played_candidate = {
            "move_uci": played_move.uci(),
            "move_san": san_for_move(board, played_move),
            "eval_cp": played_analysis["cp"],
            "mate_flag": played_analysis["mate_flag"],
            "mate_distance": played_analysis["mate_distance"],
            "score_kind": played_analysis["score_kind"],
            "rank": len(candidates) + 1,
            "is_played_move": True,
            "is_best_move": False,
            "pv_uci": played_analysis.get("pv_uci", ""),
            "pv_san": played_analysis.get("pv_san", ""),
            "continuation_summary": continuation_summary(
                "candidate",
                played_move.uci(),
                played_analysis.get("pv_san", ""),
            ),
        }
        display = candidate_display_fields(played_candidate)
        played_candidate.update(
            {
                "candidate_display_format_version": display["format_version"],
                "candidate_display_compact": display["compact"],
            }
        )
        candidates.append(played_candidate)

    stronger_alternative_count = 0
    for candidate in candidates:
        if candidate["is_played_move"]:
            continue
        if candidate_is_materially_stronger(candidate, played_candidate):
            stronger_alternative_count += 1

    ranking_summary = candidate_display_summary(candidates)
    ranked_candidates = [
        {
            "move_uci": candidate["move_uci"],
            "move_san": candidate["move_san"],
            "eval_cp": candidate["eval_cp"],
            "mate_flag": candidate["mate_flag"],
            "mate_distance": candidate["mate_distance"],
            "score_kind": candidate["score_kind"],
            "rank": candidate["rank"],
            "is_played_move": candidate["is_played_move"],
            "is_best_move": candidate["is_best_move"],
            "candidate_display_format_version": candidate["candidate_display_format_version"],
            "candidate_display_compact": candidate["candidate_display_compact"],
        }
        for candidate in candidates
    ]
    return ranked_candidates, ranking_summary, played_candidate, stronger_alternative_count


def detect_critical_moves(
    window_analysis: list[dict[str, Any]],
    collapse_side: str,
    mapped_start_ply: int,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    return _detect_critical_moves(window_analysis, collapse_side, mapped_start_ply, snapshot_from_fields)


def analyse_local_window(
    engine: chess.engine.SimpleEngine,
    game: chess.pgn.Game,
    moves: list[chess.Move],
    analysis_window_start: int,
    analysis_window_end: int,
    depth: int,
    multipv: int,
) -> list[dict[str, Any]]:
    window_analysis: list[dict[str, Any]] = []
    board = game.board()
    for ply, move in enumerate(moves, start=1):
        if analysis_window_start <= ply <= analysis_window_end:
            pre_move_board = board.copy(stack=False)
            mover = pre_move_board.turn
            ranked = analyse_board(engine, pre_move_board, depth, mover, multipv)
            best = ranked[0] if ranked else {}
            only_move_status, only_move_margin, only_move_reasoning = only_move_diagnostics(ranked)

            played_board = pre_move_board.copy(stack=False)
            played_board.push(move)
            played_analysis = analyse_board(engine, played_board, depth, mover, 1)[0]

            ranked_candidates, ranking_summary, played_candidate, stronger_alternative_count = build_ranked_candidates(
                pre_move_board,
                move,
                ranked,
                depth,
                engine,
                mover,
            )

            best_after_board = pre_move_board.copy(stack=False)
            if best.get("best_move") and best["best_move"] != "unknown":
                best_after_board.push(chess.Move.from_uci(best["best_move"]))
                best_after_analysis = analyse_board(engine, best_after_board, depth, mover, 1)[0]
            else:
                best_after_analysis = {
                    "best_move": "unknown",
                    "cp": None,
                    "mate_flag": "unknown",
                    "depth": depth,
                    "pv_uci": "",
                    "pv_san": "",
                }

            best_snapshot = snapshot_from_fields(best_after_analysis["cp"], best_after_analysis["mate_flag"])
            played_snapshot = snapshot_from_fields(played_analysis["cp"], played_analysis["mate_flag"])
            before_snapshot = snapshot_from_fields(best.get("cp"), best.get("mate_flag", "unknown"))
            eval_delta = cp_gap(best_snapshot, played_snapshot)
            mate_flag = move_level_mate_flag(best_snapshot, played_snapshot)

            window_analysis.append(
                {
                    "ply": ply,
                    "side": side_name(mover),
                    "played_move": move.uci(),
                    "best_move": best.get("best_move", "unknown"),
                    "eval_before_cp": best.get("cp"),
                    "eval_after_played_cp": played_analysis["cp"],
                    "eval_after_best_cp": best_after_analysis["cp"],
                    "eval_delta_cp": eval_delta,
                    "decisive_swing": decisive_swing_label(best_snapshot, played_snapshot),
                    "analysis_depth": max(best.get("depth", depth), played_analysis["depth"], best_after_analysis["depth"]),
                    "mate_flag": mate_flag,
                    "before_mate_flag": before_snapshot["mate_flag"],
                    "played_mate_flag": played_snapshot["mate_flag"],
                    "best_mate_flag": best_snapshot["mate_flag"],
                    "pv_uci": best.get("pv_uci", ""),
                    "pv_san": best.get("pv_san", ""),
                    "best_continuation_summary": continuation_summary("best", best.get("best_move", "unknown"), best.get("pv_san", "")),
                    "played_continuation_summary": continuation_summary("played", move.uci(), played_analysis.get("pv_san", "")),
                    "only_move_status": only_move_status,
                    "only_move_margin_cp": only_move_margin,
                    "only_move_reasoning": only_move_reasoning,
                    "candidate_moves_json": json.dumps(ranked_candidates, separators=(",", ":")),
                    "candidate_ranking_summary": ranking_summary,
                    "stronger_alternative_count": stronger_alternative_count,
                    "played_move_rank": played_candidate["rank"],
                }
            )
        board.push(move)
    return window_analysis


def analysis_for_request(request: dict[str, Any]) -> dict[str, Any]:
    analysis_run = request.get("analysis_run", {})
    puzzle_round = request.get("puzzle_round", {})
    source_game = request.get("source_game", {})
    engine_config = request.get("engine", {})

    stockfish_path = engine_config.get("stockfish_path", "")
    if not stockfish_path or not Path(stockfish_path).exists():
        raise ValueError("engine launch failure: configured stockfish path does not exist")

    pgn_text = source_game.get("pgn_text", "")
    game, moves = load_game(pgn_text)
    headers = parse_headers(pgn_text)

    puzzle_detail = {}
    raw_puzzle_json = puzzle_round.get("raw_puzzle_json")
    if raw_puzzle_json:
        puzzle_detail = json.loads(raw_puzzle_json)
    puzzle_object = puzzle_detail.get("puzzle", {})
    initial_ply = puzzle_object.get("initialPly")
    initial_fen = puzzle_round.get("initial_fen", "")
    if not initial_fen:
        raise ValueError("mapping failure: initial_fen missing from puzzle context")

    mapping = locate_puzzle_start(moves, initial_fen, initial_ply)
    mapped_start_ply = int(mapping["mapped_start_ply"])
    if mapped_start_ply <= 0:
        raise ValueError("mapping failure: mapped puzzle start did not leave a prior played move for trigger comparison")

    depth = int(engine_config.get("depth", 10))
    window_before = int(engine_config.get("window_before", 4))
    window_after = int(engine_config.get("window_after", 4))
    multipv = int(engine_config.get("multipv", 3))
    analysis_window_start = max(1, mapped_start_ply - window_before)
    analysis_window_end = min(len(moves), mapped_start_ply + window_after)

    opening_name = source_game.get("opening_name") or headers.get("Opening", "")
    side_to_move = puzzle_round.get("side_to_move", "unknown")
    collapse_side = "black" if side_to_move == "white" else "white"
    solution_moves = parse_json_list(puzzle_round.get("solution_moves_json", ""))

    with chess.engine.SimpleEngine.popen_uci(stockfish_path) as engine:
        window_analysis = analyse_local_window(
            engine,
            game,
            moves,
            analysis_window_start,
            analysis_window_end,
            depth,
            multipv,
        )

    if not window_analysis:
        raise ValueError("evidence extraction failure: tactical window contained no analysable plies")

    trigger_move = next((move for move in window_analysis if move["ply"] == mapped_start_ply), None)
    if trigger_move is None:
        raise ValueError("evidence extraction failure: mapped start ply was not present in the analysed local window")

    opening_family = derive_opening_family(opening_name)
    game_phase = derive_game_phase(initial_ply, len(moves))
    solution_summary = " -> ".join(solution_moves[:3]) if solution_moves else "unknown"
    material_balance = derive_material_balance(initial_fen, side_to_move)
    tactical = tactical_candidates(parse_json_list(puzzle_round.get("themes_json", "")), trigger_move["eval_delta_cp"])

    ratings = [puzzle_round.get("white_rating", 0), puzzle_round.get("black_rating", 0)]
    average = sum(int(r or 0) for r in ratings) // max(len(ratings), 1)
    if average <= 0:
        player_rating_range = "unknown"
    elif average < 1400:
        player_rating_range = "under_1400"
    elif average < 1800:
        player_rating_range = "1400_1799"
    elif average < 2200:
        player_rating_range = "1800_2199"
    else:
        player_rating_range = "2200_plus"

    event_id = str(uuid4())
    critical_moves, event_summary = detect_critical_moves(window_analysis, collapse_side, mapped_start_ply)
    for move in critical_moves:
        move["event_id"] = event_id

    primary_break_board = board_before_ply(game, moves, int(event_summary["primary_break_ply"]))
    structural_fields = extract_structural_evidence(
        primary_break_board,
        collapse_side,
        event_summary["retained_break_played_move"],
        event_summary["retained_break_best_move"],
    )
    key_weakness = derive_key_weakness(
        structural_fields,
        opening_name=opening_name,
        best_move=trigger_move["best_move"],
    )
    king_safety_state = derive_king_safety_state(
        structural_fields,
        opening_name=opening_name,
        headers=headers,
    )
    primary_break_move = next(
        (move for move in critical_moves if move.get("ply") == event_summary["primary_break_ply"]),
        critical_moves[0] if critical_moves else {},
    )
    piece_activity = derive_piece_activity(
        structural_fields,
        critical_reason_type=primary_break_move.get("critical_reason_type", ""),
        candidate_ranking_type=primary_break_move.get("candidate_ranking_type", ""),
        move_count=len(moves),
    )
    structural = structural_candidates_from_fields(
        structural_fields,
        key_weakness,
        opening_name=opening_name,
    )
    structural_links = structural_link_fields(structural_fields)

    for move in critical_moves:
        move["structural_link_format_version"] = structural_links["format_version"]
        move["linked_king_zone_target"] = structural_links["linked_king_zone_target"]
        move["linked_vulnerable_piece_target"] = structural_links["linked_vulnerable_piece_target"]
        move["linked_pressure_lane_target"] = structural_links["linked_pressure_lane_target"]
        move["linked_decisive_imbalance_target"] = structural_links["linked_decisive_imbalance_target"]
        move["linked_pinned_critical_piece"] = structural_links["linked_pinned_critical_piece"]
        move["linked_defender_removal_exposure"] = structural_links["linked_defender_removal_exposure"]
        move["linked_king_color_complex"] = structural_links["linked_king_color_complex"]
        move["linked_target_zone_imbalance"] = structural_links["linked_target_zone_imbalance"]
        move["linked_attacker_coordination"] = structural_links["linked_attacker_coordination"]
        move["linked_defensive_network_fragility"] = structural_links["linked_defensive_network_fragility"]
        move["structural_link_summary"] = structural_links["structural_link_summary"]

    evidence_payload = {
        "worker_version": "0.5",
        "engine_name": Path(stockfish_path).name,
        "engine_depth": depth,
        "engine_multipv": multipv,
        "analysis_window_start_ply": analysis_window_start,
        "analysis_window_end_ply": analysis_window_end,
        "adjacent_conflict_plies": ADJACENT_CONFLICT_PLIES,
        "mapping": {
            "mapping_status": mapping["mapping_status"],
            "mapped_start_ply": mapping["mapped_start_ply"],
            "mapping_method": mapping["mapping_method"],
            "mapping_confidence": mapping["mapping_confidence"],
            "mapping_notes": mapping["mapping_notes"],
            "candidate_plies": mapping["candidate_plies"],
        },
        "retained_role_selection": event_summary,
        "structural_evidence_v1": {
            "king_exposure_type": structural_fields["king_exposure_type"],
            "king_exposure_severity": structural_fields["king_exposure_severity"],
            "loose_piece_count": structural_fields["loose_piece_count"],
            "loose_piece_summary": structural_fields["loose_piece_summary"],
            "overloaded_defender_count": structural_fields["overloaded_defender_count"],
            "overloaded_defender_summary": structural_fields["overloaded_defender_summary"],
            "back_rank_state": structural_fields["back_rank_state"],
            "luft_state": structural_fields["luft_state"],
            "king_line_pressure_type": structural_fields["king_line_pressure_type"],
            "king_square_pressure_type": structural_fields["king_square_pressure_type"],
            "critical_piece_imbalance_summary": structural_fields["critical_piece_imbalance_summary"],
            "king_zone_target_type": structural_fields["king_zone_target_type"],
            "king_zone_target_summary": structural_fields["king_zone_target_summary"],
            "vulnerable_piece_target_type": structural_fields["vulnerable_piece_target_type"],
            "vulnerable_piece_target_summary": structural_fields["vulnerable_piece_target_summary"],
            "pressure_lane_target_type": structural_fields["pressure_lane_target_type"],
            "pressure_lane_target_summary": structural_fields["pressure_lane_target_summary"],
            "decisive_imbalance_target": structural_fields["decisive_imbalance_target"],
            "structural_feature_summary": structural_fields["structural_feature_summary"],
            "structural_feature_confidence": structural_fields["structural_feature_confidence"],
            "local_target_summary": structural_fields["local_target_summary"],
            "local_target_confidence": structural_fields["local_target_confidence"],
            "focus_side": collapse_side,
            "focus_position_fen": primary_break_board.fen(),
        },
        "structural_evidence_v2": {
            "pinned_critical_piece_type": structural_fields["pinned_critical_piece_type"],
            "pinned_critical_piece_summary": structural_fields["pinned_critical_piece_summary"],
            "defender_removal_exposure_type": structural_fields["defender_removal_exposure_type"],
            "defender_removal_exposure_summary": structural_fields["defender_removal_exposure_summary"],
            "king_color_complex_state": structural_fields["king_color_complex_state"],
            "king_color_complex_summary": structural_fields["king_color_complex_summary"],
            "target_zone_imbalance_type": structural_fields["target_zone_imbalance_type"],
            "target_zone_imbalance_summary": structural_fields["target_zone_imbalance_summary"],
            "structural_v2_summary": structural_fields["structural_v2_summary"],
            "structural_v2_confidence": structural_fields["structural_v2_confidence"],
        },
        "structural_evidence_v3": {
            "escape_geometry_state": structural_fields["escape_geometry_state"],
            "escape_geometry_summary": structural_fields["escape_geometry_summary"],
            "flight_control_type": structural_fields["flight_control_type"],
            "flight_control_summary": structural_fields["flight_control_summary"],
            "defensive_escape_fragility_type": structural_fields["defensive_escape_fragility_type"],
            "defensive_escape_fragility_summary": structural_fields["defensive_escape_fragility_summary"],
            "structural_v3_summary": structural_fields["structural_v3_summary"],
            "structural_v3_confidence": structural_fields["structural_v3_confidence"],
        },
        "structural_evidence_v4": {
            "attacker_coordination_type": structural_fields["attacker_coordination_type"],
            "attacker_coordination_summary": structural_fields["attacker_coordination_summary"],
            "defensive_network_fragility_type": structural_fields["defensive_network_fragility_type"],
            "defensive_network_fragility_summary": structural_fields["defensive_network_fragility_summary"],
            "structural_v4_summary": structural_fields["structural_v4_summary"],
            "structural_v4_confidence": structural_fields["structural_v4_confidence"],
        },
        "critical_move_structural_links_v2": structural_links,
        "local_window_rules": {
            "decisive_swing_cp": DECISIVE_SWING_CP,
            "only_move_gap_cp": ONLY_MOVE_GAP_CP,
            "near_only_move_gap_cp": NEAR_ONLY_MOVE_GAP_CP,
            "third_choice_only_move_gap_cp": THIRD_CHOICE_ONLY_MOVE_GAP_CP,
            "third_choice_near_only_move_gap_cp": THIRD_CHOICE_NEAR_ONLY_MOVE_GAP_CP,
            "last_holding_delta_cp": LAST_HOLDING_DELTA_CP,
            "preventative_delta_cp": PREVENTATIVE_DELTA_CP,
            "missed_counterplay_delta_cp": MISSED_COUNTERPLAY_DELTA_CP,
            "materially_stronger_alternative_cp": MATERIALLY_STRONGER_ALTERNATIVE_CP,
            "mate_advantage_plies": MATE_ADVANTAGE_PLIES,
            "forced_mate_plies": FORCED_MATE_PLIES,
            "forced_defense_plies": FORCED_DEFENSE_PLIES,
            "score_convention": "all scores are normalized from the mover perspective on the analysed pre-move board; positive is better for the mover, mate scores outrank centipawn scores, and eval_delta_cp is null when the comparison is mate-driven",
        },
        "headers": headers,
        "solution_moves": solution_moves,
        "facts_only": True,
    }

    return {
        "protocol_version": "0.1",
        "response_type": "analysis_result",
        "analysis_run": {
            "run_id": analysis_run.get("run_id", ""),
            "puzzle_id": analysis_run.get("puzzle_id", puzzle_round.get("puzzle_id", "")),
            "status": "completed",
            "engine_mode": analysis_run.get("engine_mode", "stockfish_window_v0_1"),
            "engine_name": Path(stockfish_path).name,
            "engine_depth": depth,
            "created_at": analysis_run.get("created_at", utc_now()),
            "completed_at": utc_now(),
        },
        "tactical_event": {
            "event_id": event_id,
            "run_id": analysis_run.get("run_id", ""),
            "puzzle_id": puzzle_round.get("puzzle_id", ""),
            "source_game_id": source_game.get("source_game_id", ""),
            "opening_family": opening_family,
            "game_phase": game_phase,
            "solution_summary": solution_summary,
            "side_to_move": side_to_move or "unknown",
            "material_balance": material_balance,
            "king_safety_state": king_safety_state,
            "piece_activity": piece_activity,
            "key_weakness": key_weakness,
            "king_exposure_type": structural_fields["king_exposure_type"],
            "king_exposure_severity": structural_fields["king_exposure_severity"],
            "loose_piece_count": structural_fields["loose_piece_count"],
            "loose_piece_summary": structural_fields["loose_piece_summary"],
            "overloaded_defender_count": structural_fields["overloaded_defender_count"],
            "overloaded_defender_summary": structural_fields["overloaded_defender_summary"],
            "back_rank_state": structural_fields["back_rank_state"],
            "luft_state": structural_fields["luft_state"],
            "king_line_pressure_type": structural_fields["king_line_pressure_type"],
            "king_square_pressure_type": structural_fields["king_square_pressure_type"],
            "critical_piece_imbalance_summary": structural_fields["critical_piece_imbalance_summary"],
            "structural_feature_summary": structural_fields["structural_feature_summary"],
            "structural_feature_confidence": structural_fields["structural_feature_confidence"],
            "king_zone_target_type": structural_fields["king_zone_target_type"],
            "king_zone_target_summary": structural_fields["king_zone_target_summary"],
            "vulnerable_piece_target_type": structural_fields["vulnerable_piece_target_type"],
            "vulnerable_piece_target_summary": structural_fields["vulnerable_piece_target_summary"],
            "pressure_lane_target_type": structural_fields["pressure_lane_target_type"],
            "pressure_lane_target_summary": structural_fields["pressure_lane_target_summary"],
            "decisive_imbalance_target": structural_fields["decisive_imbalance_target"],
            "pinned_critical_piece_type": structural_fields["pinned_critical_piece_type"],
            "pinned_critical_piece_summary": structural_fields["pinned_critical_piece_summary"],
            "defender_removal_exposure_type": structural_fields["defender_removal_exposure_type"],
            "defender_removal_exposure_summary": structural_fields["defender_removal_exposure_summary"],
            "king_color_complex_state": structural_fields["king_color_complex_state"],
            "king_color_complex_summary": structural_fields["king_color_complex_summary"],
            "target_zone_imbalance_type": structural_fields["target_zone_imbalance_type"],
            "target_zone_imbalance_summary": structural_fields["target_zone_imbalance_summary"],
            "structural_v2_summary": structural_fields["structural_v2_summary"],
            "structural_v2_confidence": structural_fields["structural_v2_confidence"],
            "escape_geometry_state": structural_fields["escape_geometry_state"],
            "escape_geometry_summary": structural_fields["escape_geometry_summary"],
            "flight_control_type": structural_fields["flight_control_type"],
            "flight_control_summary": structural_fields["flight_control_summary"],
            "defensive_escape_fragility_type": structural_fields["defensive_escape_fragility_type"],
            "defensive_escape_fragility_summary": structural_fields["defensive_escape_fragility_summary"],
            "structural_v3_summary": structural_fields["structural_v3_summary"],
            "structural_v3_confidence": structural_fields["structural_v3_confidence"],
            "attacker_coordination_type": structural_fields["attacker_coordination_type"],
            "attacker_coordination_summary": structural_fields["attacker_coordination_summary"],
            "defensive_network_fragility_type": structural_fields["defensive_network_fragility_type"],
            "defensive_network_fragility_summary": structural_fields["defensive_network_fragility_summary"],
            "structural_v4_summary": structural_fields["structural_v4_summary"],
            "structural_v4_confidence": structural_fields["structural_v4_confidence"],
            "local_target_summary": structural_fields["local_target_summary"],
            "local_target_confidence": structural_fields["local_target_confidence"],
            "trigger_event": trigger_move["played_move"],
            "player_rating_range": player_rating_range,
            "mapping_status": mapping["mapping_status"],
            "mapped_start_ply": mapped_start_ply,
            "mapping_method": mapping["mapping_method"],
            "mapping_confidence": mapping["mapping_confidence"],
            "mapping_notes": mapping["mapping_notes"],
            "analysis_window_start_ply": analysis_window_start,
            "analysis_window_end_ply": analysis_window_end,
            "primary_break_ply": event_summary["primary_break_ply"],
            "primary_break_reason": event_summary["primary_break_reason"],
            "retained_break_format_version": event_summary["retained_break_format_version"],
            "retained_break_role": event_summary["retained_break_role"],
            "retained_break_played_move": event_summary["retained_break_played_move"],
            "retained_break_best_move": event_summary["retained_break_best_move"],
            "retained_break_compact_sequence": event_summary["retained_break_compact_sequence"],
            "retained_break_summary": event_summary["retained_break_summary"],
            "best_vs_played_divergence_summary": event_summary["best_vs_played_divergence_summary"],
            "divergence_type": event_summary["divergence_type"],
            "divergence_severity": event_summary["divergence_severity"],
            "divergence_compact_summary": event_summary["divergence_compact_summary"],
            "divergence_evidence_notes": event_summary["divergence_evidence_notes"],
            "local_sequence_confidence": event_summary["local_sequence_confidence"],
            "collapse_sequence_format_version": event_summary["collapse_sequence_format_version"],
            "collapse_sequence_type": event_summary["collapse_sequence_type"],
            "collapse_sequence_summary": event_summary["collapse_sequence_summary"],
            "retained_role_summary": event_summary["retained_role_summary"],
            "omitted_adjacent_candidate_count": event_summary["omitted_adjacent_candidate_count"],
            "omission_reason_summary": event_summary["omission_reason_summary"],
            "omission_reason_counts_json": event_summary["omission_reason_counts_json"],
            "engine_limit_summary": f"depth={depth}, multipv={multipv}, window_before={window_before}, window_after={window_after}",
            "tactical_candidates_json": json.dumps(tactical),
            "structural_candidates_json": json.dumps(structural),
            "evidence_payload_json": json.dumps(evidence_payload),
            "assistant_inference_status": "pending",
            "assistant_labels_json": None,
            "assistant_summary_markdown": None,
        },
        "critical_moves": critical_moves,
    }


def main() -> int:
    raw = sys.stdin.read()
    if not raw.strip():
        sys.stderr.write("worker received empty stdin\n")
        return 1

    try:
        request = json.loads(raw)
        response = analysis_for_request(request)
    except ValueError as exc:
        sys.stderr.write(f"{exc}\n")
        return 2
    except chess.engine.EngineError as exc:
        sys.stderr.write(f"engine analysis failure: {exc}\n")
        return 3
    except chess.engine.EngineTerminatedError as exc:
        sys.stderr.write(f"engine launch failure: {exc}\n")
        return 4
    except Exception as exc:  # pragma: no cover
        sys.stderr.write(f"worker failure: {exc}\n")
        return 5

    sys.stdout.write(json.dumps(response, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
