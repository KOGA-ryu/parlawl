ALTER TABLE critical_moves ADD COLUMN linked_attacker_coordination TEXT NOT NULL DEFAULT 'none';
ALTER TABLE critical_moves ADD COLUMN linked_defensive_network_fragility TEXT NOT NULL DEFAULT 'none';
