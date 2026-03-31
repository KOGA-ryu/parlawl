ALTER TABLE tactical_events ADD COLUMN primary_break_reason TEXT;
ALTER TABLE tactical_events ADD COLUMN retained_break_summary TEXT;
ALTER TABLE tactical_events ADD COLUMN best_vs_played_divergence_summary TEXT;
ALTER TABLE tactical_events ADD COLUMN local_sequence_confidence TEXT;
