from __future__ import annotations

from typing import Any

import chess


PIECE_VALUE = {
    chess.PAWN: 1,
    chess.KNIGHT: 3,
    chess.BISHOP: 3,
    chess.ROOK: 5,
    chess.QUEEN: 9,
}


def color_from_name(side: str) -> chess.Color:
    return chess.WHITE if side == "white" else chess.BLACK


def piece_token(piece: chess.Piece, square: chess.Square) -> str:
    return f"{piece.symbol().lower()}@{chess.square_name(square)}"


def king_zone_squares(board: chess.Board, king_square: chess.Square) -> list[chess.Square]:
    zone = {king_square}
    zone.update(chess.SquareSet(chess.BB_KING_ATTACKS[king_square]))

    file_index = chess.square_file(king_square)
    rank_index = chess.square_rank(king_square)
    step = -1 if rank_index >= 4 else 1
    next_rank = rank_index + step
    if 0 <= next_rank <= 7:
        for file_offset in (-1, 0, 1):
            target_file = file_index + file_offset
            if 0 <= target_file <= 7:
                zone.add(chess.square(target_file, next_rank))
    return sorted(zone)


def king_escape_count(board: chess.Board, focus_color: chess.Color) -> int:
    king_square = board.king(focus_color)
    if king_square is None:
        return 0
    enemy_color = not focus_color
    count = 0
    for square in chess.SquareSet(chess.BB_KING_ATTACKS[king_square]):
        occupant = board.piece_at(square)
        if occupant is not None and occupant.color == focus_color:
            continue
        if board.is_attacked_by(enemy_color, square):
            continue
        count += 1
    return count


def pawn_shield_count(board: chess.Board, king_square: chess.Square, focus_color: chess.Color) -> int:
    file_index = chess.square_file(king_square)
    rank_index = chess.square_rank(king_square)
    step = -1 if focus_color == chess.WHITE else 1
    next_rank = rank_index + step
    if not 0 <= next_rank <= 7:
        return 0

    shield = 0
    for file_offset in (-1, 0, 1):
        target_file = file_index + file_offset
        if not 0 <= target_file <= 7:
            continue
        square = chess.square(target_file, next_rank)
        piece = board.piece_at(square)
        if piece is not None and piece.color == focus_color and piece.piece_type == chess.PAWN:
            shield += 1
    return shield


def pressure_type_from_lines(board: chess.Board, zone: list[chess.Square], enemy_color: chess.Color) -> str:
    file_hits = 0
    rank_hits = 0
    diagonal_hits = 0

    for square in zone:
        file_index = chess.square_file(square)
        rank_index = chess.square_rank(square)
        for attacker_square in board.attackers(enemy_color, square):
            attacker = board.piece_at(attacker_square)
            if attacker is None:
                continue
            if chess.square_file(attacker_square) == file_index:
                file_hits += 1
            if chess.square_rank(attacker_square) == rank_index:
                rank_hits += 1
            if abs(chess.square_file(attacker_square) - file_index) == abs(chess.square_rank(attacker_square) - rank_index):
                diagonal_hits += 1

    active = [
        ("file_pressure", file_hits),
        ("rank_pressure", rank_hits),
        ("diagonal_pressure", diagonal_hits),
    ]
    strong = [name for name, count in active if count >= 2]
    if len(strong) >= 2:
        return "multiple_line_pressure"
    if strong and any(count >= 1 for name, count in active if name not in strong):
        return "multiple_line_pressure"
    for name, count in active:
        if count >= 2:
            return name
    return "no_clear_line_pressure"


def square_pressure_type(board: chess.Board, zone: list[chess.Square], focus_color: chess.Color) -> str:
    enemy_color = not focus_color
    strongest_margin = 0
    pressured_squares = 0
    for square in zone:
        attackers = len(board.attackers(enemy_color, square))
        defenders = len(board.attackers(focus_color, square))
        margin = attackers - defenders
        if margin > 0:
            pressured_squares += 1
            strongest_margin = max(strongest_margin, margin)

    if strongest_margin >= 2:
        return "concentrated_square_pressure"
    if pressured_squares >= 1:
        return "localized_square_pressure"
    return "balanced_square_control"


def loose_piece_records(
    board: chess.Board,
    focus_color: chess.Color,
    relevant_squares: set[chess.Square],
) -> list[dict[str, Any]]:
    enemy_color = not focus_color
    records: list[dict[str, Any]] = []

    for square, piece in board.piece_map().items():
        if piece.color != focus_color or piece.piece_type == chess.KING:
            continue
        enemy_attackers = len(board.attackers(enemy_color, square))
        friendly_defenders = len(board.attackers(focus_color, square))
        if enemy_attackers > 0 and friendly_defenders == 0:
            records.append(
                {
                    "square": square,
                    "piece": piece,
                    "piece_value": PIECE_VALUE[piece.piece_type],
                    "relevant": square in relevant_squares,
                    "enemy_attackers": enemy_attackers,
                    "friendly_defenders": friendly_defenders,
                }
            )

    records.sort(
        key=lambda item: (
            0 if item["relevant"] else 1,
            -item["enemy_attackers"],
            -item["piece_value"],
            chess.square_name(item["square"]),
        )
    )
    return records


def imbalance_records(
    board: chess.Board,
    focus_color: chess.Color,
    relevant_squares: set[chess.Square],
) -> list[dict[str, Any]]:
    enemy_color = not focus_color
    records: list[dict[str, Any]] = []

    for square, piece in board.piece_map().items():
        if piece.color != focus_color or piece.piece_type == chess.KING:
            continue
        enemy_attackers = len(board.attackers(enemy_color, square))
        friendly_defenders = len(board.attackers(focus_color, square))
        if enemy_attackers <= friendly_defenders:
            continue
        records.append(
            {
                "square": square,
                "piece": piece,
                "piece_value": PIECE_VALUE[piece.piece_type],
                "relevant": square in relevant_squares,
                "enemy_attackers": enemy_attackers,
                "friendly_defenders": friendly_defenders,
                "imbalance": enemy_attackers - friendly_defenders,
            }
        )

    records.sort(
        key=lambda item: (
            0 if item["relevant"] else 1,
            -item["imbalance"],
            -item["enemy_attackers"],
            -item["piece_value"],
            chess.square_name(item["square"]),
        )
    )
    return records


def overloaded_defender_records(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    imbalance: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    zone_targets = [square for square in zone if len(board.attackers(not focus_color, square)) > 0]
    imbalance_targets = [record["square"] for record in imbalance[:2]]
    critical_targets = zone_targets + imbalance_targets
    if not critical_targets:
        return records

    for square, piece in board.piece_map().items():
        if piece.color != focus_color or piece.piece_type == chess.KING:
            continue
        duties = sum(1 for target in critical_targets if square in board.attackers(focus_color, target))
        if duties >= 2:
            records.append(
                {
                    "square": square,
                    "piece": piece,
                    "duties": duties,
                }
            )

    records.sort(
        key=lambda item: (
            -item["duties"],
            -PIECE_VALUE[item["piece"].piece_type],
            chess.square_name(item["square"]),
        )
    )
    return records


def king_exposure_fields(
    board: chess.Board,
    focus_color: chess.Color,
    king_square: chess.Square,
    line_pressure: str,
    luft_state: str,
) -> dict[str, str]:
    file_index = chess.square_file(king_square)
    rank_index = chess.square_rank(king_square)
    central_king = 2 <= file_index <= 5 and 2 <= rank_index <= 5
    shield = pawn_shield_count(board, king_square, focus_color)

    if central_king:
        return {"type": "exposed_central_king", "severity": "high"}
    if line_pressure == "file_pressure":
        return {"type": "exposed_open_file", "severity": "high" if luft_state != "safe_luft" else "medium"}
    if line_pressure == "diagonal_pressure":
        return {"type": "exposed_open_diagonal", "severity": "medium"}
    if shield <= 1:
        return {"type": "exposed_piece_shield_loss", "severity": "medium"}
    return {"type": "no_clear_exposure", "severity": "none"}


def back_rank_and_luft_state(
    board: chess.Board,
    focus_color: chess.Color,
    king_square: chess.Square,
    line_pressure: str,
) -> dict[str, str]:
    escapes = king_escape_count(board, focus_color)
    if escapes >= 2:
        luft_state = "safe_luft"
    elif escapes == 1:
        luft_state = "limited_luft"
    else:
        luft_state = "no_luft"

    rank_index = chess.square_rank(king_square)
    home_rank = 0 if focus_color == chess.WHITE else 7
    if rank_index != home_rank:
        back_rank_state = "not_back_rank_context"
    elif luft_state == "no_luft" and line_pressure in {"file_pressure", "rank_pressure", "multiple_line_pressure"}:
        back_rank_state = "back_rank_vulnerable"
    else:
        back_rank_state = "back_rank_stable"

    return {"luft_state": luft_state, "back_rank_state": back_rank_state}


def summary_from_piece_records(records: list[dict[str, Any]], *, include_counts: bool = False) -> str:
    if not records:
        return "none"
    parts = []
    for record in records[:2]:
        token = piece_token(record["piece"], record["square"])
        if include_counts:
            token += f" {record['enemy_attackers']}v{record['friendly_defenders']}"
        parts.append(token)
    return ", ".join(parts)


def overloaded_summary(records: list[dict[str, Any]]) -> str:
    if not records:
        return "none"
    return ", ".join(
        f"{piece_token(record['piece'], record['square'])} duties={record['duties']}" for record in records[:2]
    )


def structural_feature_summary(fields: dict[str, Any]) -> str:
    parts: list[str] = []
    if fields["king_exposure_type"] != "no_clear_exposure":
        parts.append(fields["king_exposure_type"])
    if fields["back_rank_state"] == "back_rank_vulnerable":
        parts.append("back_rank_vulnerable")
    elif fields["luft_state"] != "safe_luft":
        parts.append(fields["luft_state"])
    if fields["king_line_pressure_type"] != "no_clear_line_pressure":
        parts.append(fields["king_line_pressure_type"])
    if fields["king_square_pressure_type"] != "balanced_square_control":
        parts.append(fields["king_square_pressure_type"])
    if fields["loose_piece_count"] > 0:
        parts.append(f"loose {fields['loose_piece_count']}")
    if fields["overloaded_defender_count"] > 0:
        parts.append(f"overloaded {fields['overloaded_defender_count']}")
    if fields["critical_piece_imbalance_summary"] != "balanced_local_targets":
        parts.append("piece_imbalance")
    return " | ".join(parts) if parts else "no_clear_structural_trigger"


def structural_feature_confidence(fields: dict[str, Any]) -> str:
    signals = 0
    if fields["king_exposure_type"] != "no_clear_exposure":
        signals += 1
    if fields["back_rank_state"] == "back_rank_vulnerable":
        signals += 1
    if fields["loose_piece_count"] > 0:
        signals += 1
    if fields["overloaded_defender_count"] > 0:
        signals += 1
    if fields["critical_piece_imbalance_summary"] != "balanced_local_targets":
        signals += 1
    if fields["king_line_pressure_type"] != "no_clear_line_pressure":
        signals += 1
    if signals >= 3:
        return "high"
    if signals >= 1:
        return "medium"
    return "low"


def target_square_summary(square: chess.Square, enemy_attackers: int, friendly_defenders: int) -> str:
    return f"{chess.square_name(square)} {enemy_attackers}v{friendly_defenders}"


def king_zone_target_fields(
    board: chess.Board,
    focus_color: chess.Color,
    zone: list[chess.Square],
    king_square: chess.Square,
) -> dict[str, str]:
    enemy_color = not focus_color
    best_type = "no_clear_king_zone_target"
    best_summary = "none"
    best_margin = 0

    for square in zone:
        enemy_attackers = len(board.attackers(enemy_color, square))
        friendly_defenders = len(board.attackers(focus_color, square))
        margin = enemy_attackers - friendly_defenders
        if margin <= 0:
            continue
        if square == king_square:
            target_type = "king_square_target"
        elif square in chess.SquareSet(chess.BB_KING_ATTACKS[king_square]):
            target_type = "adjacent_escape_target"
        else:
            target_type = "king_access_target"
        if margin > best_margin:
            best_margin = margin
            best_type = target_type
            best_summary = target_square_summary(square, enemy_attackers, friendly_defenders)

    return {"type": best_type, "summary": best_summary}


def vulnerable_piece_target_fields(loose: list[dict[str, Any]], imbalance: list[dict[str, Any]]) -> dict[str, str]:
    if loose:
        record = loose[0]
        return {
            "type": "loose_piece_target",
            "summary": (
                f"{piece_token(record['piece'], record['square'])} "
                f"{record['enemy_attackers']}v{record['friendly_defenders']}"
            ),
        }
    if imbalance:
        record = imbalance[0]
        return {
            "type": "underdefended_piece_target",
            "summary": (
                f"{piece_token(record['piece'], record['square'])} "
                f"{record['enemy_attackers']}v{record['friendly_defenders']}"
            ),
        }
    return {"type": "no_clear_vulnerable_piece_target", "summary": "none"}


def pressure_lane_target_fields(
    board: chess.Board,
    zone: list[chess.Square],
    enemy_color: chess.Color,
) -> dict[str, str]:
    best_lane: tuple[int, str, str] | None = None
    for square in zone:
        file_index = chess.square_file(square)
        rank_index = chess.square_rank(square)
        for attacker_square in board.attackers(enemy_color, square):
            attacker = board.piece_at(attacker_square)
            if attacker is None:
                continue
            summary: str | None = None
            lane_type: str | None = None
            if chess.square_file(attacker_square) == file_index:
                lane_type = "file_lane_target"
                summary = f"file {chr(ord('a') + file_index)}"
            elif chess.square_rank(attacker_square) == rank_index:
                lane_type = "rank_lane_target"
                summary = f"rank {rank_index + 1}"
            elif abs(chess.square_file(attacker_square) - file_index) == abs(chess.square_rank(attacker_square) - rank_index):
                lane_type = "diagonal_lane_target"
                summary = f"diag {chess.square_name(attacker_square)}-{chess.square_name(square)}"
            if lane_type is None or summary is None:
                continue
            span = max(
                abs(chess.square_file(attacker_square) - file_index),
                abs(chess.square_rank(attacker_square) - rank_index),
            )
            candidate = (span, lane_type, summary)
            if best_lane is None or candidate[0] > best_lane[0]:
                best_lane = candidate

    if best_lane is None:
        return {"type": "no_clear_pressure_lane_target", "summary": "none"}
    return {"type": best_lane[1], "summary": best_lane[2]}


def decisive_imbalance_target(records: list[dict[str, Any]]) -> str:
    if not records:
        return "none"
    record = records[0]
    return (
        f"{piece_token(record['piece'], record['square'])} "
        f"{record['enemy_attackers']}v{record['friendly_defenders']}"
    )


def local_target_summary(fields: dict[str, Any]) -> str:
    parts: list[str] = []
    if fields["king_zone_target_type"] != "no_clear_king_zone_target":
        parts.append(f"king-zone {fields['king_zone_target_summary']}")
    if fields["vulnerable_piece_target_type"] != "no_clear_vulnerable_piece_target":
        parts.append(f"piece {fields['vulnerable_piece_target_summary']}")
    if fields["pressure_lane_target_type"] != "no_clear_pressure_lane_target":
        parts.append(fields["pressure_lane_target_summary"])
    if fields["decisive_imbalance_target"] != "none":
        parts.append(f"imbalance {fields['decisive_imbalance_target']}")
    return " | ".join(parts) if parts else "none"


def local_target_confidence(fields: dict[str, Any]) -> str:
    signals = 0
    if fields["king_zone_target_type"] != "no_clear_king_zone_target":
        signals += 1
    if fields["vulnerable_piece_target_type"] != "no_clear_vulnerable_piece_target":
        signals += 1
    if fields["pressure_lane_target_type"] != "no_clear_pressure_lane_target":
        signals += 1
    if fields["decisive_imbalance_target"] != "none":
        signals += 1
    if signals >= 3:
        return "high"
    if signals >= 1:
        return "medium"
    return "low"
