DROP TABLE IF EXISTS critical_moves;
DROP TABLE IF EXISTS tactical_events;
DROP TABLE IF EXISTS analysis_runs;
DROP TABLE IF EXISTS source_games;
DROP TABLE IF EXISTS puzzle_rounds;
DROP TABLE IF EXISTS miss_reason_hypotheses;

CREATE TABLE IF NOT EXISTS puzzle_rounds (
    puzzle_id TEXT PRIMARY KEY,
    puzzle_rating INTEGER NOT NULL,
    time_control TEXT,
    white_player TEXT,
    white_rating INTEGER,
    black_player TEXT,
    black_rating INTEGER,
    side_to_move TEXT NOT NULL,
    source_game_id TEXT NOT NULL,
    fetched_at_utc TEXT NOT NULL,
    initial_fen TEXT,
    last_move TEXT,
    solved INTEGER NOT NULL DEFAULT 0,
    raw_puzzle_json TEXT,
    raw_activity_json TEXT,
    solution_moves_json TEXT,
    themes_json TEXT
);

CREATE TABLE IF NOT EXISTS source_games (
    source_game_id TEXT PRIMARY KEY,
    pgn_text TEXT NOT NULL,
    opening_name TEXT,
    fetched_at_utc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS analysis_runs (
    run_id TEXT PRIMARY KEY,
    puzzle_id TEXT NOT NULL,
    status TEXT NOT NULL,
    engine_mode TEXT NOT NULL,
    created_at_utc TEXT NOT NULL,
    completed_at_utc TEXT,
    error_message TEXT,
    FOREIGN KEY (puzzle_id) REFERENCES puzzle_rounds(puzzle_id)
);

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
    eval_delta_cp TEXT,
    decisive_swing TEXT,
    FOREIGN KEY (event_id) REFERENCES tactical_events(event_id)
);
