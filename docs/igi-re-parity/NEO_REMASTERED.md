# Neo Remastered mod compatibility (issue #49 part 3)

Issue #49 asks to "verify and extend parsers/asset paths so levels/assets from
the Neo Remastered mod load correctly, not just vanilla IGI1 data".

## Verdict

| | |
| --- | --- |
| The **Neo Remastered mod itself** | **NOT VERIFIABLE HERE.** No asset from it exists on this machine, and there is no IGI-RE oracle for its formats. |
| The **texture/asset path** | **ALREADY HANDLED.** The mod-class v11 texture format `0x43` is in the engine's own switch and in the editor's reader. |
| The **QVM container path** | **NOT PARSEABLE for a non-retail bytecode minor**, with the cause now named. Measured on a third-party level archive that is present. |

---

## 1. What "Neo Remastered" is, and what is not here

The repository's own README identifies it:

* `README.md:84` — "This editor was tested on **Project IGI Neonix Remastered**
  ([Nexus Mods Link](https://www.nexusmods.com/projectigi/mods/5)) … for HD
  Textures, Terrain, and enhanced Gameplay."
* `README.md:316` — "**Upgraded compatibility**: A better upgraded version to
  support the Neo Remastered mod." (the roadmap item this issue tracks)
* `README.md:329` — credits "**[Neo](https://next.nexusmods.com/profile/xaeroneo?gameId=5664)**"
  for guiding and testing the project.

So it is a **third-party mod** — HD textures, terrain and gameplay — not retail
`IGI.exe`. `docs/igi-re-parity/ISSUE_TRIAGE_FULL.md:665-667` already recorded the
consequence, and this document does not contradict it:

> **"Neo Remastered" has no oracle at all** — it is a third-party mod, not retail
> `IGI.exe`, so no IGI-RE claim can be made about its formats.

**No Neo Remastered asset is present on this machine.** A filesystem search for
`*remaster*` / `*neonix*` under `/Users/haseeb-mir` returns exactly one hit,
`Neonix-IGI-Workbench-updated.zip`, which is tooling rather than assets (§2). The
install at `/Users/haseeb-mir/Downloads/IGI` is vanilla IGI1 data plus two
editor-generated directories (`content/`, `cache/`); its 23 `content/textures/
level1/*.tex` files carry the retail `LOOP` v11 header with format `2`, not a mod
format.

**Therefore no claim is made about whether the mod loads.** The two container
rules below are what can be checked without it.

---

## 2. The one mod-author artifact present is an IGI **2** toolchain

`/Users/haseeb-mir/Downloads/Neonix-IGI-Workbench-updated.zip` (extracted read-only
to `scratch/neonix/`) contains `NeonixTools.exe` and its C# source. Its own README
is explicit (`Neonix IGI Workbench/README.md:3-4`):

> A focused frontend for the original `GConv 3.5` toolchain shipped with
> *I.G.I. 2: Covert Strike* …

and lists (README:15) "QVM decompilation and **v7 to v5 conversion**" among its
operations. It is an **IGI 2** toolchain.

It is nevertheless the only description available of the QVM header word the
finding below turns on, so it is cited as *what the mod-author toolchain does*,
never as an engine rule. The engine rules come from IGI-RE.

---

## 3. The QVM container: a non-retail bytecode minor

### 3.1 The engine's rule

```
Qvm_Load   HOOK 0x4BD220   src/qvm/qvm.c:433-486
    if (!file || size < sizeof(QvmHeader) || file[0] != 'L' || file[1] != 'O'
        || file[2] != 'O' || file[3] != 'P' || h->version != 8)
        return NULL;                       // qvm.c:441
    if (h->unk08 < 5) { Resource_Unload(name); return NULL; }   // qvm.c:443-446
```

Two things follow, and both matter:

1. `version` (header offset 0x04) must be **exactly 8**.
2. `unk08` (header offset 0x08) is only tested for **`>= 5`**. Nothing else reads
   it in `Qvm_Load`, and `Qvm_Init` (HOOK 0x4BD430, `src/qvm/qvm.c:521-570`)
   registers **one** opcode table — the one the decompilation numbers
   `OP_BRK = 0 … OP_ILLEGAL = 48`, matching `src/qsc/qvmop.c:502`'s name table.

So a container can carry any `unk08 >= 5`, pass the header check, and then be
decoded with a table that does not describe it.

### 3.2 What the shipping retail data carries

Every retail IGI1 container measured carries `unk08 = 5`. The host test
`ModAssetCompat.EveryRetailContainerCarriesBytecodeMinorFive` asserts it for the
corpus file it opens, and the editor's reader produces no note for them.

### 3.3 What a third-party level archive carries

`/Users/haseeb-mir/Downloads/LEVEL5.zip` is a third-party `LEVEL5/` tree
(102 `.qvm`, 14 `.dat`, 9 `.res`, 5 `.jpg`, 3 `.tex`, 1 `.mtp`, 1 `.mp3`; file
dates 2018-03-02). **It is not established to be the Neo Remastered mod** — the
author is not recorded in the archive — and it is used here only as a measured
example of "not vanilla IGI1 data". Extracted read-only to `scratch/l5/`.

Measured over all 102 `.qvm` files:

| property | retail IGI1 | this archive |
| --- | --- | --- |
| magic | `LOOP` | `LOOP` |
| `version` (0x04) | 8 | 8 |
| `unk08` (0x08) | 5 | **7** (102/102) |
| first code byte (at `code`, header 0x2C) | a callee push | **`0x13`** (102/102) |
| header name-offset table starts at | 0x3C (60) | 0x40 (64) |
| code section decodes with the v5 opcode order | yes | **no — 0/102** |
| code section decodes with the v7 opcode order | no | **yes — 102/102, byte-exact** |

The `0x13` byte is the whole failure: in the retail numbering it is `RET` (19),
for which `Qvm_Init` registers `Qvm_OpError` (`src/qvm/qvm.c:544`), i.e.
`Qvm_OpIllegal` (HOOK 0x4BEC90, `src/qsc/qvmop.c:490-496`) — it sets
`vm->error = QVM_ERR_ILLEGAL` and returns **without advancing `pc`**, and
`Qvm_Execute`'s `while (!vm->error)` (`src/qvm/qvm.c:331`) stops. The walk cannot
start, and `Qvm_Run` prints "Illegal instruction" (`src/qvm/qvm.c:390-392`).

The v7 column is the opcode order the mod-author toolchain selects on the same
word: `backend/NeonixTools/src/Qvm.cs:213-224` reads `verMajor` at offset 4 and
`verMinor` at offset 8 and refuses anything but 5 or 7, and `:46-50`
(`MapOpcode`) maps through `V7Order` when `verMinor == 7`. Under that order
`0x13` is `PUSHIIB` and `0x07` is `CALL`, which is exactly the shape a compiled
script opens with — the retail files open with `PUSHIIB 0; CALL argc; BRA`.

**This is a real incompatibility and it is not guesswork**: all 102 files decode
byte-exactly to the last byte of the code section under one order and none of
them decodes under the other.

### 3.4 What the editor does about it

**Accept/reject is unchanged, because it is the engine's.** `QVM_Parse`
(`source/level/qvm_parser.cpp`) still accepts `version == 8 && unk08 >= 5` and
still fails the decode, exactly as `IGI.exe` would.

**Added:** a non-fatal `QVMFile::warning` when `unk08 != 5`, and that note is
appended to the decode error, so the message names the cause instead of stopping
at "Illegal instruction". Measured before/after on `LEVEL5/mission.qvm`:

```
before: Illegal instruction
after:  Illegal instruction (bytecode minor 7; the engine decodes version-5 bytecode only)
```

**NOT added: a v7 opcode table.** That would be inventing logic. IGI-RE has one
opcode table for IGI1 (`Qvm_Init`, HOOK 0x4BD430, `src/qvm/qvm.c:521-570`, plus
the per-opcode bodies in `src/qsc/qvmop.c`); there is no IGI1 source for a second
order, and the only description of it available here is a third-party IGI **2**
toolchain, which is not an IGI-RE oracle. So the file is diagnosed, not decoded.

---

## 4. The texture path: already handled

The mod-level textures in the same archive are `LOOP` **v11** with `format = 0x43`
at header offset 8 (retail level textures are v11 with format `2`). The engine
converts that format:

```
D3DTex_DownloadTexture  HOOK 0x50AD10   src/sysmisc/d3dtex.c:411-432
    switch (d->format) {
    case 1:    Pixel_8To32(...);    break;
    case 2:    Pixel_1555To32(...); break;
    case 3: case 4: case 0x43: Pixel_Copy32(...); break;
    case 5:    Pixel_565To32(...);  break;
    default:   Error_Show("Unsupported texture format"); for (;;) ;
    }
```

`0x43` goes through `Pixel_Copy32` (src/terrdraw/pixel.c:112-124), so it is
**4 bytes per pixel**. The editor's `DepthForMode`
(`source/renderer/tex_parser.cpp:212-222`) already maps `0x43` to 4 and the v11
reader already walks the mip chain with `pitch = bytesPerPixel * unk1A`
(`src/file/texture.c:242`).

**No change was needed here**; what was missing was a test that pins it, which
§5 adds. The `dat`/`mtp` files in the same archive are byte-for-byte the retail
IGI1 formats (`*** This file is machine generated` header; `FORM`/`MTP BANM`
chunks), so they are not a difference either.

---

## 5. What was implemented

| file | change |
| --- | --- |
| `source/level/qvm_parser.h` | `QVMFile::warning` — a non-fatal container note |
| `source/level/qvm_parser.cpp` | set it when `unk08 != 5`; append it to the illegal-instruction error, and name the opcode for a bare `RET` opening |
| `tests/test_mod_asset_compat.cpp` | NEW — 9 tests: the retail minor, acceptance + warning for another minor, the decode being independent of the header word, the named error, the still-refused `< 5` case, `DepthForMode(0x43) == 4`, a v11 format-0x43 texture, an unknown format refused, and a real corpus texture |
| `CMakeLists.txt` | the new test file registered |

The tests that need a corpus file self-skip when `IGI_GAME_PATH` is unset, which
is the lane's existing convention.

---

## 6. What remains unverified

* **The Neo Remastered mod itself.** No asset from it is on this machine, so
  whether its levels and textures load is **not tested**. This is the honest
  answer to the issue's acceptance criterion "Neo Remastered levels/assets load
  without parser errors": it cannot be evaluated here.
* **Which mod `LEVEL5.zip` is.** Its author and its relationship to the Neo
  Remastered mod are unknown. It is reported as a third-party level archive, not
  as that mod.
* **Whether `unk08` is a bytecode minor in *IGI1*.** IGI-RE shows only that
  `Qvm_Load` requires `>= 5` and that `Qvm_Init` registers one table; it does not
  name the field. The measured correlation (5 ⇔ v5 bytecode, 7 ⇔ v7 bytecode,
  102/102 and 13/13) and the mod-author toolchain both point at it, but the
  editor does **not** act on the interpretation beyond reporting it.
* **Decoding a v7 container.** Not implemented, for the reason in §3.4.
* **Any other asset path a mod might change.** Only `.qvm`, `.tex`, `.dat` and
  `.mtp` were examined, and only from the one archive.

---

## 7. Reproducing the measurements

```bash
# every third-party container: version, bytecode minor, first code byte
python3 - <<'PY'
import glob, struct, collections
files = sorted(glob.glob('scratch/l5/LEVEL5/**/*.qvm', recursive=True))
v, u, f = collections.Counter(), collections.Counter(), collections.Counter()
for p in files:
    d = open(p, 'rb').read()
    if len(d) < 60 or d[:4] != b'LOOP': continue
    v[struct.unpack_from('<i', d, 4)[0]] += 1
    u[struct.unpack_from('<i', d, 8)[0]] += 1
    f[d[struct.unpack_from('<i', d, 0x2C)[0]]] += 1
print(len(files), dict(v), dict(u), {hex(k): n for k, n in f.items()})
PY

# the editor's verdict on the same files (needs the test binary or a probe)
IGI_GAME_PATH=/path/to/IGI bash scripts/run-host-tests.sh
```

`scratch/l5/` and `scratch/neonix/` are throwaway extractions; the archives
themselves were never modified.
