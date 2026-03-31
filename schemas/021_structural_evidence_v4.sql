ALTER TABLE tactical_events ADD COLUMN attacker_coordination_type TEXT NOT NULL DEFAULT 'no_clear_attacker_coordination';
ALTER TABLE tactical_events ADD COLUMN attacker_coordination_summary TEXT NOT NULL DEFAULT 'none';
ALTER TABLE tactical_events ADD COLUMN defensive_network_fragility_type TEXT NOT NULL DEFAULT 'no_clear_defensive_network_fragility';
ALTER TABLE tactical_events ADD COLUMN defensive_network_fragility_summary TEXT NOT NULL DEFAULT 'none';
ALTER TABLE tactical_events ADD COLUMN structural_v4_summary TEXT NOT NULL DEFAULT 'no_clear_structural_v4';
ALTER TABLE tactical_events ADD COLUMN structural_v4_confidence TEXT NOT NULL DEFAULT 'low';
