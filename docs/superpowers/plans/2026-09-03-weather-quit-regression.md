# Weather and Editor Quit Regression

## Goal

Make editor shutdown idempotent and ensure weather is enabled only from a valid
per-level `RainEffect` object, with regression coverage for every level 1-14.

## Architecture

- Keep shutdown ownership in `App::Shutdown`, but make repeated calls safe because
  both `main` and the global `App` destructor can reach it.
- Move RainEffect token validation and weather selection into a small pure runtime
  policy function. `LoadLevel` will apply only the resolved result after resetting
  the renderer state.
- Test the policy with a data-driven table covering all fourteen levels plus
  malformed and stale-state cases.

## Implementation Tasks

1. Capture current crash evidence and inspect the installed level object data.
2. Add failing regression tests for idempotent shutdown entry and all level weather
   object cases.
3. Implement the shutdown guard and validated weather resolver/reset behavior.
4. Build the editor, deploy it to `D:\IGI1`, launch through WMI, and exercise a
   graceful quit; run the complete relevant test suite.
5. Review the diff, commit the verified changes, push the current branch, and record
   the bug ID and resolution in the required memory note.

## Verification

- Unit tests prove no-weather levels remain disabled, valid rain/snow objects map
  correctly, malformed objects cannot activate weather, and all levels are covered.
- Release build succeeds and the deployed editor exits through its normal close path
  without a new application crash event.
- The pushed commit and remote branch tip match the local verified commit.
