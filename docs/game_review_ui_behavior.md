# Game study workspace behavior

This document describes ParlAWL's multi-game study presentation. It changes how retained replay evidence and user-authored notes are shown; it does not produce new engine analysis or alter source evidence.

## Window model

- The player explorer remains the source for opening a local read-only game breakdown.
- Opening a game creates or selects one tab in a separate `ParlAWL · Review Hub` window and one floating board window for that game. Opening another game does not replace the first game.
- Each open game owns an independent replay position, playback state, notation expansion, board appearance, and notes document.
- Activating a floating board selects the matching Review Hub tab. Selecting a Review Hub tab restores and raises the matching board.
- Closing a floating board hides it without closing its Review Hub tab. Selecting the tab shows the board again. Closing a Review Hub tab closes that game and its board.
- `Back` in a review returns attention to the player explorer without discarding other open studies.

## Review Hub

The Review Hub contains one movable, closable tab per open game. Player names identify the tab; the tooltip retains ratings, result, and date. A tab accent and the floating board's letter and palette provide redundant identity so games are not distinguished by color alone.

Every game tab contains two reading modes:

### Visual Map

- The compact three-column notation table remains the primary navigation surface.
- Clicking White or Black notation seeks to that ply and opens one full-width coach row directly beneath its move pair. Clicking the selected notation again collapses the row; selecting another notation moves the expansion.
- Symbol-only annotations remain `◫` for the first move outside the retained opening path, `◷` for the longest server-accounted move, and `●` for a retained deep moment. Tooltips preserve their exact meanings.
- A retained deep moment can show a small alternatives map. Its clickable nodes represent only the recorded constrained line and retained alternative lines present in the joined report. Selecting a node reveals its published White score, depth, selective depth, retained node count, W/D/L when available, and PV.
- The map is explicitly labeled as retained lines rather than a complete Stockfish search tree. Node count is shown as retained search evidence, not invented probability, weight, or visit share.
- An ordinary move that was not selected for deep review expands to deterministic mechanics such as phase, legal-move count, forcedness, and server-accounted time. Its shallow values are not exposed as an unlabeled score.

### Detailed Evidence

The worded view uses a fixed plain-text section order so every selected move scans the same way:

1. Chess-piece icon, move number, and SAN.
2. Played SAN and UCI.
3. Evidence status and published expectation change when available.
4. One deterministic explanation.
5. Phase, legal-move count, and forcedness.
6. Server-accounted clock facts.
7. Retained alternative summaries.
8. Source and claim boundary.

The view uses fixed-scale text and preserves scrolling without zooming. It remains copyable and does not replace SAN with icons.

## Evidence vocabulary and honesty

The evidence vocabulary remains fixed:

| Retained state | Visible status |
| --- | --- |
| `confirmed_severe_error` | `deep confirmed severe` |
| `confirmed_missed_opportunity` | `confirmed missed opportunity` |
| `ambiguous_engine_instability` | `ambiguous` |
| `incomplete_deep_evidence` | `ambiguous` |
| `below_confirmation_threshold` | `below threshold` |
| Deep report exists, but the move has no selected deep moment | `not selected for deep review` |
| No deep report, but persisted shallow evidence exists | `shallow screening candidate` |

An unknown deep source status fails closed as `evidence unavailable`; its raw retained status remains in technical details. Non-selection is never converted into Accurate, Best, Brilliant, Perfect, or engine approval. Ambiguous evidence never receives a fabricated numeric change.

## Floating board windows

- A board window contains `Board` and `Notes` tabs.
- `Previous`, `Play/Pause`, `Next`, and wheel scrubbing affect only that board's game session.
- The board header contains a stable letter identity plus Board and Pieces selectors.
- Board palettes are Walnut, Graphite, Sage, and Tournament. Piece treatments are Classic, Outlined, and Monochrome.
- Appearance is presentation-only and never changes notation, positions, or evidence.

## Notes and AI handoff

- The Notes tab is freely editable local text and is visibly labeled as user-authored material, not retained game evidence.
- `Pin current move` inserts a move-number/SAN anchor such as `[19. Nxa7]` at the cursor.
- Notes and per-game appearance are stored locally under a key derived from the exact source game ID.
- `Copy study context` copies the game header, selected move, and user notes to the clipboard for deliberate discussion elsewhere. It performs no upload, network call, or automatic AI submission.

## Focus Read

`Focus Read` remains optional inside a retained deep-moment coach row. It uses one emphasized anchor word, up to four preceding and following words, and a current/total counter. Left moves backward, Right or Space advances, and Escape closes it.

## Boundaries

The workspace adds no engine, network calls, producer-source replay, statistics, scoring, avatars, XP, hearts, streaks, confetti, or praise animation. It does not infer intent or causality. Free-analysis branches and a complete clickable engine search tree require separate retained data and are not simulated by this presentation.
