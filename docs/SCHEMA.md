# schema

Schema version `0.1` remains the first vertical-slice schema, with an explicit evidence versus assistant boundary.

## evidence owned by parlawl

### puzzle_round

- `puzzle_id`
- `puzzle_rating`
- `time_control`
- `white_player`
- `white_rating`
- `black_player`
- `black_rating`
- `side_to_move`
- `fetched_at_utc`
- `source_game_id`
- `initial_fen`
- `last_move`
- `solved`
- `raw_puzzle_json`
- `raw_activity_json`
- `solution_moves_json`
- `themes_json`

### source_game

- `source_game_id`
- `pgn_text`
- `opening_name`
- `fetched_at_utc`

### analysis_run

- `run_id`
- `puzzle_id`
- `status`
- `engine_mode`
- `engine_name`
- `engine_depth`
- `created_at_utc`
- `completed_at_utc`
- `error_message`

### tactical_event

- `event_id`
- `run_id`
- `puzzle_id`
- `source_game_id`
- `opening_family`
- `game_phase`
- `solution_summary`
- `side_to_move`
- `material_balance`
- `king_safety_state`
- `piece_activity`
- `key_weakness`
- `king_exposure_type`
- `king_exposure_severity`
- `loose_piece_count`
- `loose_piece_summary`
- `overloaded_defender_count`
- `overloaded_defender_summary`
- `back_rank_state`
- `luft_state`
- `king_line_pressure_type`
- `king_square_pressure_type`
- `critical_piece_imbalance_summary`
- `structural_feature_summary`
- `structural_feature_confidence`
- `king_zone_target_type`
- `king_zone_target_summary`
- `vulnerable_piece_target_type`
- `vulnerable_piece_target_summary`
- `pressure_lane_target_type`
- `pressure_lane_target_summary`
- `decisive_imbalance_target`
- `pinned_critical_piece_type`
- `pinned_critical_piece_summary`
- `defender_removal_exposure_type`
- `defender_removal_exposure_summary`
- `king_color_complex_state`
- `king_color_complex_summary`
- `target_zone_imbalance_type`
- `target_zone_imbalance_summary`
- `structural_v2_summary`
- `structural_v2_confidence`
- `escape_geometry_state`
- `escape_geometry_summary`
- `flight_control_type`
- `flight_control_summary`
- `defensive_escape_fragility_type`
- `defensive_escape_fragility_summary`
- `structural_v3_summary`
- `structural_v3_confidence`
- `attacker_coordination_type`
- `attacker_coordination_summary`
- `defensive_network_fragility_type`
- `defensive_network_fragility_summary`
- `structural_v4_summary`
- `structural_v4_confidence`
- `local_target_summary`
- `local_target_confidence`
- `trigger_event`
- `player_rating_range`
- `mapping_status`
- `mapped_start_ply`
- `mapping_method`
- `mapping_confidence`
- `mapping_notes`
- `analysis_window_start_ply`
- `analysis_window_end_ply`
- `primary_break_ply`
- `primary_break_reason`
- `retained_break_format_version`
- `retained_break_role`
- `retained_break_played_move`
- `retained_break_best_move`
- `retained_break_compact_sequence`
- `retained_break_summary`
- `best_vs_played_divergence_summary`
- `divergence_type`
- `divergence_severity`
- `divergence_compact_summary`
- `divergence_evidence_notes`
- `local_sequence_confidence`
- `collapse_sequence_format_version`
- `collapse_sequence_type`
- `collapse_sequence_summary`
- `retained_role_summary`
- `omitted_adjacent_candidate_count`
- `omission_reason_summary`
- `omission_reason_counts_json`
- `engine_limit_summary`
- `tactical_candidates_json`
- `structural_candidates_json`
- `evidence_payload_json`

### critical_move

- `critical_move_id`
- `event_id`
- `role`
- `ply`
- `side`
- `played_move`
- `best_move`
- `why_critical`
- `eval_before_cp`
- `eval_after_played_cp`
- `eval_after_best_cp`
- `eval_delta_cp`
- `decisive_swing`
- `analysis_depth`
- `mate_flag`
- `critical_reason_type`
- `critical_reason_severity`
- `critical_reason_compact_summary`
- `continuation_format_version`
- `best_continuation_compact`
- `played_continuation_compact`
- `pv_uci`
- `pv_san`
- `best_continuation_summary`
- `played_continuation_summary`
- `only_move_status`
- `only_move_margin_cp`
- `only_move_reasoning`
- `candidate_moves_json`
- `candidate_ranking_type`
- `candidate_ranking_severity`
- `candidate_ranking_compact_summary`
- `candidate_ranking_summary`
- `evidence_note_type`
- `evidence_note_severity`
- `evidence_note_compact`
- `structural_link_format_version`
- `linked_king_zone_target`
- `linked_vulnerable_piece_target`
- `linked_pressure_lane_target`
- `linked_decisive_imbalance_target`
- `linked_pinned_critical_piece`
- `linked_defender_removal_exposure`
- `linked_king_color_complex`
- `linked_target_zone_imbalance`
- `linked_attacker_coordination`
- `linked_defensive_network_fragility`
- `structural_link_summary`
- `stronger_alternative_count`

Current `candidate_moves_json` entries are compact local engine records with:

- `move_uci`
- `move_san`
- `eval_cp`
- `mate_flag`
- `mate_distance`
- `score_kind`
- `rank`
- `is_played_move`
- `is_best_move`
- `candidate_display_format_version`
- `candidate_display_compact`

Persistence note:

- `tactical_event` and `critical_move` storage now uses centralized insert specs and named row mappers in `libs/storage/`
- schema growth should update one explicit field list per entity rather than separate SQL column and bind-order chains

`key_weakness` semantics:

- `key_weakness` remains the same persisted field
- it is now a compact derived local weakness signal, not a legacy opening-only hint
- derivation prefers retained structural and local-target evidence, and only falls back when that newer packet is inconclusive
- current normalized states are:
  - `king_exposure`
  - `back_rank_weakness`
  - `no_luft`
  - `vulnerable_piece`
  - `overloaded_defender`
  - `pressure_lane`
  - `decisive_imbalance`
  - `mixed_local_weakness`
  - `no_clear_key_weakness`

`king_safety_state` semantics:

- `king_safety_state` remains the same persisted field
- it is now a compact derived king-zone safety descriptor sourced from retained structural and local-target evidence when possible
- current normalized states are:
  - `king_exposure`
  - `back_rank_danger`
  - `no_luft_pressure`
  - `pressured_king_zone`
  - `stable_king`
- legacy heuristic king-safety fallback is only used when both structural and local-target confidence are low

`structural_candidates_json` semantics:

- `structural_candidates_json` remains the same persisted field
- it is now derived from retained structural and local-target evidence when possible rather than from older opening-only parallel heuristics
- current compact tags can include:
  - `king_exposure`
  - `back_rank_weakness`
  - `no_luft`
  - `pressure_lane`
  - `vulnerable_piece`
  - `overloaded_defender`
  - `decisive_imbalance`
  - `king_zone_target`
- legacy structural-candidate fallback only runs when both structural and local-target confidence are low

`piece_activity` semantics:

- `piece_activity` remains the same persisted field
- it is now a compact derived local activity descriptor sourced from the retained local packet when possible
- current normalized states are:
  - `active_king_zone_pressure`
  - `active_pressure_lane`
  - `active_tactical_piece`
  - `overloaded_defensive_piece`
  - `constrained_piece_activity`
  - `mixed_local_activity`
  - `no_clear_piece_activity`
- legacy piece-activity fallback only runs when both structural and local-target confidence are low

## assistant-owned placeholders

Stored but not produced by Parlawl in this phase:

- `assistant_inference_status`
- `assistant_labels_json`
- `assistant_summary_markdown`

Desktop integration note:

- Parlawl can now export a saved run as a ChatGPT-ready markdown packet
- Parlawl can now import a downloaded external assistant-inference artifact and store it back into these existing fields
- worker extraction still does not populate these fields

Migration files:

- [001_init.sql](/Users/kogaryu/dev/parlawl/schemas/001_init.sql)
- [002_lock_schema_v0_1.sql](/Users/kogaryu/dev/parlawl/schemas/002_lock_schema_v0_1.sql)
- [003_evidence_assistant_boundary.sql](/Users/kogaryu/dev/parlawl/schemas/003_evidence_assistant_boundary.sql)
- [004_engine_evidence_slice.sql](/Users/kogaryu/dev/parlawl/schemas/004_engine_evidence_slice.sql)
- [005_mapping_and_continuation_evidence.sql](/Users/kogaryu/dev/parlawl/schemas/005_mapping_and_continuation_evidence.sql)
- [006_ranked_candidate_evidence.sql](/Users/kogaryu/dev/parlawl/schemas/006_ranked_candidate_evidence.sql)
- [007_mate_aware_only_move_diagnostics.sql](/Users/kogaryu/dev/parlawl/schemas/007_mate_aware_only_move_diagnostics.sql)
- [008_adjacent_ply_coherence_summary.sql](/Users/kogaryu/dev/parlawl/schemas/008_adjacent_ply_coherence_summary.sql)
- [009_retained_break_explanation.sql](/Users/kogaryu/dev/parlawl/schemas/009_retained_break_explanation.sql)
- [010_normalized_divergence_summary.sql](/Users/kogaryu/dev/parlawl/schemas/010_normalized_divergence_summary.sql)
- [011_retained_break_format_fields.sql](/Users/kogaryu/dev/parlawl/schemas/011_retained_break_format_fields.sql)
- [012_normalized_collapse_sequence_fields.sql](/Users/kogaryu/dev/parlawl/schemas/012_normalized_collapse_sequence_fields.sql)
- [013_normalized_critical_move_fields.sql](/Users/kogaryu/dev/parlawl/schemas/013_normalized_critical_move_fields.sql)
- [014_normalized_candidate_ranking_fields.sql](/Users/kogaryu/dev/parlawl/schemas/014_normalized_candidate_ranking_fields.sql)
- [015_structural_evidence_v1.sql](/Users/kogaryu/dev/parlawl/schemas/015_structural_evidence_v1.sql)
- [016_local_target_selection_v1_1.sql](/Users/kogaryu/dev/parlawl/schemas/016_local_target_selection_v1_1.sql)
- [017_critical_move_structural_links.sql](/Users/kogaryu/dev/parlawl/schemas/017_critical_move_structural_links.sql)
- [018_structural_evidence_v2.sql](/Users/kogaryu/dev/parlawl/schemas/018_structural_evidence_v2.sql)
- [019_critical_move_structural_v2_links.sql](/Users/kogaryu/dev/parlawl/schemas/019_critical_move_structural_v2_links.sql)
- [020_structural_evidence_v3.sql](/Users/kogaryu/dev/parlawl/schemas/020_structural_evidence_v3.sql)
- [021_structural_evidence_v4.sql](/Users/kogaryu/dev/parlawl/schemas/021_structural_evidence_v4.sql)
- [022_critical_move_structural_v4_links.sql](/Users/kogaryu/dev/parlawl/schemas/022_critical_move_structural_v4_links.sql)

Score semantics:

- all persisted engine comparisons are normalized from the mover perspective on the analysed pre-move board
- positive values are better for the mover
- `eval_delta_cp` is only stored when both compared post-move positions are centipawn scores
- mate-driven comparisons keep `eval_delta_cp = null` and use `mate_flag`, candidate `score_kind`, and `only_move_status` instead

Current only-move status set:

- `only_move`
- `near_only_move`
- `multiple_viable`
- `forced_mate_resource`
- `forced_defense_resource`

Current event-level coherence summary:

- `primary_break_ply` is the retained local break point for the event
- `primary_break_reason` is a compact extraction label such as `collapse_trigger`, `narrow_missed_defense`, `failed_preventative_hold`, or `missed_active_resource`
- `retained_break_format_version` identifies the normalized retained-break summary format and is currently `rbsv1`
- `retained_break_role`, `retained_break_played_move`, and `retained_break_best_move` store the compact structured parts of the retained break point
- `retained_break_compact_sequence` stores the shortest normalized best-versus-played distinction for the retained break point
- `retained_break_summary` is derived from the structured retained-break fields rather than generated independently
- `best_vs_played_divergence_summary` is the compact divergence description for the retained break point
- `divergence_type`, `divergence_severity`, `divergence_compact_summary`, and `divergence_evidence_notes` are normalized retained-break divergence fields derived from the same local evidence
- `local_sequence_confidence` is a compact evidence-quality label for the retained local sequence
- `collapse_sequence_format_version` identifies the normalized collapse-sequence summary format and is currently `csv1`
- `collapse_sequence_type` stores the normalized local-window sequence class for the retained event
- `collapse_sequence_summary` is derived from normalized sequence fields rather than generated independently
- `retained_role_summary` is a compact role@ply list for the roles the worker kept
- `omitted_adjacent_candidate_count` counts nearby candidates suppressed by the adjacent-ply conflict rules
- `omission_reason_summary` is a normalized compact summary of omitted adjacent candidate reasons
- `omission_reason_counts_json` stores omission-reason counts as compact JSON

Current structural evidence v1:

- `king_exposure_type` is one of `exposed_open_file`, `exposed_open_diagonal`, `exposed_piece_shield_loss`, `exposed_central_king`, or `no_clear_exposure`
- `king_exposure_severity` is currently `high`, `medium`, or `none`
- `loose_piece_count` counts focus-side non-king pieces that are attacked and have zero defenders in the retained local break position
- `loose_piece_summary` is a compact list such as `n@e5, p@d5`
- `overloaded_defender_count` counts conservative overload cases where one focus-side defender covers at least two critical duties in the retained local break position
- `overloaded_defender_summary` is a compact list such as `n@f6 duties=2`
- `luft_state` is one of `safe_luft`, `limited_luft`, `no_luft`, or `unknown`
- `back_rank_state` is one of `back_rank_vulnerable`, `back_rank_stable`, or `not_back_rank_context`
- `king_line_pressure_type` is one of `file_pressure`, `rank_pressure`, `diagonal_pressure`, `multiple_line_pressure`, or `no_clear_line_pressure`
- `king_square_pressure_type` is one of `concentrated_square_pressure`, `localized_square_pressure`, or `balanced_square_control`
- `critical_piece_imbalance_summary` is a compact attacked-versus-defended local summary such as `p@d5 2v1`
- `structural_feature_summary` is derived from those structured structural fields rather than generated independently
- `structural_feature_confidence` is currently `high`, `medium`, or `low`
- these structural fields describe local board-state evidence around the retained break point, not behavioral explanation

Current structural evidence v1.1 local-target fields:

- `king_zone_target_type` is one of `king_square_target`, `adjacent_escape_target`, `king_access_target`, or `no_clear_king_zone_target`
- `king_zone_target_summary` is a compact square-plus-pressure summary such as `g8 1v0`
- `vulnerable_piece_target_type` is one of `loose_piece_target`, `underdefended_piece_target`, or `no_clear_vulnerable_piece_target`
- `vulnerable_piece_target_summary` is a compact piece-plus-pressure summary such as `n@e5 2v1`
- `pressure_lane_target_type` is one of `file_lane_target`, `rank_lane_target`, `diagonal_lane_target`, or `no_clear_pressure_lane_target`
- `pressure_lane_target_summary` is a compact lane summary such as `file g` or `diag b3-f7`
- `decisive_imbalance_target` is the single strongest compact attacked-versus-defended local target such as `p@d5 2v1`, or `none` when no target stands out
- `local_target_summary` is derived from the structured local-target fields rather than generated independently
- `local_target_confidence` is currently `high`, `medium`, or `low`
- these local-target fields sharpen the retained-break structural packet with compact targeting context rather than broadening into full-board evaluation

Current structural evidence v2:

- `pinned_critical_piece_type` is one of `pinned_overloaded_defender`, `pinned_king_defender`, `pinned_target_defender`, or `no_clear_pinned_critical_piece`
- `pinned_critical_piece_summary` is a compact piece reference such as `n@f6`
- `defender_removal_exposure_type` is one of `deflection_sensitive_defense`, `single_defender_exposure`, or `no_clear_defender_removal_exposure`
- `defender_removal_exposure_summary` is a compact dependency summary such as `n@f6 -> g8 3v1`
- `king_color_complex_state` is one of `weak_dark_complex`, `weak_light_complex`, or `no_clear_color_complex_weakness`
- `king_color_complex_summary` is a compact count summary such as `dark 2 weak squares`
- `target_zone_imbalance_type` is one of `attacker_heavy`, `defender_fragile`, `balanced_target_zone`, or `mixed_target_zone_pressure`
- `target_zone_imbalance_summary` is a compact target-zone summary such as `g8 3v1`
- `structural_v2_summary` is derived from those structured structural v2 fields rather than generated independently
- `structural_v2_confidence` is currently `high`, `medium`, or `low`
- these structural v2 fields remain local board-state evidence around the retained break position, not behavioral explanation

Current structural evidence v3:

- `escape_geometry_state` is one of `sealed_king_box`, `attacked_flight_squares`, `blocked_flight_squares`, or `no_clear_escape_geometry`
- `escape_geometry_summary` is a compact local escape count summary such as `escapes 0 | attacked 3 | blocked 2`
- `flight_control_type` is one of `mixed_flight_control`, `king_square_control`, `escape_square_control`, or `no_clear_flight_control`
- `flight_control_summary` is a compact flight-control summary such as `g8 3v1 + 3 attacked flights`
- `defensive_escape_fragility_type` is one of `overloaded_escape_defender`, `single_escape_defender`, or `no_clear_escape_fragility`
- `defensive_escape_fragility_summary` is a compact piece or square summary such as `n@f6` or `g7 2v1`
- `structural_v3_summary` is derived from those structured structural v3 fields rather than generated independently
- `structural_v3_confidence` is currently `high`, `medium`, or `low`
- these structural v3 fields remain local board-state evidence around the retained break position, not behavioral explanation

Current structural evidence v4:

- `attacker_coordination_type` is one of `file_battery`, `diagonal_battery`, `converging_target_pressure`, or `no_clear_attacker_coordination`
- `attacker_coordination_summary` is a compact local lane or target summary such as `file g via q@g3+r@g1`
- `defensive_network_fragility_type` is one of `single_chain_defense`, `mutually_dependent_defenders`, `collapsing_defender_cluster`, or `no_clear_defensive_network_fragility`
- `defensive_network_fragility_summary` is a compact local defense summary such as `g8 2v1 via n@f6`
- `structural_v4_summary` is derived from those structured structural v4 fields rather than generated independently
- `structural_v4_confidence` is currently `high`, `medium`, or `low`
- these structural v4 fields remain local board-state evidence around the retained break position, not behavioral explanation

Current normalized divergence types:

- `mate_created`
- `mate_avoided`
- `forced_defense_missed`
- `only_move_missed`
- `stronger_alternatives_available`
- `centipawn_collapse`
- `tactical_resource_lost`
- `defensible_position_lost`

Current collapse-sequence summary format:

- `collapse_sequence_summary` currently uses `csv1`
- `csv1` format is `<sequence_type> | break@<ply> | roles <role_list> | omitted <n>`
- `collapse_sequence_summary` is derived from:
  - `collapse_sequence_format_version`
  - `collapse_sequence_type`
  - `primary_break_ply`
  - `retained_role_summary`
  - `omitted_adjacent_candidate_count`

Current collapse-sequence types:

- `single_break`
- `preventative_then_break`
- `defense_then_break`
- `counterplay_then_break`
- `multi_step_local_sequence`

Current omission-reason taxonomy:

- `adjacent_duplicate`
- `lower_priority_same_window`
- `weaker_support_same_role`
- `preventative_too_close_to_break`
- `redundant_consequence_ply`

Current retained-break summary format:

- `retained_break_summary` currently uses `rbsv1`
- `rbsv1` format is `<role>@<ply> | played <move> | best <move> | <compact sequence>`
- `retained_break_compact_sequence` prefers:
  - mate distinction first
  - then forced-defense or only-move distinction
  - then stronger-alternative wording
  - then normalized centipawn-collapse wording
- current compact sequence values are normalized extraction descriptors such as:
  - `mate appears`
  - `mate preserved`
  - `forced defense missed`
  - `only move missed`
  - `stronger alternatives available`
  - `<n> cp collapse`
- `tactical resource lost`
- `defensible line lost`

Current normalized critical-move fields:

- `critical_reason_type`, `critical_reason_severity`, and `critical_reason_compact_summary` are the structured move-level reason fields
- `continuation_format_version` is currently `cmv1`
- `best_continuation_compact` and `played_continuation_compact` are compact normalized continuation descriptors
- `cmv1` prefers mate wording, then forced-defense or only-move wording, then stronger-line wording, then defensible-line wording, then centipawn-collapse fallback
- `why_critical` is now derived from the structured critical-reason fields rather than generated independently
- `candidate_ranking_type`, `candidate_ranking_severity`, and `candidate_ranking_compact_summary` are the structured ranked-alternative summary fields
- `candidate_ranking_summary` is now derived from those structured ranking fields rather than generated independently
- `evidence_note_type`, `evidence_note_severity`, and `evidence_note_compact` are compact move-level evidence-note fields derived from the same local engine evidence
- `candidate_display_format_version` is currently `cdv1`
- `candidate_display_compact` is a derived inspection field stored inside each `candidate_moves_json` entry
- `cdv1` candidate display format is `<rank>. <move_san> | <score_display> | <tags>` with the trailing tag segment omitted when empty
- `structural_link_format_version` is currently `mtlv3`
- `linked_king_zone_target`, `linked_vulnerable_piece_target`, `linked_pressure_lane_target`, and `linked_decisive_imbalance_target` are compact move-level references back to the retained event-level local structural targets
- `linked_pinned_critical_piece`, `linked_defender_removal_exposure`, `linked_king_color_complex`, and `linked_target_zone_imbalance` are compact move-level references back to retained structural v2 facts when those signals are present
- `structural_link_summary` is derived from those compact move-level target references rather than generated independently

Current critical-reason types:

- `eval_collapse_trigger`
- `mating_line_allowed`
- `mating_line_preserved`
- `only_move_resource`
- `forced_defense_resource`
- `stronger_alternative_missed`
- `defensible_line_lost`
- `tactical_resource_lost`
- `preventative_hold_available`

Current continuation compact rules:

- prefer mate distinction first:
  - `mate appears`
  - `mate avoided`
  - `mate preserved`
- then forced-defense or only-move wording:
  - `forced defense held`
  - `forced defense missed`
  - `only move held`
  - `only move missed`
- then stronger-line wording:
  - `stronger line retained`
  - `stronger line missed`
- then defensible-line wording:
  - `defensible line kept`
  - `defensible line lost`
- then cp-collapse fallback:
  - `cp collapse avoided`
  - `<n> cp collapse`

Current candidate-ranking types:

- `mate_driven_choice`
- `forced_defense_choice`
- `unique_best`
- `near_unique_best`
- `several_strong_alternatives`
- `played_close_but_inferior`
- `played_collapse`

Current evidence-note types:

- `mate_allowed`
- `mate_preserved`
- `forced_defense_missed`
- `unique_best_missed`
- `stronger_alternatives_missed`
- `defensible_line_lost`
- `preventative_hold_missed`
- `collapse_triggered`
- `tactical_resource_lost`
