CREATE TABLE attempt_puzzle_records (
    puzzle_record_id TEXT PRIMARY KEY CHECK (length(puzzle_record_id) BETWEEN 1 AND 256),
    puzzle_id TEXT NOT NULL CHECK (length(puzzle_id) BETWEEN 1 AND 256),
    record_schema TEXT NOT NULL CHECK (record_schema = 'esports-probability-lab/puzzle-candidate/v1'),
    canonical_json BLOB NOT NULL CHECK (length(canonical_json) BETWEEN 2 AND 1048576),
    canonical_sha256 TEXT NOT NULL,
    retained_at_utc TEXT NOT NULL
) STRICT;

CREATE TABLE solve_attempt_instances (
    attempt_instance_id TEXT PRIMARY KEY CHECK (length(attempt_instance_id) = 64),
    instance_schema TEXT NOT NULL CHECK (instance_schema = 'parlawl/puzzle-attempt-instance/v1'),
    puzzle_id TEXT NOT NULL CHECK (length(puzzle_id) BETWEEN 1 AND 256),
    puzzle_record_id TEXT NOT NULL CHECK (length(puzzle_record_id) BETWEEN 1 AND 256),
    solver_id TEXT NOT NULL CHECK (length(solver_id) = 54),
    session_id TEXT NOT NULL CHECK (length(session_id) = 55),
    started_at_utc TEXT NOT NULL,
    puzzle_snapshot_json TEXT NOT NULL CHECK (length(CAST(puzzle_snapshot_json AS BLOB)) BETWEEN 2 AND 1048576),
    genesis_hash TEXT NOT NULL UNIQUE
) STRICT;

CREATE TABLE solve_attempt_events (
    event_id TEXT PRIMARY KEY,
    event_schema TEXT NOT NULL CHECK (event_schema = 'parlawl/puzzle-attempt-event/v1'),
    attempt_instance_id TEXT NOT NULL,
    event_index INTEGER NOT NULL CHECK (event_index >= 0),
    event_kind TEXT NOT NULL CHECK (event_kind IN (
        'attempt_started', 'move_correct', 'move_rejected', 'attempt_solved',
        'attempt_failed_wrong_move', 'hint_granted', 'solution_revealed',
        'attempt_invalidated', 'attempt_abandoned', 'solution_revealed_review',
        'retry_requested'
    )),
    occurred_at_utc TEXT NOT NULL,
    elapsed_milliseconds INTEGER NOT NULL CHECK (elapsed_milliseconds >= 0),
    payload_json TEXT NOT NULL CHECK (length(CAST(payload_json AS BLOB)) BETWEEN 2 AND 65536),
    terminal_kind TEXT NOT NULL DEFAULT '' CHECK (terminal_kind IN (
        '', 'solved', 'failed_wrong_move', 'revealed_failed', 'invalidated', 'abandoned'
    )),
    previous_hash TEXT NOT NULL,
    event_hash TEXT NOT NULL UNIQUE,
    UNIQUE (attempt_instance_id, event_index),
    CHECK (event_id = event_hash),
    CHECK (
        (event_kind = 'attempt_solved' AND terminal_kind = 'solved') OR
        (event_kind = 'attempt_failed_wrong_move' AND terminal_kind = 'failed_wrong_move') OR
        (event_kind = 'solution_revealed' AND terminal_kind = 'revealed_failed') OR
        (event_kind = 'attempt_invalidated' AND terminal_kind = 'invalidated') OR
        (event_kind = 'attempt_abandoned' AND terminal_kind = 'abandoned') OR
        (event_kind IN ('attempt_started', 'move_correct', 'move_rejected', 'hint_granted',
                        'solution_revealed_review', 'retry_requested') AND terminal_kind = '')
    )
) STRICT;

CREATE TABLE solve_attempt_terminal_records (
    attempt_id TEXT PRIMARY KEY,
    logical_attempt_id TEXT NOT NULL UNIQUE,
    attempt_instance_id TEXT NOT NULL UNIQUE,
    puzzle_record_id TEXT NOT NULL,
    record_schema TEXT NOT NULL CHECK (record_schema = 'esports-probability-lab/puzzle-attempt/v1'),
    outcome TEXT NOT NULL CHECK (outcome IN ('solved', 'failed')),
    observed_at_utc TEXT NOT NULL,
    duration_milliseconds INTEGER CHECK (duration_milliseconds >= 1),
    wrong_move_count INTEGER NOT NULL CHECK (wrong_move_count >= 0),
    hints_used INTEGER NOT NULL CHECK (hints_used >= 0),
    solution_revealed INTEGER NOT NULL CHECK (solution_revealed IN (0, 1)),
    metadata_json TEXT NOT NULL CHECK (length(CAST(metadata_json AS BLOB)) BETWEEN 2 AND 4096),
    canonical_json BLOB NOT NULL CHECK (length(canonical_json) BETWEEN 2 AND 1048576),
    canonical_sha256 TEXT NOT NULL,
    CHECK (NOT (outcome = 'solved' AND solution_revealed = 1))
) STRICT;

CREATE INDEX solve_attempt_instances_started_idx ON solve_attempt_instances(started_at_utc, attempt_instance_id);
CREATE INDEX solve_attempt_instances_puzzle_idx ON solve_attempt_instances(puzzle_id, started_at_utc);
CREATE INDEX solve_attempt_events_kind_idx ON solve_attempt_events(event_kind, occurred_at_utc);

CREATE TRIGGER attempt_puzzle_records_no_update BEFORE UPDATE ON attempt_puzzle_records BEGIN SELECT RAISE(ABORT, 'attempt puzzle records are append-only'); END;
CREATE TRIGGER attempt_puzzle_records_no_delete BEFORE DELETE ON attempt_puzzle_records BEGIN SELECT RAISE(ABORT, 'attempt puzzle records are append-only'); END;
CREATE TRIGGER attempt_puzzle_records_no_replace BEFORE INSERT ON attempt_puzzle_records WHEN EXISTS (SELECT 1 FROM attempt_puzzle_records WHERE puzzle_record_id = NEW.puzzle_record_id) BEGIN SELECT RAISE(ABORT, 'attempt puzzle record identity already exists'); END;
CREATE TRIGGER solve_attempt_instances_no_update BEFORE UPDATE ON solve_attempt_instances BEGIN SELECT RAISE(ABORT, 'solve attempt instances are append-only'); END;
CREATE TRIGGER solve_attempt_instances_no_delete BEFORE DELETE ON solve_attempt_instances BEGIN SELECT RAISE(ABORT, 'solve attempt instances are append-only'); END;
CREATE TRIGGER solve_attempt_instances_no_replace BEFORE INSERT ON solve_attempt_instances WHEN EXISTS (SELECT 1 FROM solve_attempt_instances WHERE attempt_instance_id = NEW.attempt_instance_id OR genesis_hash = NEW.genesis_hash) BEGIN SELECT RAISE(ABORT, 'solve attempt instance identity already exists'); END;
CREATE TRIGGER solve_attempt_instances_require_record BEFORE INSERT ON solve_attempt_instances WHEN NOT EXISTS (SELECT 1 FROM attempt_puzzle_records WHERE puzzle_record_id = NEW.puzzle_record_id AND puzzle_id = NEW.puzzle_id) BEGIN SELECT RAISE(ABORT, 'solve attempt puzzle record is missing or mismatched'); END;
CREATE TRIGGER solve_attempt_events_no_update BEFORE UPDATE ON solve_attempt_events BEGIN SELECT RAISE(ABORT, 'solve attempt events are append-only'); END;
CREATE TRIGGER solve_attempt_events_no_delete BEFORE DELETE ON solve_attempt_events BEGIN SELECT RAISE(ABORT, 'solve attempt events are append-only'); END;
CREATE TRIGGER solve_attempt_events_no_replace BEFORE INSERT ON solve_attempt_events WHEN EXISTS (SELECT 1 FROM solve_attempt_events WHERE event_id = NEW.event_id OR event_hash = NEW.event_hash OR (attempt_instance_id = NEW.attempt_instance_id AND event_index = NEW.event_index)) BEGIN SELECT RAISE(ABORT, 'solve attempt event identity or sequence already exists'); END;
CREATE TRIGGER solve_attempt_events_require_instance BEFORE INSERT ON solve_attempt_events WHEN NOT EXISTS (SELECT 1 FROM solve_attempt_instances WHERE attempt_instance_id = NEW.attempt_instance_id) BEGIN SELECT RAISE(ABORT, 'solve attempt event instance is missing'); END;
CREATE TRIGGER solve_attempt_events_require_sequence BEFORE INSERT ON solve_attempt_events WHEN NEW.event_index != COALESCE((SELECT MAX(event_index) + 1 FROM solve_attempt_events WHERE attempt_instance_id = NEW.attempt_instance_id), 0) BEGIN SELECT RAISE(ABORT, 'solve attempt event sequence is not contiguous'); END;
CREATE TRIGGER solve_attempt_events_require_previous_hash BEFORE INSERT ON solve_attempt_events WHEN NEW.previous_hash != COALESCE((SELECT event_hash FROM solve_attempt_events WHERE attempt_instance_id = NEW.attempt_instance_id ORDER BY event_index DESC LIMIT 1), (SELECT genesis_hash FROM solve_attempt_instances WHERE attempt_instance_id = NEW.attempt_instance_id)) BEGIN SELECT RAISE(ABORT, 'solve attempt event previous hash does not match'); END;
CREATE TRIGGER solve_attempt_events_require_start BEFORE INSERT ON solve_attempt_events WHEN (NEW.event_index = 0 AND (NEW.event_kind != 'attempt_started' OR NEW.elapsed_milliseconds != 0 OR NEW.occurred_at_utc != (SELECT started_at_utc FROM solve_attempt_instances WHERE attempt_instance_id = NEW.attempt_instance_id))) OR (NEW.event_index != 0 AND NEW.event_kind = 'attempt_started') BEGIN SELECT RAISE(ABORT, 'solve attempt event start position is invalid'); END;
CREATE TRIGGER solve_attempt_events_one_terminal BEFORE INSERT ON solve_attempt_events WHEN NEW.terminal_kind != '' AND EXISTS (SELECT 1 FROM solve_attempt_events WHERE attempt_instance_id = NEW.attempt_instance_id AND terminal_kind != '') BEGIN SELECT RAISE(ABORT, 'solve attempt already has a terminal event'); END;
CREATE TRIGGER solve_attempt_events_after_terminal BEFORE INSERT ON solve_attempt_events WHEN EXISTS (SELECT 1 FROM solve_attempt_events WHERE attempt_instance_id = NEW.attempt_instance_id AND terminal_kind != '') AND NEW.event_kind NOT IN ('solution_revealed_review', 'retry_requested') BEGIN SELECT RAISE(ABORT, 'solve attempt accepts only review events after terminal'); END;
CREATE TRIGGER solve_attempt_terminal_records_no_update BEFORE UPDATE ON solve_attempt_terminal_records BEGIN SELECT RAISE(ABORT, 'terminal solve attempt records are append-only'); END;
CREATE TRIGGER solve_attempt_terminal_records_no_delete BEFORE DELETE ON solve_attempt_terminal_records BEGIN SELECT RAISE(ABORT, 'terminal solve attempt records are append-only'); END;
CREATE TRIGGER solve_attempt_terminal_records_no_replace BEFORE INSERT ON solve_attempt_terminal_records WHEN EXISTS (SELECT 1 FROM solve_attempt_terminal_records WHERE attempt_id = NEW.attempt_id OR logical_attempt_id = NEW.logical_attempt_id OR attempt_instance_id = NEW.attempt_instance_id) BEGIN SELECT RAISE(ABORT, 'terminal solve attempt identity already exists'); END;
CREATE TRIGGER solve_attempt_terminal_records_require_source BEFORE INSERT ON solve_attempt_terminal_records WHEN NOT EXISTS (SELECT 1 FROM solve_attempt_instances AS instance JOIN attempt_puzzle_records AS record ON record.puzzle_record_id = instance.puzzle_record_id AND record.puzzle_id = instance.puzzle_id WHERE instance.attempt_instance_id = NEW.attempt_instance_id AND record.puzzle_record_id = NEW.puzzle_record_id) BEGIN SELECT RAISE(ABORT, 'terminal solve attempt source record is missing or mismatched'); END;
CREATE TRIGGER solve_attempt_terminal_records_require_terminal_event BEFORE INSERT ON solve_attempt_terminal_records WHEN NOT EXISTS (SELECT 1 FROM solve_attempt_events AS event WHERE event.attempt_instance_id = NEW.attempt_instance_id AND event.terminal_kind IN ('solved', 'failed_wrong_move', 'revealed_failed') AND event.event_index = (SELECT MAX(latest.event_index) FROM solve_attempt_events AS latest WHERE latest.attempt_instance_id = NEW.attempt_instance_id)) BEGIN SELECT RAISE(ABORT, 'latest solve attempt event is not an exportable terminal'); END;
