# annotated replay import

ParlAWL can open an `annotated-game-replay-v1` JSON artifact produced by the
esports probability lab. The import is a read-only presentation boundary: it
does not query Chess.com or Lichess, start Stockfish, invoke the Python worker,
or write to ParlAWL's SQLite database.

## use

1. Open the **Analysis Replay** tab.
2. Choose **Open Analysis Replay** and select the exported JSON file.
3. Use **Prev**, **Next**, the mouse wheel, or the SAN move list to navigate the
   immutable played game.
4. When a move has a retained alternative, choose **Show Engine Line**. The
   board enters the already-recorded line and labels it **ENGINE LINE, NOT
   PLAYED**.
5. Choose **Return to Game** to discard the temporary branch and restore the
   actual played move exactly.
6. Choose **Back to Puzzles** to return to the unchanged puzzle session.

## validation

The importer opens one absolute, non-symlink final path through a no-follow
file descriptor, requires a regular file of at most 4 MiB, and reads that same
descriptor under a before/after size check. It requires the exact v1 object
shape, two to 700 contiguous plies, bounded supplied facts and narration, and
an exact legal UCI/FEN chain. Retained engine branches must begin at their
recorded checkpoint, start with the supplied preferred move, remain legal, and
carry restore fields that match the actual played move and post-move position.

Any contract-shape or independently checkable chess-mechanics mismatch rejects
the entire file before the desktop enters replay mode. The canonical mainline
is never modified by branch navigation, and a failed return leaves the active
branch unchanged.

## evidence boundary

The imported file contains supplied derived lineage references and fixed-node
engine claims. ParlAWL preserves the supplied facts, authorities, narration,
opening classification, engine metadata, node limit, and semantic-ID-shaped
strings without inventing new explanations. **ParlAWL does not recompute the
esports pipeline's semantic identities and does not authenticate those supplied
annotations or engine claims.** The UI labels them as supplied and not verified.

ParlAWL independently verifies only the mechanical presentation boundary: the
played UCI moves, generated SAN, exact FEN sequence, legality of displayed
branch moves, and exact return to the immutable played-game checkpoint.

A displayed alternative is a supplied review line, not a playable puzzle and
not proof of an objectively best move. Puzzle creation remains a separate
contract that requires a complete, independently engine-validated solution
record.

While replay mode is active, ParlAWL stops and resets any fresh Stockfish
review, hides puzzle metadata, settings, analysis configuration, reports,
status logs, and recent runs, and guards their handlers against database,
network, settings, or puzzle-session work. **Back to Puzzles** restores the
unchanged underlying puzzle workspace and its controls.
