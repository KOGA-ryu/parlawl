DROP TABLE IF EXISTS critical_moves;
DROP TABLE IF EXISTS tactical_events;

ALTER TABLE analysis_runs ADD COLUMN engine_name TEXT;
ALTER TABLE analysis_runs ADD COLUMN engine_depth INTEGER;

CREATE TABLE IF NOT EXISTS tactical_events (
    event_id TEXT PRIMARY KEY,
    run_id TEXT NOT NULL,
    puzzle_id TEXT NOT NULL,
    source_game_id TEXT NOT NULL,
    opening_family TEXT,
    game_phase TEXT,
    solution_summary TEXT,
    side_to_move TEXT,
    material_balance TEXT,
    king_safety_state TEXT,
    piece_activity TEXT,
    key_weakness TEXT,
    trigger_event TEXT,
    player_rating_range TEXT,
    analysis_window_start_ply INTEGER,
    analysis_window_end_ply INTEGER,
    tactical_candidates_json TEXT,
    structural_candidates_json TEXT,
    evidence_payload_json TEXT,
    assistant_inference_status TEXT NOT NULL DEFAULT 'pending',
    assistant_labels_json TEXT,
    assistant_summary_markdown TEXT,
    FOREIGN KEY (run_id) REFERENCES analysis_runs(run_id),
    FOREIGN KEY (puzzle_id) REFERENCES puzzle_rounds(puzzle_id),
    FOREIGN KEY (source_game_id) REFERENCES source_games(source_game_id)
);

CREATE TABLE IF NOT EXISTS critical_moves (
    critical_move_id TEXT PRIMARY KEY,
    event_id TEXT NOT NULL,
    role TEXT NOT NULL,
    ply INTEGER NOT NULL,
    side TEXT,
    played_move TEXT,
    best_move TEXT,
    why_critical TEXT,
    eval_before_cp INTEGER,
    eval_after_played_cp INTEGER,
    eval_after_best_cp INTEGER,
    eval_delta_cp INTEGER,
    decisive_swing TEXT,
    analysis_depth INTEGER,
    mate_flag TEXT,
    FOREIGN KEY (event_id) REFERENCES tactical_events(event_id)
);
