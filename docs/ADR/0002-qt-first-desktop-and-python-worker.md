# adr 0002: qt-first desktop and python worker split

## status

accepted

## context

The product is a local desktop system centered on UI workflows, settings, SQLite access, orchestration, and local reporting. Chess-specific replay and engine analysis are likely to stabilize faster in Python.

## decision

Adopt a Qt-first desktop architecture:

- Qt Widgets desktop shell in C++20
- SQLite access through Qt SQL
- local settings through `QSettings`
- process orchestration through `QProcess`
- Python worker for PGN replay and Stockfish orchestration
- explicit JSON-over-stdio protocol between desktop and worker

## consequences

- the desktop application remains the primary product shell
- chess analysis can iterate independently inside the worker
- core logic is kept out of the UI layer through orchestration and storage modules
- future worker replacement remains possible because the boundary is explicit
