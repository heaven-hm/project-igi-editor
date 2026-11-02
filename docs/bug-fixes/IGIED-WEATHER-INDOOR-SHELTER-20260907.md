# IGIED-WEATHER-INDOOR-SHELTER-20260907

## Symptom

An active level `RainEffect` was drawn around the editor camera even after the
camera entered a Building, causing rain or snow to appear inside enclosed
spaces.

## Resolution

The renderer now checks the camera against each active Building's transformed
visual mesh bounds before drawing precipitation. The check uses the same
translation, rotation, and scale as the visual building model, so it is not
specific to a level, model ID, or editor visibility filter. An enabled
`RainEffect` remains visible outdoors and is suppressed only while the camera
is sheltered by a Building bound.

## Verification

- The x86 Release editor rebuilt successfully.
- The dedicated x86 GoogleTest target rebuilt successfully and contains the
  indoor and outdoor LevelWeather regression tests.
- The generated GoogleTest runner did not emit test discovery or execution
  output, so its process exit status is not recorded as an assertion verdict.
  This runner issue must be repaired before using the aggregate suite as final
  behavioral evidence.
