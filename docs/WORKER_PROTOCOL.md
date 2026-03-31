# worker protocol

Transport:

- desktop launches the configured Python interpreter with `workers/analysis_py/worker.py`
- desktop writes one UTF-8 JSON request to stdin
- worker writes one UTF-8 JSON response to stdout

Request fields:

- `protocol_version`
- `request_type`
- `analysis_run`
- `engine`
- `puzzle_round`
- `source_game`

Request payload is real for this slice:

- puzzle metadata comes from live Lichess fetches
- source game metadata comes from live puzzle detail and game export fetches
- `pgn_text` is the full exported source PGN
- `engine.stockfish_path` is required for engine-backed analysis
- `engine.depth` is currently `10`
- `engine.window_before` and `engine.window_after` are currently `4`
- `engine.multipv` is currently `3`

Current live puzzle-ingestion boundary:

- orchestration currently acquires puzzle context from latest solved puzzle activity plus puzzle detail lookup
- orchestration then resolves the source game PGN from the returned source game id
- puzzle dashboard, batch puzzle retrieval, next puzzle, and daily puzzle are documented future acquisition modes only
- those deferred endpoints do not change the worker contract in this phase

Response fields:

- `protocol_version`
- `response_type`
- `analysis_run`
- `tactical_event`
- `critical_moves`

Worker responsibility in this phase:

- parse and replay the source PGN
- map the puzzle start region from `initialPly` plus `initial_fen`
- analyze a bounded tactical window around the puzzle start with Stockfish
- derive factual candidates such as opening family, game phase, tactical motif candidates, and structural candidates
- derive `king_safety_state` and `structural_candidates_json` from the retained structural and local-target packet when that newer packet is strong enough
- derive `piece_activity` from the retained local packet when that newer packet is strong enough
- emit critical move records with played move versus best move, ranked candidates, mate-aware only-move diagnostics, and objective evaluation fields
- emit `evidence_payload_json`
- derive `key_weakness` as a compact local weakness signal from the retained structural and local-target packet when those signals are strong enough

Mapping result fields returned inside `tactical_event`:

- `mapping_status`
- `mapped_start_ply`
- `mapping_method`
- `mapping_confidence`
- `mapping_notes`
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
- `king_exposure_type`
- `king_exposure_severity`
- `king_safety_state`
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
- `structural_candidates_json`
- `key_weakness`

Supported mapping methods:

- `exact_initial_ply_match`
- `exact_fen_search_match`
- `ambiguous_fen_match` as a surfaced failure class
- `mapping_failed` as a surfaced failure class

Current mapping rule:

- compare FEN identity using board, turn, castling, and en passant fields
- ignore move counters when matching the puzzle position to the PGN replay
- fail if multiple replay plies match and no strong disambiguation exists

Worker non-responsibility in this phase:

- no human behavioral explanation as claimed truth
- no assistant labels
- no assistant summary content
- desktop-side assistant export/import does not change the worker request or response contract

Ranked candidate evidence in each `critical_move` can include:

- `critical_reason_type`
- `critical_reason_severity`
- `critical_reason_compact_summary`
- `continuation_format_version`
- `best_continuation_compact`
- `played_continuation_compact`
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
- `pv_uci`
- `pv_san`
- `best_continuation_summary`
- `played_continuation_summary`
- `only_move_status`
- `only_move_margin_cp`

Failure classes surfaced by orchestration:

- fetch failure
- PGN parse failure
- engine launch failure
- engine analysis failure
- persistence failure

Score convention:

- all worker score comparisons use the mover perspective on the analysed pre-move board
- positive is better for the mover
- mate scores outrank centipawn scores
- faster mate-for-mover outranks slower mate-for-mover
- longer mate-against-mover outranks shorter mate-against-mover
- `eval_delta_cp` is null when a comparison is mate-driven rather than centipawn-only

Current only-move statuses:

- `only_move`
- `near_only_move`
- `multiple_viable`
- `forced_mate_resource`
- `forced_defense_resource`

Current adjacent-ply coherence rules:

- adjacent role conflicts are resolved inside a one-ply suppression window
- `preventative_resource` is only retained when it remains at least two plies earlier than the retained break point
- role priority inside an adjacent conflict is `decisive_blunder` > `last_holding_defense` > `missed_counterplay` > `preventative_resource`
- ties within the same role priority are resolved by stronger local continuation support, then later ply
- the worker returns compact event-level summaries describing which nearby candidates were retained and how many were omitted

Current normalized collapse-sequence rules:

- `collapse_sequence_format_version` is currently `csv1`
- `single_break` when only one retained role remains
- `preventative_then_break` when `preventative_resource` is retained distinctly earlier than the primary break
- `defense_then_break` when `last_holding_defense` is retained distinctly earlier than the primary break
- `counterplay_then_break` when `missed_counterplay` is retained distinctly before the break
- `multi_step_local_sequence` when more than two retained roles remain and no simpler normalized type fits
- `collapse_sequence_summary` is derived from normalized sequence fields rather than generated independently

Current omission-reason taxonomy:

- `adjacent_duplicate`
- `lower_priority_same_window`
- `weaker_support_same_role`
- `preventative_too_close_to_break`
- `redundant_consequence_ply`
- `omission_reason_summary` is a compact normalized summary derived from omission-reason counts

Current retained-break explanation rules:

- `primary_break_reason` is derived from the retained primary candidate role
- `retained_break_format_version` is currently `rbsv1`
- `retained_break_role`, `retained_break_played_move`, `retained_break_best_move`, and `retained_break_compact_sequence` are the structured retained-break fields returned by the worker
- `retained_break_summary` is derived from those structured retained-break fields rather than generated separately
- `best_vs_played_divergence_summary` prefers mate-appearance, only-move miss, stronger-alternative count, and then centipawn divergence wording in that order
- `local_sequence_confidence` is `high` for clean mate or only-move style retained breaks with little ambiguity, `medium` for significant but less forced local breaks, and `low` otherwise

Current retained-break summary formatting:

- `rbsv1` summary format is `<role>@<ply> | played <move> | best <move> | <compact sequence>`
- `retained_break_compact_sequence` prefers:
  - mate distinction first
  - then forced-defense or only-move distinction
  - then stronger-alternative wording
  - then normalized centipawn-collapse wording
- the worker keeps this compact sequence patterned and reusable rather than freeform prose

Current normalized divergence precedence:

- mate-related divergence outranks all centipawn-only divergence
- `forced_defense_missed` and `only_move_missed` outrank general stronger-alternative wording
- `stronger_alternatives_available` applies only when no more specific retained-break distinction exists
- `centipawn_collapse` is the centipawn-specific fallback for decisive retained breaks
- `tactical_resource_lost` is the final compact fallback when no more specific local distinction applies

Current divergence severity:

- `critical`: mate-created, mate-avoided, forced-defense misses
- `major`: only-move misses, stronger-alternatives cases with significant swing, centipawn collapse
- `moderate`: defensible-position loss and tactical-resource loss with significant local swing
- `minor`: low-swing tactical-resource loss fallback

Current critical-move reason rules:

- `why_critical` is derived from the structured critical-move reason fields rather than generated independently
- mate-related reasons outrank centipawn-only reasons
- forced-defense and only-move reasons outrank general stronger-alternative wording
- stronger-alternative wording outranks generic centipawn-collapse fallback
- preventative-hold wording applies only when the retained role and local evidence support it

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

Current continuation compact formatting:

- `continuation_format_version` is currently `cmv1`
- compact continuation summaries prefer:
  - mate distinction first
  - then forced-defense or only-move distinction
  - then stronger-line wording
  - then defensible-line wording
  - then centipawn-collapse fallback
- the worker keeps these compact continuation descriptors patterned and comparable rather than prose-like

Current compact candidate-display formatting:

- `candidate_moves_json` entries remain machine-usable structured candidate records
- each entry now also carries `candidate_display_format_version` and `candidate_display_compact`
- `candidate_display_format_version` is currently `cdv1`
- `cdv1` candidate display format is `<rank>. <move_san> | <score_display> | <tags>`
- the trailing `| <tags>` segment is omitted when there are no tags
- `score_display` uses `mate +n` or `mate -n` for mate-backed candidates and `cp +n` or `cp -n` for centipawn-backed candidates
- `candidate_display_compact` is fully derived from the structured candidate entry rather than generated independently
- current move-level structural-link format is `mtlv3`
- move-level structural links are derived references back to the retained event-level local targets and structural v2/v4 signals rather than a second structural-analysis pass

Current structural evidence v1:

- structural evidence is extracted from the retained local break position, not from a whole-game strategic pass
- focus remains on the local collapse side and its immediate king zone or tactically relevant pieces
- `king_exposure_type` currently supports `exposed_open_file`, `exposed_open_diagonal`, `exposed_piece_shield_loss`, `exposed_central_king`, and `no_clear_exposure`
- `luft_state` currently supports `safe_luft`, `limited_luft`, `no_luft`, and `unknown`
- `back_rank_state` currently supports `back_rank_vulnerable`, `back_rank_stable`, and `not_back_rank_context`
- `king_line_pressure_type` currently supports `file_pressure`, `rank_pressure`, `diagonal_pressure`, `multiple_line_pressure`, and `no_clear_line_pressure`
- `king_square_pressure_type` currently supports `concentrated_square_pressure`, `localized_square_pressure`, and `balanced_square_control`
- `loose_piece_count` counts attacked focus-side non-king pieces with zero defenders
- `overloaded_defender_count` counts conservative local cases where one defender covers at least two critical duties
- `critical_piece_imbalance_summary` is a compact attacked-versus-defended summary over tactically relevant focus-side pieces
- `structural_feature_summary` is derived from those structured structural fields rather than generated independently
- structural evidence is local board-state evidence only, not behavioral interpretation

Current structural evidence v1.1 local-target extraction:

- local-target extraction is still derived from the retained local break position, not from a whole-board importance pass
- `king_zone_target_type` can emit `king_square_target`, `adjacent_escape_target`, `king_access_target`, or `no_clear_king_zone_target`
- `vulnerable_piece_target_type` can emit `loose_piece_target`, `underdefended_piece_target`, or `no_clear_vulnerable_piece_target`
- `pressure_lane_target_type` can emit `file_lane_target`, `rank_lane_target`, `diagonal_lane_target`, or `no_clear_pressure_lane_target`
- `decisive_imbalance_target` is the strongest compact local attacked-versus-defended target, or `none`
- `local_target_summary` is derived from those structured local-target fields rather than generated independently
- local target evidence is compact board-state targeting context only, not behavioral interpretation

Current structural evidence v2:

- structural v2 is still extracted from the retained local break position, not from a whole-board strategic pass
- `pinned_critical_piece_type` can emit `pinned_overloaded_defender`, `pinned_king_defender`, `pinned_target_defender`, or `no_clear_pinned_critical_piece`
- `defender_removal_exposure_type` can emit `deflection_sensitive_defense`, `single_defender_exposure`, or `no_clear_defender_removal_exposure`
- `king_color_complex_state` can emit `weak_dark_complex`, `weak_light_complex`, or `no_clear_color_complex_weakness`
- `target_zone_imbalance_type` can emit `attacker_heavy`, `defender_fragile`, `balanced_target_zone`, or `mixed_target_zone_pressure`
- `structural_v2_summary` is derived from those structured structural v2 fields rather than generated independently
- structural evidence v2 remains local board-state evidence only, not behavioral interpretation

Current structural evidence v3:

- structural v3 is still extracted from the retained local break position, not from a whole-board strategic pass
- `escape_geometry_state` can emit `sealed_king_box`, `attacked_flight_squares`, `blocked_flight_squares`, or `no_clear_escape_geometry`
- `flight_control_type` can emit `mixed_flight_control`, `king_square_control`, `escape_square_control`, or `no_clear_flight_control`
- `defensive_escape_fragility_type` can emit `overloaded_escape_defender`, `single_escape_defender`, or `no_clear_escape_fragility`
- `structural_v3_summary` is derived from those structured structural v3 fields rather than generated independently
- structural evidence v3 remains local board-state evidence only, not behavioral interpretation

Current structural evidence v4:

- structural v4 is still extracted from the retained local break position, not from a whole-board strategic pass
- `attacker_coordination_type` can emit `file_battery`, `diagonal_battery`, `converging_target_pressure`, or `no_clear_attacker_coordination`
- `defensive_network_fragility_type` can emit `single_chain_defense`, `mutually_dependent_defenders`, `collapsing_defender_cluster`, or `no_clear_defensive_network_fragility`
- `structural_v4_summary` is derived from those structured structural v4 fields rather than generated independently
- structural evidence v4 remains local board-state evidence only, not behavioral interpretation

Current candidate-ranking normalization:

- `candidate_ranking_summary` is derived from structured ranking fields rather than generated independently
- mate-driven choice outranks cp-only ranking phrasing
- forced-defense and only-move style choice outrank general several-strong-alternatives wording
- collapse phrasing outranks close-but-inferior phrasing
- close-but-inferior applies only when the local evidence remains materially non-collapsing

Current candidate-ranking types:

- `mate_driven_choice`
- `forced_defense_choice`
- `unique_best`
- `near_unique_best`
- `several_strong_alternatives`
- `played_close_but_inferior`
- `played_collapse`

Current evidence-note taxonomy:

- `mate_allowed`
- `mate_preserved`
- `forced_defense_missed`
- `unique_best_missed`
- `stronger_alternatives_missed`
- `defensible_line_lost`
- `preventative_hold_missed`
- `collapse_triggered`
- `tactical_resource_lost`
