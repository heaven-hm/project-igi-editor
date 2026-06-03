# Parameter Data Types

Declarations use these types to describe each parameter:

| Type | Size | Description | JSON mapping |
|---|---|---|---|
| bool8 | 1 value | Boolean (TRUE/FALSE) | boolean |
| Int16 | 1 value | 16-bit signed integer | number |
| Int32 | 1 value | 32-bit signed integer | number |
| Real32 | 1 value | 32-bit float | number |
| Real64 | 1 value | 64-bit float | number |
| Angle | 1 value | Float angle in radians | number |
| Degrees | 1 value | Float angle in degrees | number |
| RangeReal32 | 1 value | Float (range-constrained in editor) | number |
| ObjectPos | 3 values | Position as (X, Y, Z) floats | [x, y, z] |
| Real32x3 | 3 values | 3-component float vector | [x, y, z] |
| Real64x3 | 3 values | 3-component double vector | [x, y, z] |
| Real32x9 | 3 values | Orientation stored as 3 floats (alpha, beta, gamma radians) | [a, b, g] |
| RGB | 3 values | Color as (R, G, B) floats 0–1 | [r, g, b] |
| String16 | 1 value | String up to 16 chars (model IDs, sound names) | string |
| String32 | 1 value | String up to 32 chars | string |
| String256 | 1 value | String up to 256 chars (file paths) | string |
| VarString | 1 value | Variable-length expression string (scripting) | string |
| EnumInt32 | 1 value | Enum stored as integer | number |
| EnumString32 | 1 value | Enum stored as string | string |
| DropDownCombo | 1 value | Editor dropdown (string value) | string |
| PushButton | 1 value | Editor-only action button (always FALSE in data) | boolean |
| Graph | 1 value | AI graph data blob (node/edge counts as comma-separated) | string |
| AnimData | 1 value | Animation data blob | string |
