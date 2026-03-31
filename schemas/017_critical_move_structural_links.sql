ALTER TABLE critical_moves ADD COLUMN structural_link_format_version TEXT;
ALTER TABLE critical_moves ADD COLUMN linked_king_zone_target TEXT;
ALTER TABLE critical_moves ADD COLUMN linked_vulnerable_piece_target TEXT;
ALTER TABLE critical_moves ADD COLUMN linked_pressure_lane_target TEXT;
ALTER TABLE critical_moves ADD COLUMN linked_decisive_imbalance_target TEXT;
ALTER TABLE critical_moves ADD COLUMN structural_link_summary TEXT;
