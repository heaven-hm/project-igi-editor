[Back to README](../README.md)

# MEF Model Naming Convention

MEF model filenames follow the pattern `aaa_bb_c.mef` where each component has a specific meaning. Some models use descriptive names instead of numeric prefixes (e.g. `igiagent_1.mef`, `forest_tower_1.mef`).

## Pattern Components

| Component | Meaning | Example |
|-----------|---------|---------|
| `aaa` | Object category prefix | `940` = vegetation |
| `bb` | Variant/sub-model index | `01`, `02`, ... different shapes within a category |
| `c` | LOD level (Level of Detail) | `1` = highest detail, `5` = lowest detail |

## LOD System

The `c` value is a Level of Detail index. The engine loads the base model (`c=1`) and substitutes lower-detail versions at increasing distances, as defined by `ModelLODSettings` in objects.qsc (LOD 2 through LOD 5 plus a cutoff distance).

Not all models have 5 LOD levels -- some only have `c=1`, while others go up to `c=5`. File sizes decrease with higher `c` values (median ratio of lowest to highest LOD is ~0.16).

Example -- `940_01` (tall pine tree):

| LOD | File | Size |
|-----|------|------|
| 1 | `940_01_1.mef` | 33,888 bytes |
| 2 | `940_01_2.mef` | 8,080 bytes |
| 3 | `940_01_3.mef` | 5,940 bytes |
| 4 | `940_01_4.mef` | 2,628 bytes |
| 5 | `940_01_5.mef` | 1,824 bytes |

## Statistics

- Total MEF files: 7,609
- Files matching `aaa_bb_c` pattern: 7,223 (94.9%)
- Non-pattern files (named models): 386 (5.1%)
- Unique `aaa` prefixes: 204

## Category Prefix Mapping

Extracted from `magicobjconfig.qvm`, `weapons/weapon.qvm`, `lod.qvm`, and 24 level `objects.qvm` files inside `.res` archives.

### Overview by Range

| Prefix Range | Category |
|---|---|
| 005 | Player/Misc |
| 100--153 | Weapons & Equipment |
| 200--262 | Indoor Props/Furniture |
| 300--371 | Outdoor Structures/Props |
| 400--460 | Buildings & Architecture |
| 500--563 | Doors |
| 600--667 | Ground Vehicles |
| 700--771 | Air Vehicles & Special |
| 800--806 | Characters/Misc |
| 900--963 | Vegetation/Nature |
| 1112--1123 | Shell Casings |
| 2030--2031 | Watercraft |

### Weapons & Equipment (100--153)

| Prefix | Item |
|---|---|
| 100 | SMG-2 |
| 101 | Glock |
| 102 | MP5A3 |
| 103 | SOCOM |
| 104 | PSG-1 |
| 105 | G36 |
| 106 | Scope/attachment |
| 107 | AK-47 |
| 108 | Steyr AUG |
| 109 | Binoculars |
| 110 | M16 A2 |
| 111 | Combat Knife |
| 112 | M2HB (.50 cal HMG) |
| 113 | C4 Bomb/Backpack |
| 114 | Medi kit |
| 116--121 | Ammo types |
| 122 | Grenade (explosive) |
| 123 | Grenade part |
| 124 | Flashbang |
| 126 | SPAS-12 |
| 127 | Colt Anaconda/Magnum |
| 128 | Jackhammer |
| 129 | LAW 80 |
| 130 | UZI |
| 131 | FN Minimi (SAW) |
| 132 | MAC-10 |
| 133 | Thrown grenade |
| 134 | Thrown flashbang |
| 135 | Grenade pin |
| 136 | Proximity mine |
| 137 | SVD Dragunov |
| 138 | Desert Eagle |
| 139 | Glock SD / C4 Timer |
| 140 | RPG-7 |
| 141 | M203 grenade |
| 142 | Barrett M82A1 |
| 143 | M1014 shotgun |
| 144 | Smoke grenade |
| 145 | G11 |
| 146 | Makarov |
| 147 | Type 64 SMG |
| 148 | Laser designator / IR |
| 149 | Thermal / IR |
| 150 | Medipack |
| 151 | Silenced SMG-2 |
| 152 | Smoke projectile |
| 153 | PSG-1 SD |

### Indoor Props/Furniture (200--262)

| Prefix | Description |
|---|---|
| 200 | EMP briefcase, elevator |
| 201 | Switch models |
| 202 | Elevator, doors |
| 203 | Furniture (toilet, locker, table, chair) |
| 204 | Corner table |
| 205 | Floor lamp, chair, monitor, rocks |
| 206 | PC (Russian) |
| 207 | Curtains |
| 208 | Chinese locker |
| 220 | Alarm pad, army PC, laptop |
| 221 | Drawer |
| 230 | Bed, safe, letter pickup |
| 231 | Sofa, tables, bench, TV |
| 232 | Switch/button models |
| 237 | Plant |
| 239 | Wooden case of explosives |
| 240 | Sewer light |
| 260 | Bunk, desk, lab tube light, boxes |
| 261 | Radar terminal, data streamer, chair |
| 262 | Radar panel, rocket fuelling display |

### Outdoor Structures/Props (300--371)

| Prefix | Description |
|---|---|
| 300 | Wall segments, watchtowers |
| 302 | Urban walls |
| 303 | Fence, barrier |
| 305 | Concrete walls, air vent |
| 306 | Crate (IGI1), concrete barrier |
| 307 | Road spline, tunnel |
| 313 | Sentry camera/gun |
| 320 | Wire |
| 330 | Crates (wooden) |
| 331 | Containers (cargo) |
| 332 | Minefield marker |
| 333 | Explosive barrel |
| 334 | Alarm switch |
| 335 | Security camera |
| 336 | Barb fence, sewer ladder |
| 337 | Fuel pump, signs, floodlight, generator |
| 338 | Rock |
| 339 | Weather balloon crate |
| 340 | Wooden wall, rocks, bridge, toilet |
| 341 | Airdrop canister, parachute |
| 342 | Arrow marker, destroyed barrel, crates |
| 350 | Ammo crate, fuel hose, alarm, crane ladder |
| 360 | Sandbags, concrete blocks |
| 361 | Parabol, radio mast |
| 370 | Electric fence |
| 371 | Launch tower structures |

### Buildings & Architecture (400--460)

| Prefix | Description |
|---|---|
| 400 | Flood light, air vent, silo, gantry |
| 401 | Buildings, palace, houses |
| 405 | Middle east brick house |
| 406 | Small wall, tower, palace |
| 407 | Space control, warehouse, house interiors |
| 408 | Chinese Temple |
| 409 | Bunker, bridge segments |
| 410 | Train tunnel, office building, windows |
| 411 | Chip/EMP factory |
| 412 | Laboratory |
| 413 | Office |
| 414 | Stone crusher, conveyor belt, ladder |
| 415 | Cave office window |
| 420 | Snow house |
| 430 | Prison lamp |
| 431 | Priboi's mansion, windows |
| 432 | Prison tunnel shed, metal shed |
| 433 | Door holder, stairs |
| 434 | Guard shed, airfield tower, hangar |
| 435 | Crane mounting, crane glass |
| 436 | Admin stairs |
| 437 | Admin interior, windows |
| 456 | Sentry gun (weapon system) |
| 460 | Elevator shaft, server room, tunnel |

### Doors (500--563)

| Prefix | Description |
|---|---|
| 500 | Gate, armoured door |
| 501 | Old door, hangar door, wooden wall door |
| 502 | Armoured door (heavy) |
| 515 | Blue door, air ventilation door |
| 520 | Massive Russian industrial door |
| 522 | Old wooden door |
| 523 | Toilet door |
| 526 | Electex door |
| 560 | Asian keylock, glass door, lab cave entrance |
| 561 | Door variant |
| 562 | Tunnel entrance door, Chinese doors |
| 563 | Door variant |

### Ground Vehicles (600--667)

| Prefix | Description |
|---|---|
| 600 | Truck (static/Kjetil's) |
| 603 | Tank truck, mining train |
| 604 | Destroyed M35 truck |
| 605 | Forklift |
| 606 | Lada, Priboi's limo |
| 610 | Car door model |
| 614 | T80 tank (weapon model) |
| 616 | Limo |
| 631 | Truck cab, trailer |
| 660 | Trailer, generator, mobile radar |
| 661 | APC |
| 662 | Rocket, module |
| 663 | Chinese truck |
| 665 | APC destroyed |
| 666 | Destroyed China truck |
| 667 | APC static |

### Air Vehicles & Special (700--771)

| Prefix | Description |
|---|---|
| 700 | Sentry gun, C-130 Hercules |
| 709 | Hit zone model |
| 710 | Black Hawk helicopter |
| 711 | Rotor blade |
| 712 | Rotor blade |
| 720 | Hind helicopter |
| 730 | Desert Huey |
| 740 | Russian helicopter (KA-27) |
| 741 | Russian helicopter (airborne) |
| 760 | Destroyed JSF, laser bomb |
| 770 | Door for aircraft |

### Characters/Misc (800--806)

| Prefix | Description |
|---|---|
| 800 | Stack of stools |
| 801--805 | Prisoner, misc objects |
| 806 | Priboi's suitcase (pickup) |

### Vegetation/Nature (900--963)

| Prefix | Description |
|---|---|
| 900 | Palm trees, modelled grass |
| 930 | Pine trees, bush, prototype tree |
| 931 | Birch tree |
| 932 | Prototype tree, pine detail |
| 933 | Grass, small green plant |
| 935 | Tree stump |
| 938 | Snowy tree |
| 939 | Snowy pine tree |
| 940 | Tall pine, pine tree, birch (all sizes) |
| 960 | Tree, bush |
| 962 | Cherry tree |
| 963 | Chinese high poly tree |

### Shell Casings (1112--1123)

| Prefix | Description |
|---|---|
| 1112 | Shotgun shell casings |
| 1123 | Bullet shell casings (9mm, 5.56, 7.62, 12.7) |

### Watercraft (2030--2031)

| Prefix | Description |
|---|---|
| 2030 | Eurocrane (seacraft) |
| 2031 | Fishing boat |

## MagicObj Types

Special interactive behaviors assigned to model sub-parts in `magicobjconfig.qvm`:

| Type | Description | Example prefixes |
|---|---|---|
| SHADOWVOLUME | Shadow casting geometry | Most weapons (100--153) |
| GLASS | Breakable glass | 231, 260, 407--415, 431, 600, 603, 663 |
| LADDER | Climbable surface | 336, 350, 360, 400, 414, 436, 460 |
| WHEEL | Rotating wheel | 600, 616, 661, 663 |
| CARDOOR | Opening car door | 610 |
| DEATHZONE | Lethal area | 603, 610 |
| AISTATIONARYGUN | AI-controlled stationary weapon | 313, 661, 700, 720 |
| HITZONE | Damageable area with health | 709 |
| ROTOR | Helicopter rotor blade | 711, 712 |
| GRENADEPIN | Grenade pin model | 135, 144 |
| RPGROCKET | RPG rocket projectile | 140 |
| DRAWER | Openable drawer | 221 |
| BOMBBACKPACK | Bomb/backpack model | 113 |

## Named Models (Non-numeric)

386 files (5.1%) use descriptive names instead of numeric prefixes but follow the same `name_c.mef` LOD convention:

- **Characters**: `igiagent`, `jones`, `priboi`, `anya`, `sci`, `rq`, `zib`
- **Structures**: `aztec_temple`, `forest_tower`, `hut_table`, `timber_hut`
- **Vehicles**: `bad_sc`, `good_sc`
- **Shadow meshes**: `hawkshadow`, `helishadow`, `boatshadow`
- **Misc**: `100m`, `blueprints`, `briefcase`, `c-130bay`, `c130`, `canister`, `fence`, `helmet`, `truck`

## Data Sources

- `magicobjconfig.qvm` -- MagicObj type definitions per model sub-part
- `weapons/weapon.qvm` -- Weapon model prefix to weapon name mapping
- `lod.qvm` -- Human-readable model descriptions
- 24 level `objects.qvm` files -- `Task_New` calls mapping prefixes to object types
- `docs/format_objects_qsc.md` lines 1527--1540 -- `ModelLODSettings` confirming LOD system

## See Also

- [MEF Format](format_mef.md) — 3D mesh model binary format
- [objects.qsc](format_objects_qsc.md) — level scene graph with object type declarations
- [Game Structure](game_structure.md) — IGI 2 directory layout and asset pipeline
