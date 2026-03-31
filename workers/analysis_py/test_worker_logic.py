import sys
import unittest
from pathlib import Path

import chess

PROJECT_ROOT = Path(__file__).resolve().parents[2]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from workers.analysis_py.worker import (  # noqa: E402
    candidate_is_materially_stronger,
    candidate_display_fields,
    candidate_ranking_fields,
    collapse_sequence_fields,
    cp_gap,
    derive_king_safety_state,
    derive_piece_activity,
    attacker_coordination_fields,
    defensive_escape_fragility_fields,
    defensive_network_fragility_fields,
    extract_structural_evidence,
    continuation_compact_fields,
    critical_reason_fields,
    defender_removal_exposure_fields,
    detect_critical_moves,
    evidence_note_fields,
    divergence_summary,
    derive_key_weakness,
    format_collapse_sequence_summary,
    format_omission_reason_summary,
    format_retained_break_summary,
    legacy_key_weakness_fallback,
    legacy_piece_activity_fallback,
    king_zone_target_fields,
    omission_reason_counts,
    only_move_diagnostics,
    overloaded_defender_records,
    pinned_critical_piece_fields,
    pressure_type_from_lines,
    pressure_lane_target_fields,
    primary_break_reason,
    retained_break_compact_sequence,
    retained_break_fields,
    retained_break_summary,
    square_pressure_type,
    score_gap,
    snapshot_from_fields,
    structural_feature_summary,
    king_zone_squares,
    imbalance_records,
    vulnerable_piece_target_fields,
    decisive_imbalance_target,
    local_target_summary,
    local_target_confidence,
    escape_geometry_fields,
    flight_control_fields,
    structural_candidates_from_fields,
    structural_link_fields,
    structural_v2_summary,
    structural_v3_summary,
    structural_v4_summary,
    legacy_king_safety_fallback,
    structural_v3_confidence,
    structural_v4_confidence,
    target_zone_imbalance_fields,
)


class TestWorkerLogic(unittest.TestCase):
    @staticmethod
    def make_move(
        ply: int,
        side: str,
        *,
        eval_delta_cp: int | None,
        decisive_swing: str = "limited",
        only_move_status: str = "multiple_viable",
        eval_after_best_cp: int | None = 0,
        best_mate_flag: str = "none",
        stronger_alternative_count: int = 1,
    ) -> dict:
        return {
            "ply": ply,
            "side": side,
            "played_move": f"m{ply}",
            "best_move": f"b{ply}",
            "eval_before_cp": 0,
            "eval_after_played_cp": -100 if eval_delta_cp is not None else None,
            "eval_after_best_cp": eval_after_best_cp,
            "eval_delta_cp": eval_delta_cp,
            "decisive_swing": decisive_swing,
            "analysis_depth": 10,
            "mate_flag": "none" if decisive_swing != "mate_swing" else "mate_against_mover_in_1",
            "before_mate_flag": "none",
            "played_mate_flag": "none",
            "best_mate_flag": best_mate_flag,
            "pv_uci": "",
            "pv_san": "",
            "best_continuation_summary": f"best@{ply}",
            "played_continuation_summary": f"played@{ply}",
            "only_move_status": only_move_status,
            "only_move_margin_cp": eval_delta_cp,
            "only_move_reasoning": f"reason@{ply}",
            "candidate_moves_json": "[]",
            "candidate_ranking_summary": f"rank@{ply}",
            "stronger_alternative_count": stronger_alternative_count,
        }

    def test_cp_score_normalization_uses_mover_perspective(self) -> None:
        best = snapshot_from_fields(140, "none")
        played = snapshot_from_fields(-25, "none")

        self.assertEqual(cp_gap(best, played), 165)
        self.assertEqual(score_gap(best, played), 165)

    def test_mate_score_normalization_outranks_centipawns(self) -> None:
        mating = snapshot_from_fields(None, "mate_for_mover_in_2")
        centipawns = snapshot_from_fields(400, "none")

        self.assertGreater(score_gap(mating, centipawns), 0)

    def test_only_move_diagnostics_handles_mixed_mate_candidates(self) -> None:
        status, margin, reasoning = only_move_diagnostics(
            [
                {"cp": None, "mate_flag": "mate_for_mover_in_1"},
                {"cp": 280, "mate_flag": "none"},
                {"cp": 180, "mate_flag": "none"},
            ]
        )

        self.assertEqual(status, "forced_mate_resource")
        self.assertIsNone(margin)
        self.assertIn("forced mate", reasoning)

    def test_only_move_diagnostics_detects_forced_defense_resource(self) -> None:
        status, margin, reasoning = only_move_diagnostics(
            [
                {"cp": -35, "mate_flag": "none"},
                {"cp": None, "mate_flag": "mate_against_mover_in_1"},
                {"cp": None, "mate_flag": "mate_against_mover_in_2"},
            ]
        )

        self.assertEqual(status, "forced_defense_resource")
        self.assertIsNone(margin)
        self.assertIn("avoids a forced mate", reasoning)

    def test_candidate_strength_uses_mate_precedence(self) -> None:
        candidate = {"eval_cp": None, "mate_flag": "mate_for_mover_in_0"}
        played = {"eval_cp": 250, "mate_flag": "none"}

        self.assertTrue(candidate_is_materially_stronger(candidate, played))

    def test_candidate_display_fields_normalize_cp_candidate(self) -> None:
        display = candidate_display_fields(
            {
                "rank": 1,
                "move_uci": "h7h6",
                "move_san": "h6",
                "score_kind": "cp",
                "eval_cp": -32,
                "mate_distance": None,
                "is_best_move": True,
                "is_played_move": False,
            }
        )

        self.assertEqual(display["format_version"], "cdv1")
        self.assertEqual(display["compact"], "1. h6 | cp -32 | best")

    def test_candidate_display_fields_normalize_mate_candidate(self) -> None:
        display = candidate_display_fields(
            {
                "rank": 2,
                "move_uci": "g7g2",
                "move_san": "Qg2#",
                "score_kind": "mate",
                "eval_cp": None,
                "mate_distance": 1,
                "is_best_move": False,
                "is_played_move": True,
            }
        )

        self.assertEqual(display["compact"], "2. Qg2# | mate +1 | played")

    def test_adjacent_decisive_candidates_keep_only_stronger_break(self) -> None:
        moves = [
            self.make_move(10, "white", eval_delta_cp=180, decisive_swing="significant", stronger_alternative_count=2),
            self.make_move(11, "white", eval_delta_cp=340, decisive_swing="decisive", stronger_alternative_count=3),
        ]

        critical_moves, event_summary = detect_critical_moves(moves, "white", 11)

        self.assertEqual(len(critical_moves), 1)
        self.assertEqual(critical_moves[0]["role"], "decisive_blunder")
        self.assertEqual(critical_moves[0]["ply"], 11)
        self.assertEqual(event_summary["primary_break_ply"], 11)
        self.assertEqual(event_summary["omitted_adjacent_candidate_count"], 0)

    def test_preventative_and_decisive_can_both_be_retained(self) -> None:
        moves = [
            self.make_move(8, "white", eval_delta_cp=150, eval_after_best_cp=40, stronger_alternative_count=2),
            self.make_move(10, "white", eval_delta_cp=360, decisive_swing="decisive", stronger_alternative_count=1),
        ]

        critical_moves, event_summary = detect_critical_moves(moves, "white", 10)
        roles = [move["role"] for move in critical_moves]

        self.assertIn("preventative_resource", roles)
        self.assertIn("decisive_blunder", roles)
        self.assertEqual(event_summary["primary_break_ply"], 10)

    def test_adjacent_last_holding_beats_nearby_missed_counterplay(self) -> None:
        moves = [
            self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20, stronger_alternative_count=1),
            self.make_move(13, "white", eval_delta_cp=190, only_move_status="multiple_viable", eval_after_best_cp=80, stronger_alternative_count=2),
        ]

        critical_moves, event_summary = detect_critical_moves(moves, "white", 13)
        roles = [move["role"] for move in critical_moves]

        self.assertIn("last_holding_defense", roles)
        self.assertNotIn("missed_counterplay", roles)
        self.assertEqual(event_summary["omitted_adjacent_candidate_count"], 1)

    def test_distinct_last_holding_and_missed_counterplay_can_both_survive(self) -> None:
        moves = [
            self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20, stronger_alternative_count=1),
            self.make_move(14, "white", eval_delta_cp=190, only_move_status="multiple_viable", eval_after_best_cp=80, stronger_alternative_count=2),
        ]

        critical_moves, event_summary = detect_critical_moves(moves, "white", 14)
        roles = [move["role"] for move in critical_moves]

        self.assertIn("last_holding_defense", roles)
        self.assertIn("missed_counterplay", roles)
        self.assertEqual(event_summary["omitted_adjacent_candidate_count"], 0)

    def test_retained_break_explanation_matches_primary_candidate(self) -> None:
        moves = [
            self.make_move(8, "white", eval_delta_cp=150, eval_after_best_cp=40, stronger_alternative_count=2),
            self.make_move(10, "white", eval_delta_cp=360, decisive_swing="decisive", stronger_alternative_count=1),
        ]

        critical_moves, event_summary = detect_critical_moves(moves, "white", 10)

        self.assertEqual(event_summary["primary_break_ply"], 10)
        self.assertEqual(event_summary["primary_break_reason"], "collapse_trigger")
        self.assertEqual(
            event_summary["retained_break_summary"],
            "decisive_blunder@10 | played m10 | best b10 | 360 cp collapse",
        )
        self.assertIn("360 cp", event_summary["best_vs_played_divergence_summary"])
        self.assertEqual(event_summary["divergence_type"], "centipawn_collapse")
        self.assertEqual(event_summary["divergence_severity"], "major")
        self.assertEqual(event_summary["local_sequence_confidence"], "high")
        self.assertEqual(critical_moves[-1]["role"], "decisive_blunder")

    def test_suppressed_adjacent_candidates_do_not_override_retained_explanation(self) -> None:
        moves = [
            self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20, stronger_alternative_count=1),
            self.make_move(13, "white", eval_delta_cp=190, only_move_status="multiple_viable", eval_after_best_cp=80, stronger_alternative_count=2),
        ]

        _, event_summary = detect_critical_moves(moves, "white", 13)

        self.assertEqual(event_summary["primary_break_reason"], "narrow_missed_defense")
        self.assertEqual(
            event_summary["retained_break_summary"],
            "last_holding_defense@12 | played m12 | best b12 | forced defense missed",
        )
        self.assertNotIn("@13", event_summary["retained_break_summary"])

    def test_divergence_summary_reflects_only_move_status(self) -> None:
        candidate = {
            "role": "last_holding_defense",
            "move": self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20),
        }

        self.assertEqual(primary_break_reason(candidate), "narrow_missed_defense")
        divergence = divergence_summary(candidate)
        self.assertEqual(divergence["type"], "forced_defense_missed")
        self.assertEqual(divergence["severity"], "critical")
        self.assertIn("only defensive hold", divergence["summary"])
        self.assertEqual(
            retained_break_summary(candidate),
            "last_holding_defense@12 | played m12 | best b12 | forced defense missed",
        )

    def test_retained_break_summary_is_derived_from_structured_fields(self) -> None:
        candidate = {
            "role": "last_holding_defense",
            "move": self.make_move(18, "black", eval_delta_cp=119, stronger_alternative_count=2),
        }
        fields = retained_break_fields(candidate)

        self.assertEqual(fields["format_version"], "rbsv1")
        self.assertEqual(fields["role"], "last_holding_defense")
        self.assertEqual(fields["played_move"], "m18")
        self.assertEqual(fields["best_move"], "b18")
        self.assertEqual(fields["compact_sequence"], "stronger alternatives available")
        self.assertEqual(
            format_retained_break_summary(fields, 18),
            "last_holding_defense@18 | played m18 | best b18 | stronger alternatives available",
        )

    def test_mate_based_retained_break_sequence_normalization(self) -> None:
        candidate = {
            "role": "decisive_blunder",
            "move": self.make_move(20, "white", eval_delta_cp=None, decisive_swing="mate_swing"),
        }
        self.assertEqual(retained_break_compact_sequence(divergence_summary(candidate)), "mate appears")

    def test_only_move_retained_break_sequence_normalization(self) -> None:
        candidate = {
            "role": "last_holding_defense",
            "move": self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20),
        }
        self.assertEqual(retained_break_compact_sequence(divergence_summary(candidate)), "forced defense missed")

    def test_mate_created_divergence_normalization(self) -> None:
        candidate = {
            "role": "decisive_blunder",
            "move": self.make_move(18, "black", eval_delta_cp=None, decisive_swing="mate_swing"),
        }
        divergence = divergence_summary(candidate)
        self.assertEqual(divergence["type"], "mate_created")
        self.assertEqual(divergence["severity"], "critical")

    def test_continuation_compact_fields_are_stable_for_mate_and_cp_cases(self) -> None:
        mate_fields = continuation_compact_fields(
            {"type": "mating_line_allowed", "compact_summary": "mate allowed"}
        )
        cp_fields = continuation_compact_fields(
            {"type": "eval_collapse_trigger", "compact_summary": "328 cp collapse"}
        )

        self.assertEqual(mate_fields["format_version"], "cmv1")
        self.assertEqual(mate_fields["best"], "mate avoided")
        self.assertEqual(mate_fields["played"], "mate appears")
        self.assertEqual(cp_fields["best"], "cp collapse avoided")
        self.assertEqual(cp_fields["played"], "328 cp collapse")

    def test_continuation_compact_fields_match_when_played_and_best_move_are_identical(self) -> None:
        move = {
            "played_move": "h4h3",
            "best_move": "h4h3",
        }
        mate_fields = continuation_compact_fields(
            {"type": "mating_line_allowed", "compact_summary": "mate allowed"},
            move,
        )
        self.assertEqual(mate_fields["best"], "mate appears")
        self.assertEqual(mate_fields["played"], "mate appears")

    def test_structural_king_exposure_detection(self) -> None:
        board = chess.Board("6k1/8/8/8/8/8/8/6R1 b - - 0 1")
        fields = extract_structural_evidence(board, "black", "g8h8", "g8h8")

        self.assertEqual(fields["king_exposure_type"], "exposed_open_file")
        self.assertEqual(fields["king_exposure_severity"], "medium")

    def test_structural_loose_piece_detection(self) -> None:
        board = chess.Board("6k1/8/8/4n3/8/8/1B6/6K1 b - - 0 1")
        fields = extract_structural_evidence(board, "black", "e5g4", "e5c4")

        self.assertEqual(fields["loose_piece_count"], 1)
        self.assertIn("n@e5", fields["loose_piece_summary"])

    def test_overloaded_defender_detection_on_conservative_case(self) -> None:
        board = chess.Board("6k1/8/5n2/3p3Q/4P3/2N5/8/6K1 b - - 0 1")
        zone = king_zone_squares(board, board.king(chess.BLACK))
        imbalance = imbalance_records(board, chess.BLACK, {chess.D5})
        overloaded = overloaded_defender_records(board, chess.BLACK, zone, imbalance)

        self.assertEqual(len(overloaded), 1)
        self.assertEqual(overloaded[0]["duties"], 2)
        self.assertEqual(chess.square_name(overloaded[0]["square"]), "f6")

    def test_back_rank_and_luft_normalization(self) -> None:
        board = chess.Board("6k1/6pp/8/8/8/8/6PP/6RK b - - 0 1")
        fields = extract_structural_evidence(board, "black", "g8h8", "g8h8")

        self.assertEqual(fields["luft_state"], "safe_luft")
        self.assertEqual(fields["back_rank_state"], "back_rank_stable")

    def test_line_and_square_pressure_normalization(self) -> None:
        board = chess.Board("6k1/8/8/7Q/8/8/8/6K1 b - - 0 1")
        zone = king_zone_squares(board, board.king(chess.BLACK))

        self.assertEqual(pressure_type_from_lines(board, zone, chess.WHITE), "multiple_line_pressure")
        self.assertEqual(square_pressure_type(board, zone, chess.BLACK), "balanced_square_control")

    def test_structural_summary_is_derived_from_structured_fields(self) -> None:
        summary = structural_feature_summary(
            {
                "king_exposure_type": "exposed_open_file",
                "back_rank_state": "back_rank_vulnerable",
                "luft_state": "no_luft",
                "king_line_pressure_type": "file_pressure",
                "king_square_pressure_type": "balanced_square_control",
                "loose_piece_count": 1,
                "overloaded_defender_count": 1,
                "critical_piece_imbalance_summary": "n@f6 2v1",
            }
        )

        self.assertEqual(
            summary,
            "exposed_open_file | back_rank_vulnerable | file_pressure | loose 1 | overloaded 1 | piece_imbalance",
        )

    def test_king_zone_target_selection_on_local_position(self) -> None:
        board = chess.Board("6k1/6pp/8/8/2B3Q1/8/8/6K1 b - - 0 1")
        zone = king_zone_squares(board, board.king(chess.BLACK))
        fields = king_zone_target_fields(board, chess.BLACK, zone, board.king(chess.BLACK))

        self.assertEqual(fields["type"], "king_square_target")
        self.assertEqual(fields["summary"], "g8 1v0")

    def test_vulnerable_piece_target_selection_prefers_loose_piece(self) -> None:
        board = chess.Board("6k1/8/8/4n3/8/8/1B6/6K1 b - - 0 1")
        loose = extract_structural_evidence(board, "black", "e5g4", "e5c4")
        imbalance = imbalance_records(board, chess.BLACK, {chess.E5})

        target = vulnerable_piece_target_fields(
            [{"square": chess.E5, "piece": chess.Piece(chess.KNIGHT, chess.BLACK), "enemy_attackers": 1, "friendly_defenders": 0}],
            imbalance,
        )

        self.assertEqual(target["type"], "loose_piece_target")
        self.assertEqual(target["summary"], "n@e5 1v0")

    def test_pressure_lane_target_normalization(self) -> None:
        board = chess.Board("6k1/5ppp/8/8/8/1B6/8/6K1 b - - 0 1")
        zone = king_zone_squares(board, board.king(chess.BLACK))
        target = pressure_lane_target_fields(board, zone, chess.WHITE)

        self.assertEqual(target["type"], "diagonal_lane_target")
        self.assertEqual(target["summary"], "diag b3-f7")

    def test_decisive_imbalance_target_selection_is_conservative(self) -> None:
        board = chess.Board("6k1/8/8/4n3/8/8/1B6/6K1 b - - 0 1")
        records = imbalance_records(board, chess.BLACK, {chess.E5})

        self.assertEqual(decisive_imbalance_target(records), "n@e5 1v0")
        self.assertEqual(decisive_imbalance_target([]), "none")

    def test_local_target_summary_is_derived_from_structured_fields(self) -> None:
        fields = {
            "king_zone_target_type": "adjacent_escape_target",
            "king_zone_target_summary": "g7 1v0",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "vulnerable_piece_target_summary": "n@e5 2v1",
            "pressure_lane_target_type": "diagonal_lane_target",
            "pressure_lane_target_summary": "diag a7-g1",
            "decisive_imbalance_target": "n@e5 2v1",
        }

        self.assertEqual(
            local_target_summary(fields),
            "king-zone g7 1v0 | piece n@e5 2v1 | diag a7-g1 | imbalance n@e5 2v1",
        )
        self.assertEqual(local_target_confidence(fields), "high")

    def test_structural_link_fields_are_derived_from_structural_targets(self) -> None:
        fields = {
            "king_zone_target_type": "king_square_target",
            "king_zone_target_summary": "g8 3v1",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "vulnerable_piece_target_summary": "p@d5 2v1",
            "pressure_lane_target_type": "file_lane_target",
            "pressure_lane_target_summary": "file g",
            "decisive_imbalance_target": "p@d5 2v1",
        }

        linked = structural_link_fields(fields)
        self.assertEqual(linked["format_version"], "mtlv3")
        self.assertEqual(linked["linked_king_zone_target"], "g8 3v1")
        self.assertEqual(linked["linked_vulnerable_piece_target"], "p@d5 2v1")
        self.assertEqual(linked["linked_pressure_lane_target"], "file g")
        self.assertEqual(linked["linked_decisive_imbalance_target"], "p@d5 2v1")
        self.assertEqual(linked["linked_pinned_critical_piece"], "none")
        self.assertEqual(linked["linked_defender_removal_exposure"], "none")
        self.assertEqual(linked["linked_king_color_complex"], "none")
        self.assertEqual(linked["linked_target_zone_imbalance"], "none")
        self.assertEqual(
            linked["structural_link_summary"],
            "king-zone g8 3v1 | piece p@d5 2v1 | lane file g | imbalance p@d5 2v1",
        )

    def test_structural_link_fields_include_structural_v2_targets(self) -> None:
        fields = {
            "king_zone_target_type": "king_square_target",
            "king_zone_target_summary": "g8 3v1",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "vulnerable_piece_target_summary": "p@d5 2v1",
            "pressure_lane_target_type": "file_lane_target",
            "pressure_lane_target_summary": "file g",
            "decisive_imbalance_target": "p@d5 2v1",
            "pinned_critical_piece_type": "pinned_overloaded_defender",
            "pinned_critical_piece_summary": "n@f6",
            "defender_removal_exposure_type": "deflection_sensitive_defense",
            "defender_removal_exposure_summary": "n@f6 -> g8 3v1",
            "king_color_complex_state": "weak_dark_complex",
            "king_color_complex_summary": "dark 2 weak squares",
            "target_zone_imbalance_type": "attacker_heavy",
            "target_zone_imbalance_summary": "g8 3v1",
        }

        linked = structural_link_fields(fields)
        self.assertEqual(linked["format_version"], "mtlv3")
        self.assertEqual(linked["linked_pinned_critical_piece"], "n@f6")
        self.assertEqual(linked["linked_defender_removal_exposure"], "n@f6 -> g8 3v1")
        self.assertEqual(linked["linked_king_color_complex"], "dark 2 weak squares")
        self.assertEqual(linked["linked_target_zone_imbalance"], "g8 3v1")
        self.assertEqual(
            linked["structural_link_summary"],
            "king-zone g8 3v1 | piece p@d5 2v1 | lane file g | imbalance p@d5 2v1 | "
            "pinned n@f6 | defender-removal n@f6 -> g8 3v1 | color-complex dark 2 weak squares | target-zone g8 3v1",
        )

    def test_structural_link_fields_include_structural_v4_targets(self) -> None:
        fields = {
            "king_zone_target_type": "king_square_target",
            "king_zone_target_summary": "g8 3v1",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "vulnerable_piece_target_summary": "p@d5 2v1",
            "pressure_lane_target_type": "file_lane_target",
            "pressure_lane_target_summary": "file g",
            "decisive_imbalance_target": "p@d5 2v1",
            "attacker_coordination_type": "file_battery",
            "attacker_coordination_summary": "file g via q@g3+r@g1",
            "defensive_network_fragility_type": "collapsing_defender_cluster",
            "defensive_network_fragility_summary": "g8 2v1 via n@f6",
        }

        linked = structural_link_fields(fields)
        self.assertEqual(linked["format_version"], "mtlv3")
        self.assertEqual(linked["linked_attacker_coordination"], "file g via q@g3+r@g1")
        self.assertEqual(linked["linked_defensive_network_fragility"], "g8 2v1 via n@f6")
        self.assertEqual(
            linked["structural_link_summary"],
            "king-zone g8 3v1 | piece p@d5 2v1 | lane file g | imbalance p@d5 2v1 | "
            "coordination file g via q@g3+r@g1 | network-fragility g8 2v1 via n@f6",
        )

    def test_pinned_critical_piece_detection_on_conservative_local_example(self) -> None:
        board = chess.Board("6k1/5npp/8/8/2B3Q1/8/8/6K1 b - - 0 1")
        zone = king_zone_squares(board, board.king(chess.BLACK))
        fields = pinned_critical_piece_fields(
            board,
            chess.BLACK,
            zone,
            [],
            [{"square": chess.F7, "piece": chess.Piece(chess.KNIGHT, chess.BLACK), "duties": 2}],
        )

        self.assertEqual(fields["pinned_critical_piece_type"], "pinned_overloaded_defender")
        self.assertEqual(fields["pinned_critical_piece_summary"], "n@f7")

    def test_defender_removal_exposure_detection_on_conservative_local_example(self) -> None:
        overloaded = [
            {
                "square": chess.F6,
                "piece": chess.Piece(chess.KNIGHT, chess.BLACK),
                "duties": 2,
            }
        ]
        fields = defender_removal_exposure_fields(
            overloaded,
            {"type": "king_square_target", "summary": "g8 3v1"},
            {"type": "no_clear_vulnerable_piece_target", "summary": "none"},
            "none",
        )

        self.assertEqual(fields["defender_removal_exposure_type"], "deflection_sensitive_defense")
        self.assertEqual(fields["defender_removal_exposure_summary"], "n@f6 -> g8 3v1")

    def test_weak_color_complex_normalization_near_the_king(self) -> None:
        board = chess.Board("4R1k1/5ppp/8/7Q/2B5/8/8/6K1 b - - 0 1")
        fields = extract_structural_evidence(board, "black", "g8h8", "g8h8")

        self.assertEqual(fields["king_color_complex_state"], "weak_dark_complex")
        self.assertIn("dark", fields["king_color_complex_summary"])

    def test_target_zone_imbalance_normalization(self) -> None:
        board = chess.Board("6k1/6pp/8/8/2B3Q1/8/8/6K1 b - - 0 1")
        zone = king_zone_squares(board, board.king(chess.BLACK))
        fields = target_zone_imbalance_fields(
            board,
            chess.BLACK,
            zone,
            {"type": "king_square_target", "summary": "g8 1v0"},
        )

        self.assertEqual(fields["target_zone_imbalance_type"], "defender_fragile")
        self.assertEqual(fields["target_zone_imbalance_summary"], "g8 1v0")

    def test_structural_v2_summary_is_derived_from_structured_fields(self) -> None:
        fields = {
            "pinned_critical_piece_type": "pinned_overloaded_defender",
            "pinned_critical_piece_summary": "n@f6",
            "defender_removal_exposure_type": "deflection_sensitive_defense",
            "defender_removal_exposure_summary": "n@f6 -> g8 3v1",
            "king_color_complex_state": "weak_dark_complex",
            "king_color_complex_summary": "dark 2 weak squares",
            "target_zone_imbalance_type": "attacker_heavy",
            "target_zone_imbalance_summary": "g8 3v1",
        }

        self.assertEqual(
            structural_v2_summary(fields),
            "pinned n@f6 | defender removal exposure | weak_dark_complex | attacker_heavy",
        )

    def test_escape_geometry_detection_on_conservative_local_example(self) -> None:
        board = chess.Board("5bk1/6pp/8/8/2B5/8/8/6K1 b - - 0 1")
        fields = escape_geometry_fields(board, chess.BLACK, board.king(chess.BLACK))

        self.assertEqual(fields["escape_geometry_state"], "blocked_flight_squares")
        self.assertEqual(fields["escape_geometry_summary"], "escapes 1 | attacked 1 | blocked 3")

    def test_flight_control_normalization_prefers_mixed_control(self) -> None:
        board = chess.Board("6k1/6pp/5p2/6Q1/2B5/8/6PP/6K1 b - - 0 1")
        fields = flight_control_fields(
            board,
            chess.BLACK,
            board.king(chess.BLACK),
            {"type": "king_square_target", "summary": "g8 3v1"},
        )

        self.assertEqual(fields["flight_control_type"], "mixed_flight_control")
        self.assertEqual(fields["flight_control_summary"], "g8 3v1 + 1 attacked flights")

    def test_defensive_escape_fragility_detects_overloaded_escape_defender(self) -> None:
        board = chess.Board("6k1/6pp/5n2/6Q1/2B5/8/6PP/6K1 b - - 0 1")
        fields = defensive_escape_fragility_fields(
            board,
            chess.BLACK,
            board.king(chess.BLACK),
            [{"square": chess.F6, "piece": chess.Piece(chess.KNIGHT, chess.BLACK), "duties": 2}],
        )

        self.assertEqual(fields["defensive_escape_fragility_type"], "overloaded_escape_defender")
        self.assertEqual(fields["defensive_escape_fragility_summary"], "n@f6")

    def test_structural_v3_summary_is_derived_from_structured_fields(self) -> None:
        fields = {
            "escape_geometry_state": "sealed_king_box",
            "flight_control_type": "mixed_flight_control",
            "defensive_escape_fragility_type": "overloaded_escape_defender",
        }

        self.assertEqual(
            structural_v3_summary(fields),
            "sealed_king_box | mixed_flight_control | overloaded_escape_defender",
        )
        self.assertEqual(structural_v3_confidence(fields), "high")

    def test_attacker_coordination_detects_file_battery(self) -> None:
        board = chess.Board("6k1/7p/5n2/8/2B5/6Q1/8/6R1 b - - 0 1")
        focus_color = chess.BLACK
        zone = king_zone_squares(board, board.king(focus_color))
        fields = attacker_coordination_fields(
            board,
            focus_color,
            zone,
            {"type": "king_square_target", "summary": "g8 3v1"},
            {"type": "file_lane_target", "summary": "file g"},
            [],
        )

        self.assertEqual(fields["attacker_coordination_type"], "file_battery")
        self.assertEqual(fields["attacker_coordination_summary"], "file g via q@g3+r@g1")

    def test_defensive_network_fragility_detects_collapsing_cluster(self) -> None:
        board = chess.Board("6k1/7p/5n2/8/2B5/6Q1/8/6R1 b - - 0 1")
        focus_color = chess.BLACK
        zone = king_zone_squares(board, board.king(focus_color))
        overloaded = overloaded_defender_records(board, focus_color, zone, [])
        fields = defensive_network_fragility_fields(
            board,
            focus_color,
            zone,
            {"type": "king_square_target", "summary": "g8 3v1"},
            [],
            [{"square": chess.F6, "piece": chess.Piece(chess.KNIGHT, chess.BLACK), "duties": 2}],
            {
                "defender_removal_exposure_type": "deflection_sensitive_defense",
                "defender_removal_exposure_summary": "n@f6 -> g8 3v1",
            },
        )

        self.assertEqual(fields["defensive_network_fragility_type"], "collapsing_defender_cluster")
        self.assertEqual(fields["defensive_network_fragility_summary"], "g8 2v1 via n@f6")

    def test_structural_v4_summary_is_derived_from_structured_fields(self) -> None:
        fields = {
            "attacker_coordination_type": "file_battery",
            "defensive_network_fragility_type": "collapsing_defender_cluster",
        }

        self.assertEqual(
            structural_v4_summary(fields),
            "file_battery | collapsing_defender_cluster",
        )
        self.assertEqual(structural_v4_confidence(fields), "high")

    def test_key_weakness_derives_king_exposure_when_structural_packet_supports_it(self) -> None:
        fields = {
            "king_exposure_type": "exposed_open_file",
            "back_rank_state": "back_rank_stable",
            "luft_state": "limited_luft",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "pressure_lane_target_type": "file_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
            "king_zone_target_type": "king_square_target",
            "king_square_pressure_type": "localized_square_pressure",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": False,
            "defender_removal_exposure_signal": False,
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="Italian Game", best_move="h7h6"),
            "king_exposure",
        )

    def test_key_weakness_derives_back_rank_weakness_when_it_dominates(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "back_rank_vulnerable",
            "luft_state": "no_luft",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "pressure_lane_target_type": "file_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
            "king_zone_target_type": "king_square_target",
            "king_square_pressure_type": "localized_square_pressure",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": False,
            "defender_removal_exposure_signal": False,
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="unknown", best_move="h7h6"),
            "back_rank_weakness",
        )

    def test_key_weakness_derives_no_luft_when_no_luft_outweighs_lane_pressure(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "back_rank_stable",
            "luft_state": "no_luft",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "pressure_lane_target_type": "rank_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "medium",
            "structural_feature_confidence": "medium",
            "king_zone_target_type": "adjacent_escape_target",
            "king_square_pressure_type": "localized_square_pressure",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": False,
            "defender_removal_exposure_signal": False,
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="unknown", best_move="d1d8"),
            "no_luft",
        )

    def test_key_weakness_derives_vulnerable_piece_from_local_target(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "safe_luft",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "overloaded_defender_count": 0,
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "high",
            "structural_feature_confidence": "medium",
            "king_zone_target_type": "no_clear_king_zone_target",
            "king_square_pressure_type": "balanced_square_control",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": False,
            "defender_removal_exposure_signal": False,
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="unknown", best_move="a2a3"),
            "vulnerable_piece",
        )

    def test_key_weakness_derives_pressure_lane_or_decisive_imbalance_when_dominant(self) -> None:
        lane_fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "pressure_lane_target_type": "diagonal_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "medium",
            "structural_feature_confidence": "medium",
            "king_zone_target_type": "no_clear_king_zone_target",
            "king_square_pressure_type": "balanced_square_control",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": False,
            "defender_removal_exposure_signal": False,
        }
        imbalance_fields = dict(lane_fields)
        imbalance_fields["pressure_lane_target_type"] = "no_clear_pressure_lane_target"
        imbalance_fields["decisive_imbalance_target"] = "p@d5 2v1"
        imbalance_fields["local_target_confidence"] = "high"

        self.assertEqual(
            derive_key_weakness(lane_fields, opening_name="unknown", best_move="a2a3"),
            "pressure_lane",
        )
        self.assertEqual(
            derive_key_weakness(imbalance_fields, opening_name="unknown", best_move="a2a3"),
            "decisive_imbalance",
        )

    def test_key_weakness_derives_mixed_local_weakness_for_multiple_strong_local_signals(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "overloaded_defender_count": 1,
            "pressure_lane_target_type": "diagonal_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
            "king_zone_target_type": "no_clear_king_zone_target",
            "king_square_pressure_type": "balanced_square_control",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": True,
            "defender_removal_exposure_signal": True,
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="unknown", best_move="a2a3"),
            "mixed_local_weakness",
        )

    def test_key_weakness_fallback_only_triggers_when_newer_evidence_is_inconclusive(self) -> None:
        weak_fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "safe_luft",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "decisive_imbalance_target": "none",
            "local_target_confidence": "low",
            "structural_feature_confidence": "low",
            "king_zone_target_type": "no_clear_king_zone_target",
            "king_square_pressure_type": "balanced_square_control",
            "weak_color_complex_signal": False,
            "pinned_critical_piece_signal": False,
            "defender_removal_exposure_signal": False,
        }
        strong_fields = dict(weak_fields)
        strong_fields["pressure_lane_target_type"] = "file_lane_target"
        strong_fields["local_target_confidence"] = "high"

        self.assertEqual(legacy_key_weakness_fallback("unknown", "h1h7"), "king_exposure")
        self.assertEqual(
            derive_key_weakness(weak_fields, opening_name="unknown", best_move="h1h7"),
            "king_exposure",
        )
        self.assertEqual(
            derive_key_weakness(strong_fields, opening_name="unknown", best_move="h1h7"),
            "pressure_lane",
        )

    def test_key_weakness_does_not_contradict_strong_structural_fields(self) -> None:
        board = chess.Board("6k1/8/8/8/8/8/8/6R1 b - - 0 1")
        fields = extract_structural_evidence(board, "black", "g8h8", "g8h8")

        self.assertEqual(fields["king_exposure_type"], "exposed_open_file")
        self.assertEqual(
            derive_key_weakness(fields, opening_name="Italian Game", best_move="h7h6"),
            "king_exposure",
        )

    def test_king_safety_state_reflects_king_exposure_when_supported(self) -> None:
        fields = {
            "king_exposure_type": "exposed_open_file",
            "back_rank_state": "back_rank_stable",
            "luft_state": "limited_luft",
            "king_line_pressure_type": "file_pressure",
            "king_square_pressure_type": "localized_square_pressure",
            "king_zone_target_type": "king_square_target",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }

        self.assertEqual(
            derive_king_safety_state(fields, opening_name="Italian Game", headers={}),
            "king_exposure",
        )

    def test_king_safety_state_reflects_back_rank_or_no_luft_when_dominant(self) -> None:
        back_rank_fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "back_rank_vulnerable",
            "luft_state": "no_luft",
            "king_line_pressure_type": "file_pressure",
            "king_square_pressure_type": "localized_square_pressure",
            "king_zone_target_type": "king_square_target",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }
        no_luft_fields = dict(back_rank_fields)
        no_luft_fields["back_rank_state"] = "back_rank_stable"

        self.assertEqual(
            derive_king_safety_state(back_rank_fields, opening_name="unknown", headers={}),
            "back_rank_danger",
        )
        self.assertEqual(
            derive_king_safety_state(no_luft_fields, opening_name="unknown", headers={}),
            "no_luft_pressure",
        )

    def test_king_safety_state_does_not_contradict_strong_local_target_evidence(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "king_line_pressure_type": "diagonal_pressure",
            "king_square_pressure_type": "localized_square_pressure",
            "king_zone_target_type": "adjacent_escape_target",
            "local_target_confidence": "high",
            "structural_feature_confidence": "medium",
        }

        self.assertEqual(
            derive_king_safety_state(fields, opening_name="unknown", headers={}),
            "pressured_king_zone",
        )

    def test_structural_candidates_derive_from_structural_packet(self) -> None:
        fields = {
            "king_exposure_type": "exposed_open_file",
            "back_rank_state": "back_rank_vulnerable",
            "luft_state": "no_luft",
            "pressure_lane_target_type": "file_lane_target",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "overloaded_defender_count": 1,
            "decisive_imbalance_target": "p@d5 2v1",
            "king_zone_target_type": "king_square_target",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }

        self.assertEqual(
            structural_candidates_from_fields(fields, "king_exposure", opening_name="Italian Game"),
            [
                "king_exposure",
                "back_rank_weakness",
                "pressure_lane",
                "vulnerable_piece",
                "overloaded_defender",
                "decisive_imbalance",
                "king_zone_target",
            ],
        )

    def test_structural_candidates_allow_multi_tag_only_when_justified(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "pressure_lane_target_type": "diagonal_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "decisive_imbalance_target": "none",
            "king_zone_target_type": "no_clear_king_zone_target",
            "local_target_confidence": "medium",
            "structural_feature_confidence": "medium",
        }

        self.assertEqual(
            structural_candidates_from_fields(fields, "pressure_lane", opening_name="unknown"),
            ["pressure_lane"],
        )

    def test_structural_candidates_fallback_only_when_newer_evidence_is_weak(self) -> None:
        weak_fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "safe_luft",
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "decisive_imbalance_target": "none",
            "king_zone_target_type": "no_clear_king_zone_target",
            "local_target_confidence": "low",
            "structural_feature_confidence": "low",
        }
        stronger_fields = dict(weak_fields)
        stronger_fields["pressure_lane_target_type"] = "diagonal_lane_target"
        stronger_fields["local_target_confidence"] = "medium"

        self.assertEqual(
            structural_candidates_from_fields(weak_fields, "no_clear_key_weakness", opening_name="Italian Game"),
            ["open_center_development_race_candidate"],
        )
        self.assertEqual(
            structural_candidates_from_fields(stronger_fields, "pressure_lane", opening_name="Italian Game"),
            ["pressure_lane"],
        )

    def test_non_contradiction_against_key_weakness(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "pressure_lane_target_type": "diagonal_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "overloaded_defender_count": 0,
            "decisive_imbalance_target": "none",
            "king_zone_target_type": "no_clear_king_zone_target",
            "local_target_confidence": "medium",
            "structural_feature_confidence": "medium",
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="unknown", best_move="h1h7"),
            "pressure_lane",
        )
        self.assertEqual(
            structural_candidates_from_fields(fields, "pressure_lane", opening_name="unknown"),
            ["pressure_lane"],
        )
        self.assertEqual(
            derive_king_safety_state(fields, opening_name="unknown", headers={}),
            "pressured_king_zone",
        )

    def test_piece_activity_derives_active_king_zone_pressure_when_it_dominates(self) -> None:
        fields = {
            "king_zone_target_type": "king_square_target",
            "pressure_lane_target_type": "file_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "decisive_imbalance_target": "none",
            "overloaded_defender_count": 0,
            "king_line_pressure_type": "file_pressure",
            "king_square_pressure_type": "localized_square_pressure",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }

        self.assertEqual(
            derive_piece_activity(
                fields,
                critical_reason_type="eval_collapse_trigger",
                candidate_ranking_type="played_collapse",
                move_count=18,
            ),
            "active_king_zone_pressure",
        )

    def test_piece_activity_derives_active_pressure_lane_when_lane_dominates(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "king_zone_target_type": "no_clear_king_zone_target",
            "pressure_lane_target_type": "diagonal_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "decisive_imbalance_target": "none",
            "overloaded_defender_count": 0,
            "king_line_pressure_type": "diagonal_pressure",
            "king_square_pressure_type": "balanced_square_control",
            "local_target_confidence": "medium",
            "structural_feature_confidence": "medium",
        }

        self.assertEqual(
            derive_piece_activity(
                fields,
                critical_reason_type="stronger_alternative_missed",
                candidate_ranking_type="several_strong_alternatives",
                move_count=18,
            ),
            "active_pressure_lane",
        )

    def test_piece_activity_derives_active_tactical_piece_when_piece_signal_dominates(self) -> None:
        fields = {
            "king_zone_target_type": "no_clear_king_zone_target",
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "decisive_imbalance_target": "p@d5 2v1",
            "overloaded_defender_count": 0,
            "king_line_pressure_type": "no_clear_line_pressure",
            "king_square_pressure_type": "balanced_square_control",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }

        self.assertEqual(
            derive_piece_activity(
                fields,
                critical_reason_type="stronger_alternative_missed",
                candidate_ranking_type="several_strong_alternatives",
                move_count=18,
            ),
            "active_tactical_piece",
        )

    def test_piece_activity_derives_overloaded_defensive_piece_when_overload_dominates(self) -> None:
        fields = {
            "king_zone_target_type": "no_clear_king_zone_target",
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "decisive_imbalance_target": "none",
            "overloaded_defender_count": 1,
            "king_line_pressure_type": "no_clear_line_pressure",
            "king_square_pressure_type": "balanced_square_control",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }

        self.assertEqual(
            derive_piece_activity(
                fields,
                critical_reason_type="forced_defense_resource",
                candidate_ranking_type="forced_defense_choice",
                move_count=18,
            ),
            "overloaded_defensive_piece",
        )

    def test_piece_activity_derives_mixed_local_activity_when_multiple_strong_signals_coexist(self) -> None:
        fields = {
            "king_zone_target_type": "king_square_target",
            "pressure_lane_target_type": "file_lane_target",
            "vulnerable_piece_target_type": "underdefended_piece_target",
            "decisive_imbalance_target": "p@d5 2v1",
            "overloaded_defender_count": 0,
            "king_line_pressure_type": "file_pressure",
            "king_square_pressure_type": "localized_square_pressure",
            "local_target_confidence": "high",
            "structural_feature_confidence": "high",
        }

        self.assertEqual(
            derive_piece_activity(
                fields,
                critical_reason_type="eval_collapse_trigger",
                candidate_ranking_type="played_collapse",
                move_count=18,
            ),
            "mixed_local_activity",
        )

    def test_piece_activity_fallback_only_triggers_when_newer_retained_evidence_is_weak(self) -> None:
        weak_fields = {
            "king_zone_target_type": "no_clear_king_zone_target",
            "pressure_lane_target_type": "no_clear_pressure_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "decisive_imbalance_target": "none",
            "overloaded_defender_count": 0,
            "king_line_pressure_type": "no_clear_line_pressure",
            "king_square_pressure_type": "balanced_square_control",
            "local_target_confidence": "low",
            "structural_feature_confidence": "low",
        }
        strong_fields = dict(weak_fields)
        strong_fields["pressure_lane_target_type"] = "diagonal_lane_target"
        strong_fields["local_target_confidence"] = "medium"

        self.assertEqual(legacy_piece_activity_fallback(18), "constrained_piece_activity")
        self.assertEqual(
            derive_piece_activity(
                weak_fields,
                critical_reason_type="stronger_alternative_missed",
                candidate_ranking_type="several_strong_alternatives",
                move_count=18,
            ),
            "constrained_piece_activity",
        )
        self.assertEqual(
            derive_piece_activity(
                strong_fields,
                critical_reason_type="stronger_alternative_missed",
                candidate_ranking_type="several_strong_alternatives",
                move_count=18,
            ),
            "active_pressure_lane",
        )

    def test_piece_activity_does_not_contradict_strong_retained_packet(self) -> None:
        fields = {
            "king_exposure_type": "no_clear_exposure",
            "back_rank_state": "not_back_rank_context",
            "luft_state": "limited_luft",
            "king_zone_target_type": "no_clear_king_zone_target",
            "pressure_lane_target_type": "diagonal_lane_target",
            "vulnerable_piece_target_type": "no_clear_vulnerable_piece_target",
            "decisive_imbalance_target": "none",
            "overloaded_defender_count": 0,
            "king_line_pressure_type": "diagonal_pressure",
            "king_square_pressure_type": "balanced_square_control",
            "local_target_confidence": "medium",
            "structural_feature_confidence": "medium",
        }

        self.assertEqual(
            derive_key_weakness(fields, opening_name="unknown", best_move="h1h7"),
            "pressure_lane",
        )
        self.assertEqual(
            structural_candidates_from_fields(fields, "pressure_lane", opening_name="unknown"),
            ["pressure_lane"],
        )
        self.assertEqual(
            derive_piece_activity(
                fields,
                critical_reason_type="stronger_alternative_missed",
                candidate_ranking_type="several_strong_alternatives",
                move_count=18,
            ),
            "active_pressure_lane",
        )

    def test_stronger_alternatives_fallback_normalization(self) -> None:
        candidate = {
            "role": "missed_counterplay",
            "move": self.make_move(14, "white", eval_delta_cp=140, stronger_alternative_count=3),
        }
        divergence = divergence_summary(candidate)
        self.assertEqual(divergence["type"], "stronger_alternatives_available")
        self.assertEqual(divergence["severity"], "major")

    def test_centipawn_collapse_fallback_normalization(self) -> None:
        candidate = {
            "role": "decisive_blunder",
            "move": self.make_move(20, "white", eval_delta_cp=320, stronger_alternative_count=1),
        }
        divergence = divergence_summary(candidate)
        self.assertEqual(divergence["type"], "centipawn_collapse")
        self.assertEqual(divergence["severity"], "major")
        self.assertEqual(retained_break_compact_sequence(divergence), "320 cp collapse")

    def test_collapse_sequence_summary_normalization_for_single_break(self) -> None:
        retained = [{"role": "decisive_blunder", "move": self.make_move(18, "white", eval_delta_cp=320)}]
        fields = collapse_sequence_fields(18, retained, 0)

        self.assertEqual(fields["format_version"], "csv1")
        self.assertEqual(fields["type"], "single_break")
        self.assertEqual(fields["role_list"], "decisive_blunder@18")
        self.assertEqual(
            format_collapse_sequence_summary(fields),
            "single_break | break@18 | roles decisive_blunder@18 | omitted 0",
        )

    def test_collapse_sequence_summary_normalization_for_preventative_then_break(self) -> None:
        retained = [
            {"role": "preventative_resource", "move": self.make_move(8, "white", eval_delta_cp=120)},
            {"role": "decisive_blunder", "move": self.make_move(10, "white", eval_delta_cp=360, decisive_swing="decisive")},
        ]
        fields = collapse_sequence_fields(10, retained, 1)

        self.assertEqual(fields["type"], "preventative_then_break")
        self.assertEqual(
            format_collapse_sequence_summary(fields),
            "preventative_then_break | break@10 | roles preventative_resource@8, decisive_blunder@10 | omitted 1",
        )

    def test_collapse_sequence_summary_normalization_for_defense_then_break(self) -> None:
        retained = [
            {
                "role": "last_holding_defense",
                "move": self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20),
            },
            {"role": "decisive_blunder", "move": self.make_move(14, "white", eval_delta_cp=320, decisive_swing="decisive")},
        ]
        fields = collapse_sequence_fields(14, retained, 0)

        self.assertEqual(fields["type"], "defense_then_break")
        self.assertEqual(
            format_collapse_sequence_summary(fields),
            "defense_then_break | break@14 | roles last_holding_defense@12, decisive_blunder@14 | omitted 0",
        )

    def test_omission_reason_normalization_for_adjacent_duplicate_suppression(self) -> None:
        omitted = [{"role": "missed_counterplay", "ply": 13, "reason": "adjacent_duplicate"}]

        self.assertEqual(omission_reason_counts(omitted), {"adjacent_duplicate": 1})
        self.assertEqual(format_omission_reason_summary(omission_reason_counts(omitted)), "adjacent_duplicate=1")

    def test_omission_reason_normalization_for_lower_priority_same_window(self) -> None:
        omitted = [{"role": "missed_counterplay", "ply": 12, "reason": "lower_priority_same_window"}]

        self.assertEqual(omission_reason_counts(omitted), {"lower_priority_same_window": 1})
        self.assertEqual(
            format_omission_reason_summary(omission_reason_counts(omitted)),
            "lower_priority_same_window=1",
        )

    def test_reason_normalization_for_mate_related_critical_move(self) -> None:
        move = self.make_move(18, "white", eval_delta_cp=None, decisive_swing="mate_swing")
        reason = critical_reason_fields("decisive_blunder", move)

        self.assertEqual(reason["type"], "mating_line_allowed")
        self.assertEqual(reason["severity"], "critical")
        self.assertEqual(reason["compact_summary"], "mate allowed")

    def test_reason_normalization_for_only_move_forced_defense(self) -> None:
        move = self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20)
        reason = critical_reason_fields("last_holding_defense", move)

        self.assertEqual(reason["type"], "forced_defense_resource")
        self.assertEqual(reason["severity"], "critical")
        self.assertEqual(reason["compact_summary"], "forced defense missed")

    def test_reason_normalization_for_stronger_alternative_fallback(self) -> None:
        move = self.make_move(14, "white", eval_delta_cp=140, stronger_alternative_count=3)
        reason = critical_reason_fields("missed_counterplay", move)

        self.assertEqual(reason["type"], "stronger_alternative_missed")
        self.assertEqual(reason["severity"], "major")
        self.assertEqual(reason["compact_summary"], "stronger alternative missed")

    def test_reason_normalization_for_cp_collapse_fallback(self) -> None:
        move = self.make_move(20, "white", eval_delta_cp=320, stronger_alternative_count=1)
        reason = critical_reason_fields("decisive_blunder", move)

        self.assertEqual(reason["type"], "eval_collapse_trigger")
        self.assertEqual(reason["severity"], "major")
        self.assertEqual(reason["compact_summary"], "320 cp collapse")

    def test_compact_continuation_normalization_for_mate_and_non_mate_cases(self) -> None:
        mate_reason = {"type": "mating_line_allowed", "severity": "critical", "compact_summary": "mate allowed"}
        cp_reason = {"type": "stronger_alternative_missed", "severity": "major", "compact_summary": "stronger alternative missed"}

        self.assertEqual(
            continuation_compact_fields(mate_reason),
            {"format_version": "cmv1", "best": "mate avoided", "played": "mate appears"},
        )
        self.assertEqual(
            continuation_compact_fields(cp_reason),
            {"format_version": "cmv1", "best": "stronger line retained", "played": "stronger line missed"},
        )

    def test_candidate_ranking_normalization_for_mate_driven_choice(self) -> None:
        move = self.make_move(18, "white", eval_delta_cp=None, decisive_swing="mate_swing")
        ranking = candidate_ranking_fields(move)

        self.assertEqual(ranking["type"], "mate_driven_choice")
        self.assertEqual(ranking["severity"], "critical")
        self.assertEqual(ranking["compact_summary"], "mate-driven choice")

    def test_candidate_ranking_normalization_for_forced_defense_choice(self) -> None:
        move = self.make_move(12, "white", eval_delta_cp=180, only_move_status="forced_defense_resource", eval_after_best_cp=20)
        ranking = candidate_ranking_fields(move)

        self.assertEqual(ranking["type"], "forced_defense_choice")
        self.assertEqual(ranking["severity"], "critical")
        self.assertEqual(ranking["compact_summary"], "forced defense choice")

    def test_candidate_ranking_normalization_for_several_strong_alternatives(self) -> None:
        move = self.make_move(14, "white", eval_delta_cp=140, stronger_alternative_count=3)
        ranking = candidate_ranking_fields(move)

        self.assertEqual(ranking["type"], "several_strong_alternatives")
        self.assertEqual(ranking["severity"], "major")
        self.assertEqual(ranking["compact_summary"], "3 stronger alternatives")

    def test_candidate_ranking_normalization_for_played_close_but_inferior(self) -> None:
        move = self.make_move(14, "white", eval_delta_cp=80, stronger_alternative_count=1)
        ranking = candidate_ranking_fields(move)

        self.assertEqual(ranking["type"], "played_close_but_inferior")
        self.assertEqual(ranking["severity"], "minor")
        self.assertEqual(ranking["compact_summary"], "played move stayed close but inferior")

    def test_candidate_ranking_normalization_for_played_collapse(self) -> None:
        move = self.make_move(20, "white", eval_delta_cp=320, stronger_alternative_count=1)
        ranking = candidate_ranking_fields(move)

        self.assertEqual(ranking["type"], "played_collapse")
        self.assertEqual(ranking["severity"], "major")
        self.assertEqual(ranking["compact_summary"], "320 cp collapse")

    def test_evidence_note_normalization_for_supported_cases(self) -> None:
        mate_move = self.make_move(18, "white", eval_delta_cp=None, decisive_swing="mate_swing")
        mate_note = evidence_note_fields("decisive_blunder", mate_move, critical_reason_fields("decisive_blunder", mate_move))

        defensive_move = self.make_move(12, "white", eval_delta_cp=130, only_move_status="near_only_move", eval_after_best_cp=20)
        defensive_note = evidence_note_fields("last_holding_defense", defensive_move, critical_reason_fields("last_holding_defense", defensive_move))

        alt_move = self.make_move(14, "white", eval_delta_cp=140, stronger_alternative_count=3)
        alt_note = evidence_note_fields("missed_counterplay", alt_move, critical_reason_fields("missed_counterplay", alt_move))

        self.assertEqual(mate_note, {"type": "mate_allowed", "severity": "critical", "compact": "mate allowed"})
        self.assertEqual(defensive_note, {"type": "defensible_line_lost", "severity": "moderate", "compact": "defensible line lost"})
        self.assertEqual(
            alt_note,
            {"type": "stronger_alternatives_missed", "severity": "major", "compact": "3 stronger alternatives missed"},
        )


if __name__ == "__main__":
    unittest.main()
