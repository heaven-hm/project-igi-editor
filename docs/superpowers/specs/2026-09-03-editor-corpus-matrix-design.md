# Editor Corpus E2E Matrix Design

## Goal

Make live editor regression coverage proportional to the installed corpus. Every installed `location0/level1` through `level14` must be loaded in the visible editor and checked for process health, authored weather resolution, resource presence, texture-resolution failures, viewport rendering, pause-menu rendering, cursor state, terrain-edit activation, and graceful shutdown.

## Coverage contract

The matrix is generated from `D:\\IGI1`, not hand-maintained level names. Generation fails if the corpus is not exactly the expected 14 location0 levels or if a level is missing its authored scene, DAT, model archive, texture archive, or lightmap archive. Each generated scenario contains:

- WMI launch in the installed game directory and interactive Session 1 health checks.
- A bounded `LoadLevel() COMPLETE` log assertion.
- Presence and non-empty checks for the level's scene and resource files.
- A weather-resolution assertion and negative assertions for load-fatal and texture-not-found diagnostics scoped to that launch.
- A full viewport screenshot with tolerant non-black/luma metrics; generic image-entropy thresholds are not reliable across terrain types.
- A pause-menu screenshot, pause rendering metrics, native-cursor-visible assertion, and post-pause custom-cursor-state assertion.
- A separate `T` shortcut scenario that requires the terrain-edit palette in a screenshot for every level.
- Graceful close with a non-zero exit-code failure; forced cleanup remains available only for scenarios that explicitly need it.

The existing focused scenarios remain separate for deeper workflows that require stable interaction coordinates: logging/severity changes, property selection, save/reopen persistence, and model import with texture application evidence. The corpus matrix complements those scenarios; it does not replace them.

## Safety

Corpus scenarios are non-mutating. Mutating feature scenarios continue to require `-AllowGameDataMutation`, run serially, and restore the installed corpus before the final regression gate. Reports retain one directory per level with screenshots and per-step evidence.
