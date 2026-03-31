# adr 0001: two-repo model

## status

accepted

## context

`parlawl` needs a strict boundary between implementation-facing material and exploratory or review-heavy material.

## decision

Use two repositories:

- `parlawl/` for code, tests, schemas, build configuration, and lean implementation docs only
- `parlawl-docs/` for architecture notes, taxonomy drafts, roadmap material, reviews, research, and postmortems

## consequences

- implementation docs stay short and current
- research and review material can grow without polluting the code repo
- architecture and taxonomy discussion can evolve without forcing immediate code churn
