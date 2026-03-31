ALTER TABLE tactical_events ADD COLUMN primary_break_ply INTEGER;
ALTER TABLE tactical_events ADD COLUMN collapse_sequence_summary TEXT;
ALTER TABLE tactical_events ADD COLUMN retained_role_summary TEXT;
ALTER TABLE tactical_events ADD COLUMN omitted_adjacent_candidate_count INTEGER NOT NULL DEFAULT 0;
