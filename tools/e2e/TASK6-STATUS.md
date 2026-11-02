# Graph and animation workflow checkpoint

On 2026-09-05, the narrowed smoke matrix completed all 14 levels: one named
non-cutscene graph per level, selected by largest node count. Every run produced
a screenshot and a matching graph-load log in its own log interval. Evidence:
`artifacts/e2e/non-cutscene-14-live/run.json`. These are opening/focusing checks,
not edit/save/reload tests. Level 1's inspected F11 view was beneath terrain.

The 336 scenarios are draft live workflows: 295 graph cases and 41 animation
representatives across levels 1 through 14. Offline manifest validation passed,
but this does not establish successful UI execution.

The visible level 1 graph smoke currently fails node selection and the graph
edit assertion. Its automatic restoration verified the original graph hash.
Animation button coordinates, anonymous-task selection, save/reload coverage,
and patrol behavior still require implementation or live validation.

Do not interpret action names or successful input dispatch as semantic success.
The native focused run used the existing Release test executable: 66 tests
passed and one live integration test was skipped; two additional RuntimeAiTest
waypoint tests failed in the broader run.
