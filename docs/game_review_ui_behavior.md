# Game review UI behavior

This document describes the focused game-breakdown presentation. It changes how retained replay evidence is shown; it does not change the evidence or produce new analysis.

## Board-first workspace

- Game breakdown opens in a 1280 x 860 desktop window with a 5:3 board/review column ratio.
- The board has a 560 x 560 minimum size. The review column is capped at 500 pixels, the evaluation bar is hidden, and unrelated right-side tabs are removed from view.
- Replay controls remain below the board in a compact area. The review column uses one continuous rail: compact game facts followed by the move table. It has no visible `Game Review` or `move timeline` title.
- The native window title carries player names, ratings, result, and date. The compact metadata row reduces opening and retained-engine facts to `ECO · opening` and `SF · nodes · selected`; plain-language provenance remains in tooltips.

## Selected-move coach

Clicking a White or Black notation selects that ply and opens one full-width coach row immediately beneath its move pair. Clicking the selected notation again collapses the row; selecting another notation moves the expansion to the new pair. The row's primary fields are:

1. Exact evidence status.
2. A compact mover-expectation change such as `◒ 91.7% → 75.1% −16.6 pp`, when retained evidence publishes one.
3. One concise deterministic explanation derived from the joined evidence.

Ply and SAN are not repeated in the expanded row because the selected notation is highlighted directly above it. At the start position, the row is absent. An ordinary move that was not selected for deep review expands to deterministic move mechanics such as phase, legal-move count, forcedness, and server-accounted time; its shallow values are not exposed as an unlabeled score. A shallow-only report uses the exact quiet `shallow screening candidate` label with its compact expectation strip. Changing the selected ply closes Focus Read and technical details; clicking the same notation controls the row's open/closed state.

For a retained deep moment, `▸ Details` is a quiet disclosure inside the inline row, not a primary button. It expands retained SAN/UCI, phase, legal-move count, forcedness, server-accounted clocks and time; retained deep status and comparison; alternative and recorded-move engine lines; shallow screening context; search depth/node facts; and the claim boundary. It becomes `▾ Details` while expanded.

## Evidence vocabulary and state mapping

The visible evidence vocabulary is fixed:

| Retained state | Visible status |
| --- | --- |
| `confirmed_severe_error` | `deep confirmed severe` |
| `confirmed_missed_opportunity` | `confirmed missed opportunity` |
| `ambiguous_engine_instability` | `ambiguous` |
| `incomplete_deep_evidence` | `ambiguous` |
| `below_confirmation_threshold` | `below threshold` |
| Deep report exists, but the move has no selected deep moment | `not selected for deep review` |
| No deep report, but persisted shallow evidence exists | `shallow screening candidate` |

An unknown deep source status fails closed as `evidence unavailable`; its raw retained status remains visible in technical details. A move with no joined evidence uses the same neutral fallback. At the start position, evidence status and evaluation change remain hidden until a move is selected. These states do not make a move-quality claim.

If a deep report exists and a move was not selected, shallow data may only be described as a `shallow screening candidate`. The exact `not selected for deep review` state remains available to assistive text, timeline tooltips, and technical evidence; it is not rendered as a yellow warning pill or a paragraph. The UI never converts absence of selection into Accurate, Best, Brilliant, Perfect, or engine approval.

## Compact move timeline

- The timeline is a three-column move table: move number, White, and Black.
- Rows use fixed compact sizing, a fixed-width font, 13-pixel move text, and 12-pixel headers. Grid decoration is removed.
- Clicking a White or Black move seeks to that ply and opens its coach row beneath the move pair. A second click collapses it. Keyboard users can focus the table, move between notation cells with the arrow keys, and activate a move with Enter or Return. The current move is highlighted and scrolled into the center of the list.
- Inline annotations are symbol-only: `◫` marks the first move outside the retained opening path, `◷` marks the longest server-accounted move, and `●` marks a retained deep moment. Tooltips explain each symbol and preserve exact evidence wording. Unselected moves receive a tooltip that states non-selection does not certify accuracy.
- The long evidence disclaimer is not permanently displayed above the table; it remains available in tooltips and Details.

## Previous, Play, and Next

- `Previous` and `Next` move one mainline ply and stop active playback.
- `Play` advances the recorded legal mainline every 650 ms and changes to `Pause` while active. Pressing it again pauses.
- Starting Play exits a supplied variation. Starting from the end first returns to ply 0. Playback stops automatically at the final ply.
- Clicking the timeline or scrubbing the board also stops playback before changing the position.

## Focus Read

`Focus Read` is optional and appears inside the expanded coach row for a retained deep moment. It operates only on the deterministic explanation for the selected move. It uses a fixed lens: one emphasized anchor word, up to four preceding words, up to four following words, and a current/total word counter. Context remains visible while the anchor advances, and the lens does not auto-advance.

Controls are available as buttons and hotkeys:

- Left Arrow: previous word.
- Right Arrow or Space: next word.
- Escape: close Focus Read.

The position is clamped at the first and last word. Selecting another ply resets the lens to the first word and closes it.

## Typography and scrolling

Font sizing is set once when the widgets are constructed; replay-state refreshes do not enlarge text. The technical-details view uses fixed 13-pixel text. Native pinch-to-zoom is ignored, and Control/Command plus mouse-wheel input scrolls vertically instead of changing the text scale. The timeline likewise keeps fixed move and header sizes while scrolling.

## Explicit non-goals

The game-review UI does not add avatars, XP, hearts, streaks, confetti, praise animations, statistics, scoring, persistence, network calls, a new engine, producer-source replay, or evidence-schema changes. It does not infer intent, causality, a uniquely correct move, or unsupported Brilliant/Best/Perfect claims. Focus Read ports only a small fixed-lens interaction pattern; it adds no Rust runtime dependency and does not modify the read-only `4444` reference.
