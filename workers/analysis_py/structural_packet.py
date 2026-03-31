from __future__ import annotations

from typing import Any

import chess

try:
    from workers.analysis_py.structural_basics import (
        back_rank_and_luft_state,
        color_from_name,
        decisive_imbalance_target,
        imbalance_records,
        king_exposure_fields,
        king_zone_squares,
        king_zone_target_fields,
        local_target_confidence,
        local_target_summary,
        loose_piece_records,
        overloaded_defender_records,
        overloaded_summary,
        pressure_lane_target_fields,
        pressure_type_from_lines,
        square_pressure_type,
        structural_feature_confidence,
        structural_feature_summary,
        summary_from_piece_records,
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
        structural_v4_confidence,
        structural_v4_summary,
        structural_v2_confidence,
        structural_v2_summary,
        structural_v3_confidence,
        structural_v3_summary,
        target_zone_imbalance_fields,
    )
except ModuleNotFoundError:
    from structural_basics import (
        back_rank_and_luft_state,
        color_from_name,
        decisive_imbalance_target,
        imbalance_records,
        king_exposure_fields,
        king_zone_squares,
        king_zone_target_fields,
        local_target_confidence,
        local_target_summary,
        loose_piece_records,
        overloaded_defender_records,
        overloaded_summary,
        pressure_lane_target_fields,
        pressure_type_from_lines,
        square_pressure_type,
        structural_feature_confidence,
        structural_feature_summary,
        summary_from_piece_records,
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
        structural_v4_confidence,
        structural_v4_summary,
        structural_v2_confidence,
        structural_v2_summary,
        structural_v3_confidence,
        structural_v3_summary,
        target_zone_imbalance_fields,
    )


def extract_structural_evidence(
    board: chess.Board,
    focus_side: str,
    played_move_uci: str,
    best_move_uci: str,
) -> dict[str, Any]:
    focus_color = color_from_name(focus_side)
    enemy_color = not focus_color
    king_square = board.king(focus_color)
    if king_square is None:
        return {
            "king_exposure_type": "no_clear_exposure",
            "king_exposure_severity": "none",
            "loose_piece_count": 0,
            "loose_piece_summary": "none",
            "overloaded_defender_count": 0,
            "overloaded_defender_summary": "none",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "unknown",
            "king_line_pressure_type": "no_clear_line_pressure",
            "king_square_pressure_type": "balanced_square_control",
            "critical_piece_imbalance_summary": "balanced_local_targets",
            "structural_feature_summary": "no_clear_structural_trigger",
            "structural_feature_confidence": "low",
            "king_zone_target_type": "no_clear_king_zone_target",
            "king_zone_target_summary": "none",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "vulnerable_piece_target_summary": "none",
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "pressure_lane_target_summary": "none",
            "decisive_imbalance_target": "none",
            "pinned_critical_piece_type": "no_clear_pinned_critical_piece",
            "pinned_critical_piece_summary": "none",
            "defender_removal_exposure_type": "no_clear_defender_removal_exposure",
            "defender_removal_exposure_summary": "none",
            "king_color_complex_state": "no_clear_color_complex_weakness",
            "king_color_complex_summary": "none",
            "target_zone_imbalance_type": "balanced_target_zone",
            "target_zone_imbalance_summary": "balanced",
            "structural_v2_summary": "no_clear_structural_v2",
            "structural_v2_confidence": "low",
            "escape_geometry_state": "no_clear_escape_geometry",
            "escape_geometry_summary": "none",
            "flight_control_type": "no_clear_flight_control",
            "flight_control_summary": "none",
            "defensive_escape_fragility_type": "no_clear_escape_fragility",
            "defensive_escape_fragility_summary": "none",
            "structural_v3_summary": "no_clear_structural_v3",
            "structural_v3_confidence": "low",
            "attacker_coordination_type": "no_clear_attacker_coordination",
            "attacker_coordination_summary": "none",
            "defensive_network_fragility_type": "no_clear_defensive_network_fragility",
            "defensive_network_fragility_summary": "none",
            "structural_v4_summary": "no_clear_structural_v4",
            "structural_v4_confidence": "low",
            "local_target_summary": "none",
            "local_target_confidence": "low",
        }

    relevant_squares: set[chess.Square] = {king_square}
    for move_uci in (played_move_uci, best_move_uci):
        if move_uci and move_uci != "unknown":
            move = chess.Move.from_uci(move_uci)
            relevant_squares.add(move.from_square)
            relevant_squares.add(move.to_square)

    zone = king_zone_squares(board, king_square)
    line_pressure = pressure_type_from_lines(board, zone, enemy_color)
    back_rank = back_rank_and_luft_state(board, focus_color, king_square, line_pressure)
    square_pressure = square_pressure_type(board, zone, focus_color)
    exposure = king_exposure_fields(board, focus_color, king_square, line_pressure, back_rank["luft_state"])
    loose = loose_piece_records(board, focus_color, relevant_squares)
    imbalance = imbalance_records(board, focus_color, relevant_squares)
    overloaded = overloaded_defender_records(board, focus_color, zone, imbalance)
    king_target = king_zone_target_fields(board, focus_color, zone, king_square)
    vulnerable_target = vulnerable_piece_target_fields(loose, imbalance)
    lane_target = pressure_lane_target_fields(board, zone, enemy_color)
    imbalance_target = decisive_imbalance_target(imbalance)
    weak_color_complex = False
    king_square_color = (chess.square_file(king_square) + chess.square_rank(king_square)) % 2
    weak_same_color_squares = 0
    for square in zone:
        square_color = (chess.square_file(square) + chess.square_rank(square)) % 2
        if square_color != king_square_color:
            continue
        if len(board.attackers(enemy_color, square)) > len(board.attackers(focus_color, square)):
            weak_same_color_squares += 1
    if weak_same_color_squares >= 2:
        weak_color_complex = True

    pinned_critical_piece = False
    for square, piece in board.piece_map().items():
        if piece.color != focus_color or piece.piece_type == chess.KING:
            continue
        if not board.is_pinned(focus_color, square):
            continue
        if square in relevant_squares or square in zone or square in {record["square"] for record in imbalance}:
            pinned_critical_piece = True
            break

    defender_removal_exposure = bool(overloaded) and (
        king_target["type"] != "no_clear_king_zone_target"
        or vulnerable_target["type"] != "no_clear_vulnerable_piece_target"
        or imbalance_target != "none"
    )
    pinned_fields = pinned_critical_piece_fields(board, focus_color, zone, imbalance, overloaded)
    defender_removal_fields = defender_removal_exposure_fields(overloaded, king_target, vulnerable_target, imbalance_target)
    color_complex_fields = king_color_complex_fields(
        board,
        focus_color,
        zone,
        king_target,
        line_pressure,
        square_pressure,
    )
    zone_imbalance_fields = target_zone_imbalance_fields(board, focus_color, zone, king_target)
    escape_geometry = escape_geometry_fields(board, focus_color, king_square)
    flight_control = flight_control_fields(board, focus_color, king_square, king_target)
    defensive_escape_fragility = defensive_escape_fragility_fields(
        board,
        focus_color,
        king_square,
        overloaded,
    )
    attacker_coordination = attacker_coordination_fields(
        board,
        focus_color,
        zone,
        king_target,
        lane_target,
        imbalance,
    )
    defensive_network_fragility = defensive_network_fragility_fields(
        board,
        focus_color,
        zone,
        king_target,
        imbalance,
        overloaded,
        defender_removal_fields,
    )

    fields: dict[str, Any] = {
        "king_exposure_type": exposure["type"],
        "king_exposure_severity": exposure["severity"],
        "loose_piece_count": len(loose),
        "loose_piece_summary": summary_from_piece_records(loose),
        "overloaded_defender_count": len(overloaded),
        "overloaded_defender_summary": overloaded_summary(overloaded),
        "back_rank_state": back_rank["back_rank_state"],
        "luft_state": back_rank["luft_state"],
        "king_line_pressure_type": line_pressure,
        "king_square_pressure_type": square_pressure,
        "critical_piece_imbalance_summary": (
            summary_from_piece_records(imbalance, include_counts=True) if imbalance else "balanced_local_targets"
        ),
        "king_zone_target_type": king_target["type"],
        "king_zone_target_summary": king_target["summary"],
        "vulnerable_piece_target_type": vulnerable_target["type"],
        "vulnerable_piece_target_summary": vulnerable_target["summary"],
        "pressure_lane_target_type": lane_target["type"],
        "pressure_lane_target_summary": lane_target["summary"],
        "decisive_imbalance_target": imbalance_target,
        **pinned_fields,
        **defender_removal_fields,
        **color_complex_fields,
        **zone_imbalance_fields,
        "escape_geometry_state": escape_geometry["escape_geometry_state"],
        "escape_geometry_summary": escape_geometry["escape_geometry_summary"],
        "flight_control_type": flight_control["flight_control_type"],
        "flight_control_summary": flight_control["flight_control_summary"],
        "defensive_escape_fragility_type": defensive_escape_fragility["defensive_escape_fragility_type"],
        "defensive_escape_fragility_summary": defensive_escape_fragility["defensive_escape_fragility_summary"],
        "attacker_coordination_type": attacker_coordination["attacker_coordination_type"],
        "attacker_coordination_summary": attacker_coordination["attacker_coordination_summary"],
        "defensive_network_fragility_type": defensive_network_fragility["defensive_network_fragility_type"],
        "defensive_network_fragility_summary": defensive_network_fragility["defensive_network_fragility_summary"],
        "weak_color_complex_signal": weak_color_complex,
        "pinned_critical_piece_signal": pinned_critical_piece,
        "defender_removal_exposure_signal": defender_removal_exposure,
    }
    fields["structural_feature_summary"] = structural_feature_summary(fields)
    fields["structural_feature_confidence"] = structural_feature_confidence(fields)
    fields["local_target_summary"] = local_target_summary(fields)
    fields["local_target_confidence"] = local_target_confidence(fields)
    fields["structural_v2_summary"] = structural_v2_summary(fields)
    fields["structural_v2_confidence"] = structural_v2_confidence(fields)
    fields["structural_v3_summary"] = structural_v3_summary(fields)
    fields["structural_v3_confidence"] = structural_v3_confidence(fields)
    fields["structural_v4_summary"] = structural_v4_summary(fields)
    fields["structural_v4_confidence"] = structural_v4_confidence(fields)
    return fields
