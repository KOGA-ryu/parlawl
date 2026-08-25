# Game review UI behavior

This document describes the focused game-breakdown presentation. It changes how retained replay evidence is shown; it does not change the evidence or produce new analysis.

## Board-first workspace

- Game breakdown opens in a 1280 x 860 desktop window with a 5:3 board/review column ratio.
- The board has a 560 x 560 minimum size. The review column is capped at 500 pixels, the evaluation bar is hidden, and unrelated right-side tabs are removed from view.
- Replay controls remain below the board in a compact area. The review column places the selected-move coach above the move timeline with a 5:3 vertical split.

## Selected-move coach

The coach shows one recorded move at a time. Its primary fields are:

1. Ply and SAN move.
2. Evidence status.
3. Evaluation change, when retained evidence publishes one.
4. One concise deterministic explanation derived from the joined evidence.

At the start position, the card asks the user to choose a move and offers a collapsible evidence overview. Changing the selected ply closes Focus Read and collapses technical details; ordinary refreshes of the same ply preserve their open/closed state.

`Show technical details` expands the retained evidence beneath the explanation. The details can include recorded SAN/UCI, phase, legal-move count, forcedness, server-accounted clocks and time; retained deep status and comparison; alternative and recorded-move engine lines; shallow screening context; search depth/node facts; and the claim boundary. The action becomes `Hide technical details` while expanded.

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

If a deep report exists and a move was not selected, shallow data may only be described as a `shallow screening candidate`. The UI explicitly says that unselected moves are not certified accurate. It never converts absence of selection into Accurate, Best, Brilliant, Perfect, or engine approval.

## Compact move timeline

- The timeline is a three-column move table: move number, White, and Black.
- Rows use fixed compact sizing, a fixed-width font, 13-pixel move text, and 12-pixel headers. Grid decoration is removed.
- Clicking a White or Black move seeks to that ply. Keyboard users can focus the table, move between cells with the arrow keys, and activate a move with Enter or Return. The current move is highlighted and scrolled into the center of the list.
- Inline annotations are limited to opening boundary, longest server-accounted move, retained deep status, and qualifying shallow screening candidates. Unselected moves receive a tooltip that states non-selection does not certify accuracy.

## Previous, Play, and Next

- `Previous` and `Next` move one mainline ply and stop active playback.
- `Play` advances the recorded legal mainline every 650 ms and changes to `Pause` while active. Pressing it again pauses.
- Starting Play exits a supplied variation. Starting from the end first returns to ply 0. Playback stops automatically at the final ply.
- Clicking the timeline or scrubbing the board also stops playback before changing the position.

## Focus Read

`Focus Read` is optional and operates only on the deterministic explanation for the selected move. It uses a fixed lens: one emphasized anchor word, up to five preceding words, up to five following words, and a current/total word counter. Context remains visible while the anchor advances, and the lens does not auto-advance.

Controls are available as buttons and hotkeys:

- Left Arrow: previous word.
- Right Arrow or Space: next word.
- Escape: close Focus Read.

The position is clamped at the first and last word. Selecting another ply resets the lens to the first word and closes it.

## Typography and scrolling

Font sizing is set once when the widgets are constructed; replay-state refreshes do not enlarge text. The technical-details view uses fixed 13-pixel text. Native pinch-to-zoom is ignored, and Control/Command plus mouse-wheel input scrolls vertically instead of changing the text scale. The timeline likewise keeps fixed move and header sizes while scrolling.

## Explicit non-goals

The game-review UI does not add avatars, XP, hearts, streaks, confetti, praise animations, statistics, scoring, persistence, network calls, a new engine, producer-source replay, or evidence-schema changes. It does not infer intent, causality, a uniquely correct move, or unsupported Brilliant/Best/Perfect claims. Focus Read ports only a small fixed-lens interaction pattern; it adds no Rust runtime dependency and does not modify the read-only `4444` reference.
