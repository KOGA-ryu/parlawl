## 2026-03-28 modularization pass

### findings

- `workers/analysis_py/worker.py` was the clearest god file in the project. It mixed:
  - protocol entrypoint logic
  - chess replay helpers
  - move scoring and ranking logic
  - retained-packet derivation
  - structural evidence extraction across v1, v1.1, v2, and v3
- the structural packet logic had already grown into a distinct feature area but was still embedded inline inside the worker entry file.
- the result was high review density: a change to one structural feature required navigating unrelated orchestration and move-analysis code.

### structural decisions

- extracted structural evidence code by feature responsibility rather than by generic utility layer.
- kept `worker.py` as the orchestration surface and compatibility import surface for existing tests.
- split the structural packet into:
  - `structural_basics.py` for v1 and v1.1 board-state primitives and local-target selection
  - `structural_extensions.py` for v2, v3, and move-level structural link derivation
  - `structural_packet.py` for the retained-break structural packet assembly
- split retained critical-move / retained-break formatting out of the worker into:
  - `critical_move_formatting.py` for divergence, retained-break, omission, collapse-sequence, and critical-move record formatting
- split retained local-window move-selection heuristics out of the worker into:
  - `critical_move_selection.py` for decisive/holding/preventative/counterplay candidate selection and retained-candidate resolution
- preserved existing worker-facing interfaces by importing the extracted functions back into `worker.py`.

### new module map

- `workers/analysis_py/worker.py`
  - request orchestration
  - move analysis and ranking
  - retained-break, divergence, and critical-move logic
  - compatibility import surface for extracted structural helpers
- `workers/analysis_py/structural_basics.py`
  - king exposure, luft/back-rank, pressure, loose pieces, imbalance, overload
  - local target selection and compact structural summaries
- `workers/analysis_py/structural_extensions.py`
  - pinned critical pieces
  - defender-removal exposure
  - color-complex weakness
  - target-zone imbalance
  - escape geometry / flight control / defensive escape fragility
  - move-level structural link fields
- `workers/analysis_py/structural_packet.py`
  - retained-break structural packet extraction and field assembly
- `workers/analysis_py/critical_move_formatting.py`
  - critical-reason normalization
  - continuation compact formatting
  - candidate ranking / evidence note formatting
  - retained-break divergence formatting
  - collapse-sequence / omission formatting
  - critical-move record assembly helpers
- `workers/analysis_py/critical_move_selection.py`
  - local-window move snapshots for retained-candidate heuristics
  - decisive / last-holding / preventative / counterplay candidate selection
  - retained critical-move selection and adjacent-candidate suppression

### intentionally preserved behavior

- worker input/output contract
- persisted tactical and critical-move field names
- report content and shape
- existing test imports that reference helper functions through `workers.analysis_py.worker`
- current bounded local-window analysis scope

### risky areas

- `worker.py` remains the largest file in the project and still contains multiple dense feature clusters:
  - score/ranking logic
  - high-level request orchestration and packet assembly
- compatibility re-exports from `worker.py` are intentional, but they keep some import sprawl in place.

### follow-up opportunities

- if another pass is ever needed, the next candidate is score/ranking normalization around local-window analysis rather than more retained-packet cleanup.
- reduce fixture/test duplication in `workers/analysis_py/test_worker_logic.py` once the behavior surface stabilizes.
- consider a later cleanup of report rendering and storage field-group organization, but only if that work remains behavior-preserving and narrowly scoped.
