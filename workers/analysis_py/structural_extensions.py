from __future__ import annotations

from typing import Any

import chess

try:
    from workers.analysis_py.structural_basics import (
        piece_token,
        target_square_summary,
    )
except ModuleNotFoundError:
    from structural_basics import (
        piece_token,
        target_square_summary,
    )


def pinned_critical_piece_fields(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    imbalance: list[dict[str, Any]],
    overloaded: list[dict[str, Any]],
) -> dict[str, str]:
    imbalance_squares = {record["square"] for record in imbalance[:2]}
    king_square = board.king(focus_color)
    zone_targets = {square for square in zone if len(board.attackers(not focus_color, square)) > 0}

    for record in overloaded:
        square = record["square"]
        piece = board.piece_at(square)
        if piece is None or piece.color != focus_color or piece.piece_type == chess.KING:
            continue
        if board.is_pinned(focus_color, square):
            return {
                "pinned_critical_piece_type": "pinned_overloaded_defender",
                "pinned_critical_piece_summary": piece_token(piece, square),
            }

    for square, piece in board.piece_map().items():
        if piece.color != focus_color or piece.piece_type == chess.KING:
            continue
        if not board.is_pinned(focus_color, square):
            continue
        if (
            king_square is not None
            and square not in zone_targets
            and square in board.attackers(focus_color, king_square)
        ):
            return {
                "pinned_critical_piece_type": "pinned_king_defender",
                "pinned_critical_piece_summary": piece_token(piece, square),
            }
        if any(square in board.attackers(focus_color, target) for target in imbalance_squares):
            return {
                "pinned_critical_piece_type": "pinned_target_defender",
                "pinned_critical_piece_summary": piece_token(piece, square),
            }

    return {
        "pinned_critical_piece_type": "no_clear_pinned_critical_piece",
        "pinned_critical_piece_summary": "none",
    }


def defender_removal_exposure_fields(
    overloaded: list[dict[str, Any]],
    king_target: dict[str, str],
    vulnerable_target: dict[str, str],
    imbalance_target: str,
) -> dict[str, str]:
    anchor = king_target["summary"]
    if anchor == "none" and vulnerable_target["summary"] != "none":
        anchor = vulnerable_target["summary"]
    if anchor == "none" and imbalance_target != "none":
        anchor = imbalance_target

    if overloaded:
        if anchor != "none":
            return {
                "defender_removal_exposure_type": "deflection_sensitive_defense",
                "defender_removal_exposure_summary": f"{piece_token(overloaded[0]['piece'], overloaded[0]['square'])} -> {anchor}",
            }
    if anchor != "none":
        return {
            "defender_removal_exposure_type": "single_defender_exposure",
            "defender_removal_exposure_summary": anchor,
        }
    return {
        "defender_removal_exposure_type": "no_clear_defender_removal_exposure",
        "defender_removal_exposure_summary": "none",
    }


def king_color_complex_fields(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    king_target: dict[str, str],
    line_pressure: str,
    square_pressure: str,
) -> dict[str, str]:
    king_square = board.king(focus_color)
    if king_square is None:
        return {
            "king_color_complex_state": "no_clear_color_complex_weakness",
            "king_color_complex_summary": "none",
        }

    enemy_color = not focus_color
    weak_by_color = {0: 0, 1: 0}
    for square in zone:
        square_color = (chess.square_file(square) + chess.square_rank(square)) % 2
        if len(board.attackers(enemy_color, square)) > len(board.attackers(focus_color, square)):
            weak_by_color[square_color] += 1

    if (
        king_target["type"] != "no_clear_king_zone_target"
        or line_pressure != "no_clear_line_pressure"
        or square_pressure in {"concentrated_square_pressure", "localized_square_pressure"}
    ):
        if weak_by_color[0] >= 2:
            return {
                "king_color_complex_state": "weak_light_complex",
                "king_color_complex_summary": f"light {weak_by_color[0]} weak squares",
            }
        if weak_by_color[1] >= 2:
            return {
                "king_color_complex_state": "weak_dark_complex",
                "king_color_complex_summary": f"dark {weak_by_color[1]} weak squares",
            }

    return {
        "king_color_complex_state": "no_clear_color_complex_weakness",
        "king_color_complex_summary": "none",
    }


def target_zone_imbalance_fields(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    king_target: dict[str, str],
) -> dict[str, str]:
    enemy_color = not focus_color
    strongest_margin = 0
    weakest_defense = 99
    best_summary = "balanced"
    pressured_squares = 0

    zone_targets = zone if king_target["type"] == "no_clear_king_zone_target" else [board.king(focus_color)] + zone
    for square in zone_targets:
        enemy_attackers = len(board.attackers(enemy_color, square))
        friendly_defenders = len(board.attackers(focus_color, square))
        margin = enemy_attackers - friendly_defenders
        if margin > 0:
            pressured_squares += 1
            if margin > strongest_margin:
                strongest_margin = margin
                best_summary = target_square_summary(square, enemy_attackers, friendly_defenders)
            weakest_defense = min(weakest_defense, friendly_defenders)

    if strongest_margin >= 2:
        return {"target_zone_imbalance_type": "attacker_heavy", "target_zone_imbalance_summary": best_summary}
    if pressured_squares >= 1 and weakest_defense <= 1:
        return {"target_zone_imbalance_type": "defender_fragile", "target_zone_imbalance_summary": best_summary}
    if pressured_squares >= 2:
        return {"target_zone_imbalance_type": "mixed_target_zone_pressure", "target_zone_imbalance_summary": best_summary}
    return {"target_zone_imbalance_type": "balanced_target_zone", "target_zone_imbalance_summary": "balanced"}


def structural_v2_summary(fields: dict[str, Any]) -> str:
    parts: list[str] = []
    if fields["pinned_critical_piece_type"] != "no_clear_pinned_critical_piece":
        parts.append(f"pinned {fields['pinned_critical_piece_summary']}")
    if fields["defender_removal_exposure_type"] != "no_clear_defender_removal_exposure":
        parts.append("defender removal exposure")
    if fields["king_color_complex_state"] != "no_clear_color_complex_weakness":
        parts.append(fields["king_color_complex_state"])
    if fields["target_zone_imbalance_type"] != "balanced_target_zone":
        parts.append(fields["target_zone_imbalance_type"])
    return " | ".join(parts) if parts else "no_clear_structural_v2"


def structural_v2_confidence(fields: dict[str, Any]) -> str:
    signals = 0
    if fields["pinned_critical_piece_type"] != "no_clear_pinned_critical_piece":
        signals += 1
    if fields["defender_removal_exposure_type"] != "no_clear_defender_removal_exposure":
        signals += 1
    if fields["king_color_complex_state"] != "no_clear_color_complex_weakness":
        signals += 1
    if fields["target_zone_imbalance_type"] != "balanced_target_zone":
        signals += 1
    if signals >= 3:
        return "high"
    if signals >= 1:
        return "medium"
    return "low"


def escape_geometry_fields(board: chess.Board, focus_color: chess.Color, king_square: chess.Square) -> dict[str, str]:
    enemy_color = not focus_color
    adjacent = [
        square
        for square in chess.SquareSet(chess.BB_KING_ATTACKS[king_square])
    ]
    blocked = 0
    attacked = 0
    accessible = 0

    for square in adjacent:
        occupant = board.piece_at(square)
        if occupant is not None and occupant.color == focus_color:
            blocked += 1
            continue
        if len(board.attackers(enemy_color, square)) > 0:
            attacked += 1
        else:
            accessible += 1

    if accessible == 0 and attacked >= 2:
        return {
            "escape_geometry_state": "sealed_king_box",
            "escape_geometry_summary": f"escapes 0 | attacked {attacked} | blocked {blocked}",
        }
    if attacked >= 2:
        return {
            "escape_geometry_state": "attacked_flight_squares",
            "escape_geometry_summary": f"escapes {accessible} | attacked {attacked} | blocked {blocked}",
        }
    if blocked >= 2 and accessible <= 1:
        return {
            "escape_geometry_state": "blocked_flight_squares",
            "escape_geometry_summary": f"escapes {accessible} | attacked {attacked} | blocked {blocked}",
        }
    return {
        "escape_geometry_state": "no_clear_escape_geometry",
        "escape_geometry_summary": "none",
    }


def flight_control_fields(
    board: chess.Board,
    focus_color: chess.Color,
    king_square: chess.Square,
    king_target: dict[str, str],
) -> dict[str, str]:
    enemy_color = not focus_color
    attacked_adjacent = 0
    for square in chess.SquareSet(chess.BB_KING_ATTACKS[king_square]):
        occupant = board.piece_at(square)
        if occupant is not None and occupant.color == focus_color:
            continue
        if len(board.attackers(enemy_color, square)) > 0:
            attacked_adjacent += 1

    if king_target["type"] != "no_clear_king_zone_target" and attacked_adjacent >= 1:
        return {
            "flight_control_type": "mixed_flight_control",
            "flight_control_summary": f"{king_target['summary']} + {attacked_adjacent} attacked flights",
        }
    if king_target["type"] != "no_clear_king_zone_target":
        return {
            "flight_control_type": "king_square_control",
            "flight_control_summary": king_target["summary"],
        }
    if attacked_adjacent >= 2:
        return {
            "flight_control_type": "escape_square_control",
            "flight_control_summary": f"{attacked_adjacent} attacked flights",
        }
    return {
        "flight_control_type": "no_clear_flight_control",
        "flight_control_summary": "none",
    }


def defensive_escape_fragility_fields(
    board: chess.Board,
    focus_color: chess.Color,
    king_square: chess.Square,
    overloaded: list[dict[str, Any]],
) -> dict[str, str]:
    enemy_color = not focus_color
    pressured_escape_squares = [
        square
        for square in chess.SquareSet(chess.BB_KING_ATTACKS[king_square])
        if (
            (board.piece_at(square) is None or board.piece_at(square).color != focus_color)
            and len(board.attackers(enemy_color, square)) > 0
        )
    ]

    for record in overloaded:
        square = record["square"]
        if any(square in board.attackers(focus_color, target) for target in pressured_escape_squares + [king_square]):
            return {
                "defensive_escape_fragility_type": "overloaded_escape_defender",
                "defensive_escape_fragility_summary": piece_token(record["piece"], square),
            }

    for square in pressured_escape_squares:
        friendly_defenders = len(board.attackers(focus_color, square))
        if friendly_defenders <= 1:
            enemy_attackers = len(board.attackers(enemy_color, square))
            return {
                "defensive_escape_fragility_type": "single_escape_defender",
                "defensive_escape_fragility_summary": target_square_summary(square, enemy_attackers, friendly_defenders),
            }

    enemy_attackers = len(board.attackers(enemy_color, king_square))
    friendly_defenders = len(board.attackers(focus_color, king_square))
    if enemy_attackers > friendly_defenders and friendly_defenders <= 1:
        return {
            "defensive_escape_fragility_type": "single_escape_defender",
            "defensive_escape_fragility_summary": target_square_summary(king_square, enemy_attackers, friendly_defenders),
        }

    return {
        "defensive_escape_fragility_type": "no_clear_escape_fragility",
        "defensive_escape_fragility_summary": "none",
    }


def structural_v3_summary(fields: dict[str, Any]) -> str:
    parts: list[str] = []
    if fields["escape_geometry_state"] != "no_clear_escape_geometry":
        parts.append(fields["escape_geometry_state"])
    if fields["flight_control_type"] != "no_clear_flight_control":
        parts.append(fields["flight_control_type"])
    if fields["defensive_escape_fragility_type"] != "no_clear_escape_fragility":
        parts.append(fields["defensive_escape_fragility_type"])
    return " | ".join(parts) if parts else "no_clear_structural_v3"


def structural_v3_confidence(fields: dict[str, Any]) -> str:
    signals = 0
    if fields["escape_geometry_state"] != "no_clear_escape_geometry":
        signals += 1
    if fields["flight_control_type"] != "no_clear_flight_control":
        signals += 1
    if fields["defensive_escape_fragility_type"] != "no_clear_escape_fragility":
        signals += 1
    if signals >= 2:
        return "high"
    if signals >= 1:
        return "medium"
    return "low"


def _lane_file_index(summary: str) -> int | None:
    parts = summary.split()
    if len(parts) != 2 or len(parts[1]) != 1:
        return None
    file_name = parts[1]
    if file_name < "a" or file_name > "h":
        return None
    return ord(file_name) - ord("a")


def _diagonal_squares(summary: str) -> list[chess.Square]:
    parts = summary.split()
    if len(parts) != 2 or "-" not in parts[1]:
        return []
    start_name, end_name = parts[1].split("-", 1)
    try:
        start = chess.parse_square(start_name)
        end = chess.parse_square(end_name)
    except ValueError:
        return []

    file_step = 1 if chess.square_file(end) > chess.square_file(start) else -1
    rank_step = 1 if chess.square_rank(end) > chess.square_rank(start) else -1
    if abs(chess.square_file(end) - chess.square_file(start)) != abs(chess.square_rank(end) - chess.square_rank(start)):
        return []

    squares: list[chess.Square] = []
    current_file = chess.square_file(start)
    current_rank = chess.square_rank(start)
    while 0 <= current_file <= 7 and 0 <= current_rank <= 7:
        squares.append(chess.square(current_file, current_rank))
        if current_file == chess.square_file(end) and current_rank == chess.square_rank(end):
            break
        current_file += file_step
        current_rank += rank_step
    return squares


def attacker_coordination_fields(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    king_target: dict[str, str],
    lane_target: dict[str, str],
    imbalance: list[dict[str, Any]],
) -> dict[str, str]:
    enemy_color = not focus_color

    if lane_target["type"] == "file_lane_target":
        file_index = _lane_file_index(lane_target["summary"])
        if file_index is not None:
            sliders = [
                piece_token(piece, square)
                for square, piece in board.piece_map().items()
                if piece.color == enemy_color
                and piece.piece_type in (chess.ROOK, chess.QUEEN)
                and chess.square_file(square) == file_index
            ]
            if len(sliders) >= 2:
                return {
                    "attacker_coordination_type": "file_battery",
                    "attacker_coordination_summary": f"{lane_target['summary']} via {'+'.join(sliders[:2])}",
                }

    if lane_target["type"] == "diagonal_lane_target":
        diagonal = set(_diagonal_squares(lane_target["summary"]))
        sliders = [
            piece_token(piece, square)
            for square, piece in board.piece_map().items()
            if piece.color == enemy_color
            and piece.piece_type in (chess.BISHOP, chess.QUEEN)
            and square in diagonal
        ]
        if len(sliders) >= 2:
            return {
                "attacker_coordination_type": "diagonal_battery",
                "attacker_coordination_summary": f"{lane_target['summary']} via {'+'.join(sliders[:2])}",
            }

    candidate_squares: list[chess.Square] = []
    if king_target["type"] != "no_clear_king_zone_target":
        king_square = board.king(focus_color)
        if king_square is not None:
            candidate_squares.append(king_square)
    for record in imbalance[:2]:
        if record["square"] not in candidate_squares:
            candidate_squares.append(record["square"])
    for square in zone:
        if square not in candidate_squares:
            candidate_squares.append(square)

    best_square: chess.Square | None = None
    best_attackers = 0
    best_defenders = 0
    best_type_count = 0
    for square in candidate_squares:
        attacker_types = set()
        attacker_count = 0
        for attacker_square in board.attackers(enemy_color, square):
            piece = board.piece_at(attacker_square)
            if piece is None or piece.piece_type == chess.KING:
                continue
            attacker_count += 1
            attacker_types.add(piece.piece_type)
        defender_count = len(board.attackers(focus_color, square))
        if attacker_count >= 2 and len(attacker_types) >= 2:
            if attacker_count > best_attackers or (
                attacker_count == best_attackers and len(attacker_types) > best_type_count
            ):
                best_square = square
                best_attackers = attacker_count
                best_defenders = defender_count
                best_type_count = len(attacker_types)

    if best_square is not None:
        return {
            "attacker_coordination_type": "converging_target_pressure",
            "attacker_coordination_summary": target_square_summary(best_square, best_attackers, best_defenders),
        }

    return {
        "attacker_coordination_type": "no_clear_attacker_coordination",
        "attacker_coordination_summary": "none",
    }


def defensive_network_fragility_fields(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    king_target: dict[str, str],
    imbalance: list[dict[str, Any]],
    overloaded: list[dict[str, Any]],
    defender_removal_exposure: dict[str, str],
) -> dict[str, str]:
    enemy_color = not focus_color
    overloaded_squares = {record["square"] for record in overloaded}
    candidate_squares: list[chess.Square] = []
    king_square = board.king(focus_color)
    if king_target["type"] != "no_clear_king_zone_target" and king_square is not None:
        candidate_squares.append(king_square)
    for record in imbalance[:2]:
        if record["square"] not in candidate_squares:
            candidate_squares.append(record["square"])
    for square in zone:
        attackers = len(board.attackers(enemy_color, square))
        defenders = len(board.attackers(focus_color, square))
        if attackers > 0 and defenders > 0 and square not in candidate_squares:
            candidate_squares.append(square)

    for square in candidate_squares:
        enemy_attackers = len(board.attackers(enemy_color, square))
        defenders = [
            defender_square
            for defender_square in board.attackers(focus_color, square)
            if (
                (piece := board.piece_at(defender_square)) is not None
                and piece.color == focus_color
                and piece.piece_type != chess.KING
            )
        ]
        if enemy_attackers <= 0 or not defenders:
            continue

        if (
            len(defenders) <= 2
            and defender_removal_exposure["defender_removal_exposure_type"] != "no_clear_defender_removal_exposure"
            and any(defender_square in overloaded_squares for defender_square in defenders)
        ):
            defender_tokens = "+".join(
                piece_token(board.piece_at(defender_square), defender_square)  # type: ignore[arg-type]
                for defender_square in defenders[:2]
            )
            return {
                "defensive_network_fragility_type": "collapsing_defender_cluster",
                "defensive_network_fragility_summary": f"{target_square_summary(square, enemy_attackers, len(defenders))} via {defender_tokens}",
            }

        if len(defenders) == 1:
            defender_square = defenders[0]
            defender_piece = board.piece_at(defender_square)
            if defender_piece is None:
                continue
            support_count = sum(
                1
                for support_square in board.attackers(focus_color, defender_square)
                if (
                    support_square != defender_square
                    and support_square not in defenders
                    and (support_piece := board.piece_at(support_square)) is not None
                    and support_piece.color == focus_color
                    and support_piece.piece_type != chess.KING
                )
            )
            if support_count <= 1 or defender_square in overloaded_squares:
                return {
                    "defensive_network_fragility_type": "single_chain_defense",
                    "defensive_network_fragility_summary": (
                        f"{target_square_summary(square, enemy_attackers, len(defenders))} via {piece_token(defender_piece, defender_square)}"
                    ),
                }

        if len(defenders) == 2:
            defender_tokens: list[str] = []
            mutually_thin = True
            for defender_square in defenders:
                defender_piece = board.piece_at(defender_square)
                if defender_piece is None:
                    mutually_thin = False
                    break
                defender_tokens.append(piece_token(defender_piece, defender_square))
                support_count = sum(
                    1
                    for support_square in board.attackers(focus_color, defender_square)
                    if (
                        support_square not in defenders
                        and (support_piece := board.piece_at(support_square)) is not None
                        and support_piece.color == focus_color
                        and support_piece.piece_type != chess.KING
                    )
                )
                if support_count > 1:
                    mutually_thin = False
                    break
            if mutually_thin:
                return {
                    "defensive_network_fragility_type": "mutually_dependent_defenders",
                    "defensive_network_fragility_summary": (
                        f"{target_square_summary(square, enemy_attackers, len(defenders))} via {'+'.join(defender_tokens)}"
                    ),
                }

    return {
        "defensive_network_fragility_type": "no_clear_defensive_network_fragility",
        "defensive_network_fragility_summary": "none",
    }


def structural_v4_summary(fields: dict[str, Any]) -> str:
    parts: list[str] = []
    if fields["attacker_coordination_type"] != "no_clear_attacker_coordination":
        parts.append(fields["attacker_coordination_type"])
    if fields["defensive_network_fragility_type"] != "no_clear_defensive_network_fragility":
        parts.append(fields["defensive_network_fragility_type"])
    return " | ".join(parts) if parts else "no_clear_structural_v4"


def structural_v4_confidence(fields: dict[str, Any]) -> str:
    signals = 0
    if fields["attacker_coordination_type"] != "no_clear_attacker_coordination":
        signals += 1
    if fields["defensive_network_fragility_type"] != "no_clear_defensive_network_fragility":
        signals += 1
    if signals >= 2:
        return "high"
    if signals >= 1:
        return "medium"
    return "low"


def structural_link_fields(structural_fields: dict[str, Any]) -> dict[str, str]:
    linked_king_zone_target = (
        structural_fields["king_zone_target_summary"]
        if structural_fields["king_zone_target_type"] != "no_clear_king_zone_target"
        else "none"
    )
    linked_vulnerable_piece_target = (
        structural_fields["vulnerable_piece_target_summary"]
        if structural_fields["vulnerable_piece_target_type"] != "no_clear_vulnerable_piece_target"
        else "none"
    )
    linked_pressure_lane_target = (
        structural_fields["pressure_lane_target_summary"]
        if structural_fields["pressure_lane_target_type"] != "no_clear_pressure_lane_target"
        else "none"
    )
    linked_decisive_imbalance_target = structural_fields["decisive_imbalance_target"] or "none"
    linked_pinned_critical_piece = (
        structural_fields.get("pinned_critical_piece_summary", "none")
        if structural_fields.get("pinned_critical_piece_type", "no_clear_pinned_critical_piece")
        != "no_clear_pinned_critical_piece"
        else "none"
    )
    linked_defender_removal_exposure = (
        structural_fields.get("defender_removal_exposure_summary", "none")
        if structural_fields.get("defender_removal_exposure_type", "no_clear_defender_removal_exposure")
        != "no_clear_defender_removal_exposure"
        else "none"
    )
    linked_king_color_complex = (
        structural_fields.get("king_color_complex_summary", "none")
        if structural_fields.get("king_color_complex_state", "no_clear_color_complex_weakness")
        != "no_clear_color_complex_weakness"
        else "none"
    )
    linked_target_zone_imbalance = (
        structural_fields.get("target_zone_imbalance_summary", "balanced")
        if structural_fields.get("target_zone_imbalance_type", "balanced_target_zone")
        != "balanced_target_zone"
        else "none"
    )
    linked_attacker_coordination = (
        structural_fields.get("attacker_coordination_summary", "none")
        if structural_fields.get("attacker_coordination_type", "no_clear_attacker_coordination")
        != "no_clear_attacker_coordination"
        else "none"
    )
    linked_defensive_network_fragility = (
        structural_fields.get("defensive_network_fragility_summary", "none")
        if structural_fields.get("defensive_network_fragility_type", "no_clear_defensive_network_fragility")
        != "no_clear_defensive_network_fragility"
        else "none"
    )

    parts: list[str] = []
    if linked_king_zone_target != "none":
        parts.append(f"king-zone {linked_king_zone_target}")
    if linked_vulnerable_piece_target != "none":
        parts.append(f"piece {linked_vulnerable_piece_target}")
    if linked_pressure_lane_target != "none":
        parts.append(f"lane {linked_pressure_lane_target}")
    if linked_decisive_imbalance_target != "none":
        parts.append(f"imbalance {linked_decisive_imbalance_target}")
    if linked_pinned_critical_piece != "none":
        parts.append(f"pinned {linked_pinned_critical_piece}")
    if linked_defender_removal_exposure != "none":
        parts.append(f"defender-removal {linked_defender_removal_exposure}")
    if linked_king_color_complex != "none":
        parts.append(f"color-complex {linked_king_color_complex}")
    if linked_target_zone_imbalance != "none":
        parts.append(f"target-zone {linked_target_zone_imbalance}")
    if linked_attacker_coordination != "none":
        parts.append(f"coordination {linked_attacker_coordination}")
    if linked_defensive_network_fragility != "none":
        parts.append(f"network-fragility {linked_defensive_network_fragility}")

    return {
        "format_version": "mtlv3",
        "linked_king_zone_target": linked_king_zone_target,
        "linked_vulnerable_piece_target": linked_vulnerable_piece_target,
        "linked_pressure_lane_target": linked_pressure_lane_target,
        "linked_decisive_imbalance_target": linked_decisive_imbalance_target,
        "linked_pinned_critical_piece": linked_pinned_critical_piece,
        "linked_defender_removal_exposure": linked_defender_removal_exposure,
        "linked_king_color_complex": linked_king_color_complex,
        "linked_target_zone_imbalance": linked_target_zone_imbalance,
        "linked_attacker_coordination": linked_attacker_coordination,
        "linked_defensive_network_fragility": linked_defensive_network_fragility,
        "structural_link_summary": " | ".join(parts) if parts else "no_clear_structural_link",
    }
