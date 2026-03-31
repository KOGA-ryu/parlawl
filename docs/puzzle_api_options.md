# Puzzle API Options

Parlawl centralizes Lichess puzzle endpoint construction in `libs/lichess/` so puzzle callers do not hand-roll paths, query strings, or request bodies.

## Modules

- `libs/lichess/puzzle_api_types.h/.cpp`
  - centralized puzzle enums
  - `puzzle_difficulty`
  - `puzzle_color`
- `libs/lichess/puzzle_api_requests.h/.cpp`
  - typed request option models
  - endpoint path builders
  - query serialization
  - request-body serialization for batch solve

## Supported Endpoints

### GET `/api/puzzle/{id}`

- model: `puzzle_id_request`
- required:
  - `id`
- no query params

### GET `/api/puzzle/daily`

- model: `puzzle_daily_request`
- no path params
- no query params

### GET `/api/puzzle/next`

- model: `puzzle_next_options`
- optional query params:
  - `angle`
  - `difficulty`
  - `color`

Supported difficulty values:

- `easiest`
- `easier`
- `normal`
- `harder`
- `hardest`

Supported color values:

- `white`
- `black`

Unset values are omitted.

### GET `/api/puzzle/batch/{angle}`

- model: `puzzle_batch_fetch_options`
- path param:
  - `angle`
- query params:
  - `difficulty`
  - `nb`
  - `color`

Constraints:

- `nb` must be between `1` and `50`
- `color` is only valid when `nb == 1`
- invalid combinations fail early and are not serialized

Current Parlawl trainer usage:

- uses batch fetch with a scalar difficulty and `nb > 1`
- omits `color` entirely for trainer batches
- does not rely on repeated query params or undocumented array-style query behavior

Parlawl currently uses this as the primary live puzzle-supply endpoint.

### POST `/api/puzzle/batch/{angle}`

- models:
  - `puzzle_batch_solve_options`
  - `puzzle_batch_solve_request`
  - `puzzle_batch_solution`
- path param:
  - `angle`
- query params:
  - `nb`
- JSON body:
  - `solutions`

Constraints:

- `nb` must be between `0` and `50`
- at least one solution is required
- each solution must include a non-empty puzzle id

### GET `/api/puzzle/activity`

- model: `puzzle_activity_options`
- optional query params:
  - `max`
  - `before`
  - `since`

Constraints:

- `max` must be a non-negative integer
- `before` and `since` must be non-negative integer timestamps

Response handling remains NDJSON-aware in the client layer.

### GET `/api/puzzle/replay/{days}/{theme}`

- model: `puzzle_replay_request`
- path params:
  - `days`
  - `theme`

Constraints:

- `days` must be non-negative
- `theme` is required

### GET `/api/puzzle/dashboard/{days}`

- model: `puzzle_dashboard_request`
- path param:
  - `days`

Constraints:

- `days` must be non-negative

## Invalid Combinations

Parlawl rejects invalid puzzle-request combinations early instead of passing them through silently.

Examples:

- batch fetch with `color=white` and `nb=10`
- negative `before` timestamp for puzzle activity
- empty batch `angle`
- empty solve request body

## Current Integration

Current puzzle network callers use the centralized option layer through `LichessClient`:

- latest solved puzzle activity
- puzzle detail fetch by id
- live training batch fetch

The shared rate-limit policy still lives in `PuzzleSupplyCoordinator`. The option layer only handles request modeling and construction.

## Developer Note

When adding a new puzzle endpoint:

1. add any new constrained enums or value helpers to `puzzle_api_types.*`
2. add a typed request model to `puzzle_api_requests.h`
3. add validation and path/query/body serialization to `puzzle_api_requests.cpp`
4. wire the caller through `LichessClient`
5. add a focused unit test for:
   - valid serialization
   - invalid combinations
6. update this doc

Do not add raw puzzle endpoint strings or ad hoc query assembly in UI, controller, or coordinator code.
