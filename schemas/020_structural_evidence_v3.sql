ALTER TABLE tactical_events ADD COLUMN escape_geometry_state TEXT NOT NULL DEFAULT 'no_clear_escape_geometry';
ALTER TABLE tactical_events ADD COLUMN escape_geometry_summary TEXT NOT NULL DEFAULT 'none';
ALTER TABLE tactical_events ADD COLUMN flight_control_type TEXT NOT NULL DEFAULT 'no_clear_flight_control';
ALTER TABLE tactical_events ADD COLUMN flight_control_summary TEXT NOT NULL DEFAULT 'none';
ALTER TABLE tactical_events ADD COLUMN defensive_escape_fragility_type TEXT NOT NULL DEFAULT 'no_clear_escape_fragility';
ALTER TABLE tactical_events ADD COLUMN defensive_escape_fragility_summary TEXT NOT NULL DEFAULT 'none';
ALTER TABLE tactical_events ADD COLUMN structural_v3_summary TEXT NOT NULL DEFAULT 'no_clear_structural_v3';
ALTER TABLE tactical_events ADD COLUMN structural_v3_confidence TEXT NOT NULL DEFAULT 'low';
