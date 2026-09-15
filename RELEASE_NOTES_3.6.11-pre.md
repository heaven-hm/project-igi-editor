# Project IGI Editor 3.6.11-pre

This pre-release introduces live weather controls, authentic OpenIGI precipitation parity, indoor weather shelter suppression, accurately aligned rail tracks and spline spans, enhanced F11 object framing with rigid center rotation, and major foreign model texture resolution improvements.

### Key Highlights:

- **Interactive Weather Mode Toggle**:
  - Live in-game pause menu toggle (`Weather: [Default / Rain / Snow / OFF]`) cycling between authored weather, forced rain, forced snow, and weather disable.
  - Authentic OpenIGI rain & snow particle footprint and quad rendering parity (1200 drops, 900 flakes with 0.09m diameter and sinusoidal drift).
  - Single-source-of-truth GLSL shaders generated directly with blending and depth restoration.
- **Indoor Weather Shelter**:
  - Precipitation is accurately suppressed inside building footprints and roofs while remaining visible in open sky outdoors.
- **Accurate Rail Tracks & Spline Spans**:
  - Spline span segment models now resolve from the end waypoint (`ResolveSegmentModel`).
  - Waypoint Z-X-Y Euler orientation correctly mapped to local-X spline tangents scaled to span chord length (`MakeWaypointTangent`).
  - Tangent overshoots and distortions at rail/road transitions eliminated.
  - Spline segment model schema exposed in task tree properties.
- **F11 Camera Snap & Framing Accuracy**:
  - F11 camera framing centers accurately on rendered mesh bounds, incorporating rigid object Euler rotation.
  - Prevents NaN camera snap on graph selections and zero-extent items.
  - Snap works reliably through pause gate while keeping editor overlays visible.
- **Foreign Model Import & Shared Common Textures**:
  - Foreign models (e.g. Sniper `001_02_1`) resolve textures via common-archive source bundles (`location0.res` fallback with level precedence).
  - Clean common textures preferred over duplicate/polluted level copies.
  - Instant cache invalidation upon publish.
- **Editor Performance & Interactive Responsiveness**:
  - Smooth hover picking and frame tracking; picking bypassed during mouse drags.
  - ATTA sub-models restored while moving; immediate-mode unbind safety guards.
