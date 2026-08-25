# Game-study UI housekeeping ledger

This ledger records the desktop deficiencies observed on the real Hikaru launch on 2026-08-25. The launch used the local Explorer, retained Report-v2 directory, and `chess-game-review-display-v1` sidecars. It is a presentation and window-lifecycle audit; it does not authorize new analysis, evidence, or source behavior.

## Intended window model

- The player Explorer is a library/source picker.
- The Review window is the single notation and evidence host.
- Every open game has one independently floating board window.
- Explorer and study are workspace states. They should not be competing visible hosts.
- Switching workspaces must preserve open games, notes, appearance, and selected positions while pausing hidden playback.

## Deficiencies and remedies

| ID | Deficiency observed | User impact | Remedy | Disposition |
| --- | --- | --- | --- | --- |
| H01 | An exact-game command-line launch shows the legacy puzzle/player shell as well as the Review window and board. | The app appears to start two host UIs. | Do not show the legacy shell when `--source-game-id` successfully opens the study workspace. | Build now |
| H02 | The legacy shell remains in the macOS window cycle while studying. | Command-Tab/window cycling repeatedly lands on an irrelevant host. | Hide the Explorer shell for the entire study workspace, not just cover it. | Build now |
| H03 | The legacy shell displays its own large chessboard at a different position from the floating game board. | Two simultaneous boards appear to describe the same study but do not. | Make the floating board the only visible board during study. | Build now |
| H04 | The legacy shell exposes Puzzle Info, Move List, Analysis Replay, Game Review, and player tabs behind the new Review window. | Navigation appears duplicated and the source of truth is unclear. | Treat the shell as the Explorer state and the Review window as the study state. | Build now |
| H05 | The legacy Puzzle Info area says `Run Analyze` and `Awaiting analysis` during a retained, read-only game review. | It suggests that the displayed review is incomplete or that an engine should be started. | Keep those controls out of the visible study workspace; do not alter their puzzle-mode semantics. | Build now |
| H06 | Startup calls `show()` on the legacy host after the game has already opened, then raises study windows on a zero-delay timer. | The windows flicker and compete for focus. | Skip the legacy `show()` entirely for exact-game startup. | Build now |
| H07 | The board is activated last during startup. | The notation/evidence host is not the primary arrival surface. | Show the board first and activate the Review window last. | Build now |
| H08 | `Explorer` raises the legacy shell but leaves Review and all boards visible. | Explorer becomes a fourth overlapping surface instead of a workspace switch. | Hide the complete study workspace before surfacing Explorer. | Build now |
| H09 | Playback can continue while the user is in Explorer. | A hidden game may advance without the user seeing it. | Stop per-game playback whenever the study workspace is hidden. | Build now |
| H10 | Returning to Explorer does not explicitly save all board notes and appearance first. | Recent local edits depend on later timers or window destruction. | Flush notes/appearance before hiding each board. | Build now |
| H11 | Closing the last game tab leaves a blank Review host. | The user reaches a dead-end empty window with the real Explorer hidden. | Hide Review and return to Explorer when the final tab closes. | Build now |
| H12 | A single open game still consumes a full outer game-tab row even though the matchup is already in the native title. | It duplicates identity and wastes vertical space. | Hide the outer game tab bar for one game; reveal it automatically at two or more. | Build now |
| H13 | The outer game tab reappearing or disappearing has no invariant test. | A later style change can silently restore the redundant row. | Extend the multi-game UI test to cover one-game hidden and two-game visible states. | Build now |
| H14 | Closing from two games down to one does not simplify the tab hierarchy. | The redundant outer row remains after comparison work ends. | Recompute outer-tab visibility after every open and close. | Build now |
| H15 | Closing the Review window hides boards but does not describe the resulting workspace transition. | Depending on platform behavior, the app can appear to vanish while a hidden host still owns state. | Keep close as a study close; the explicit `Explorer` action and last-tab transition are the reversible navigation paths. Document the distinction. | Document now |
| H16 | Floating boards have no deterministic initial placement. | The OS may place a board directly on top of notation. | Place Review at the left and boards from the right edge inward. | Build now |
| H17 | Multiple floating boards receive the same default placement. | Comparing games produces an indistinguishable stack. | Cascade boards with a bounded per-game offset while keeping them on-screen. | Build now |
| H18 | Fixed 660×860 and 700×790 startup sizes assume a sufficiently large display. | Windows can exceed a smaller work area or obscure each other. | Clamp initial geometry to the available screen with safe margins. | Build now |
| H19 | Window activation and raising are performed in several methods with different ordering. | The frontmost surface depends on the route used to open a game. | Centralize the arrival order in `surfaceActiveGame`: board visible, Review active. | Build now |
| H20 | Opening an already-open game from Explorer can reveal Review without reliably restoring its hidden board. | Reopening a study can produce notation without the corresponding board. | Route all successful game opens through the same workspace-surface method. | Build now |
| H21 | The Review window owns an empty generic title until a current tab is established. | Transient chrome does not identify the game. | Continue deriving the native title from the selected game and update it on every tab change. | Already complete |
| H22 | The floating board repeated the full matchup inside the content area as well as the native title. | The board header was text-heavy. | Keep only `Game A/B/…` inside; preserve full facts in the title and tooltip. | Already complete |
| H23 | Opening and engine context were formerly split across a tall inspector region. | Useful notation space was lost to metadata. | Keep them in one compact chip strip with longer boundary text in tooltips. | Already complete |
| H24 | Coach Review used nested full borders and a placeholder retained-line box. | The selected moment felt like a dashboard and created clutter. | Use one state accent and reveal a retained line only after an explicit line choice. | Already complete |
| H25 | Expanded Coach evidence is only 185 px tall. | Section landmarks are present, but the content feels like a peephole. | Increase the evidence reading area while retaining scrolling and fixed text scale. | Build now |
| H26 | Detailed evidence includes long UCI and FEN material. | It is accurate and copyable but remains expert-facing. | Preserve raw evidence; keep wrapping, section labels, and chess-piece landmarks. Do not invent a graph or translate evidence. | Retained boundary |
| H27 | A clickable weighted move graph was requested conceptually, but retained data contains lines and engine values rather than a complete weighted tree. | A graph could falsely imply visit share, probability, or complete search. | Wait for an explicit retained graph schema; do not fabricate nodes or weights in Qt. | Data-boundary follow-up |
| H28 | Board and piece theme candidates exist separately from the app. | Silent integration would turn an unselected candidate into production behavior. | Keep current built-in palettes until the user explicitly selects a candidate theme. | Acceptance-boundary follow-up |
| H29 | The legacy shell still owns Explorer loading and long-lived controllers even when hidden. | Removing it outright would be a larger architectural migration. | Use it as a hidden controller/Explorer owner for now; separate application controllers from the legacy widget tree in a future architecture slice. | Architectural follow-up |
| H30 | Window-role behavior was described across code and conversation but not in one durable acceptance record. | Later work can reintroduce competing hosts without realizing it. | Keep this ledger and the game-study behavior document aligned with the tested lifecycle. | Build now |

## Acceptance checks

1. Exact-game startup exposes two visible top-level study windows: one Review window and one board. The legacy shell is not visible or present in the normal window cycle.
2. The Review window is the active arrival surface; the board remains visible and independently movable.
3. A single game has no outer game-tab row. Opening a second game reveals that row; returning to one hides it again.
4. `Explorer` pauses playback, saves notes/appearance, hides Review and every board, then surfaces the Explorer shell.
5. Reopening either an existing or new game hides Explorer and restores the same Review-plus-board model.
6. Closing the final game tab returns to Explorer instead of leaving an empty Review host.
7. No evidence label, retained value, source join, or engine/network boundary changes as part of this housekeeping.
