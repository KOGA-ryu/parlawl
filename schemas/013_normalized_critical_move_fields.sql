ALTER TABLE critical_moves ADD COLUMN critical_reason_type TEXT;
ALTER TABLE critical_moves ADD COLUMN critical_reason_severity TEXT;
ALTER TABLE critical_moves ADD COLUMN critical_reason_compact_summary TEXT;
ALTER TABLE critical_moves ADD COLUMN continuation_format_version TEXT;
ALTER TABLE critical_moves ADD COLUMN best_continuation_compact TEXT;
ALTER TABLE critical_moves ADD COLUMN played_continuation_compact TEXT;
