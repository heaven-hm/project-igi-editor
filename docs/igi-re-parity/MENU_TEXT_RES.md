# Retail menu labels from the language `.RES` text (task-16)

Status: **complete**. Every number below is reproduced by
`tests/test_menu_text_res.cpp` against the installed game, and every claim about
the engine is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`, a
verified C decompilation of retail `IGI.exe`; `HOOK(0xADDR, Name)` marks the
original address).

Files: `source/menu/menu_text_res.h`, `source/menu/menu_text_res.cpp` (new),
`tests/test_menu_text_res.cpp` (new), this document. `CMakeLists.txt` gains two
lines: the test in the `igi_tests` source list and the module in
`target_sources(igi_tests PRIVATE ...)`. `source/menu/qvm_image.*`, `menu_ui.*`
and `app_menu.cpp` are **not** touched — the resolver is a new module, and the
one-line wiring into the editor is described in §9.

## TL;DR

`MenuManager_GetString` (`src/menu/menumanager.c:832-852`, HOOK 0x418EA0) turns a
label key into the path `LOCAL:<gLanguageNames[language]>/<key>`, looks it up with
`Resource_Find`, and returns `Resource_Load(...)`; on a miss it returns exactly
`"RM: <key>"`. The shipped language archives name their entries exactly that way:

```
LANGUAGE/ENGLISH/menusystem.res   entry  LOCAL:english/Resume Game   ->  "Resume Game"
LANGUAGE/ENGLISH/menusystem.res   entry  LOCAL:english/Restart Game  ->  "Restart Mission"
LANGUAGE/ENGLISH/menusystem.res   entry  LOCAL:english/Single Player ->  "Play Project IGI"
```

so the engine's own path form is the one that matches, and the label text is
frequently **not** the key. Measured on both retail menus:

| menu | labels | resolved | how | fell back | text ≠ key |
|---|---|---|---|---|---|
| `menusystem/ingamemenu.qvm` | 35 | **35** | 35 engine-path, 0 final-component | 0 | 2 |
| `menusystem/MAINMENU.QVM` | 81 | **80** | 80 engine-path, 0 final-component | 1 | 14 |

The single fallback is the key `Geometry High Detail`, which has no entry in any
shipped language archive — the engine itself would render `RM: Geometry High
Detail` for it. The editor's own label `Editor Options` is likewise absent, which
is what makes it a usable negative assertion.

## 1. The oracle chain, end to end

```
MenuText_Draw ... MenuManager_GetString(mm, "Resume Game")
  src/menu/menumanager.c:837   crt_sprintf(path, "LOCAL:%s/%s",
                                            Config_GetLanguageName(mm->language), name);
  src/profile/profile.c:379    Config_GetLanguageName(lang) -> gLanguageNames[lang]
  src/profile/profile.c:419    gLanguageNames[6] = {"ENGLISH","GERMAN","FRENCH",
                                                     "ITALIAN","SPANISH","USA"}
  src/menu/menumanager.c:840   if (Resource_Find(path)) return Resource_Load(path, NULL);
  src/res/resource.c:442-446   Resource_Find (HOOK 0x4B64E0) == Resource_IsLoaded(name, NULL)
  src/res/resource.c:124-147   Resource_IsLoaded (HOOK 0x4B5B90):
                                   next = File_ResolvePath(path, name, next);   // alias expansion
                                   String_ToUpper(upper, path); Path_FixSlashes(upper);
                                   h = Resource_Hash(upper);
                                   compare h and strcmp against every registered resource
  src/menu/menumanager.c:849   miss: crt_sprintf(s->text, "RM: %s", name);   // verbatim
```

What is registered is decided by the archive loader:

```
src/res/resource.c:281-316   Resource_Add (HOOK 0x4B5F80): an "ILFF"/"IRES" file becomes a
                             RES_PACK, then Resource_AddChunks adds its members
src/res/resource.c:328-372   Resource_AddChunks (HOOK 0x4B60F0): for each NAME chunk it takes
                             Chunk_Data(c, form) as the member name; a following CSTR chunk
                             becomes a member with that name
src/res/resource.c:322       Resource_AddMember -> Resource_Create(name, ...)
src/res/resource.c:69-70     Resource_Create: String_ToUpper(r->name, name); Path_FixSlashes(r->name)
```

So the registered name of a CSTR member **is the archive's own stored name,
upper-cased with slashes normalised** — `LOCAL:ENGLISH/RESUME GAME`. That is
exactly what `MenuManager_GetString` builds, case-insensitively, which is why the
lookup succeeds in the retail game. The archives:

```
src/menu/menumanager.c:254-255   MenuManager_LoadLanguageRes(mm, "menusystem.res");
                                 MenuManager_LoadLanguageRes(mm, "missions.res");
src/menu/menumanager.c:298-308   MenuManager_LoadLanguageRes (HOOK 0x417FF0) loads
                                 "LOCAL:language/%s/%s" for every language
src/profile/profile.c:373-376    Config_GetLanguageCount() == CONFIG_LANGUAGE_COUNT (6)
```

which on disk is `LANGUAGE/<LANGNAME>/{menusystem.res,missions.res}` — the engine's
`LOCAL:language/…` path is found because the local device's lookup is
case-insensitive (the install spells the directory `LANGUAGE/`). `Config_Apply`
(`src/profile/profile.c:143-153`, HOOK 0x405B30) additionally sets the alias
`"LANGUAGE:" -> "LOCAL:language/<name>/"`, but `MenuManager_GetString` does not use
that prefix; nothing in the decompiled engine references it, so it is not modelled
here.

## 2. Correction: which archive is the menu system's text

The task brief measured `menusystem/ENGLISH.RES` (16863 B, 146 entries stored as
`LOCAL:menusystem/language/<Key>`). That file is real retail data, but it is **not
what the menu system loads**, and it is not referenced anywhere in the engine:

* The engine loads `LOCAL:language/<lang>/{menusystem.res,missions.res}`
  (`menumanager.c:254-255`), i.e. `LANGUAGE/ENGLISH/menusystem.res` (25592 B, 262
  entries) and `LANGUAGE/ENGLISH/missions.res` (3290 B, 28 entries) — 288 distinct
  keys.
* No string `ENGLISH.RES` or `menusystem/ENGLISH.RES` occurs anywhere in IGI-RE.
* The two files disagree, and the difference is decisive. `menusystem/ENGLISH.RES`
  answers the key with itself (`Single Player` → `Single Player`, `Restart Game` →
  `Restart Game`), while `LANGUAGE/ENGLISH/menusystem.res` answers with the text the
  game actually shows (`Play Project IGI`, `Restart Mission`).
* The naming convention settles it: every one of the six shipped language
  directories names its entries `LOCAL:<language>/<Key>`, matching
  `gLanguageNames` exactly (measured):

| directory | entries | stored prefix | `gLanguageNames` |
|---|---|---|---|
| `LANGUAGE/ENGLISH` | 262 + 28 | `LOCAL:english/` | `ENGLISH` |
| `LANGUAGE/FRENCH` | 261 + 28 | `LOCAL:french/` | `FRENCH` |
| `LANGUAGE/GERMAN` | 261 + 28 | `LOCAL:german/` | `GERMAN` |
| `LANGUAGE/ITALIAN` | 261 + 28 | `LOCAL:italian/` | `ITALIAN` |
| `LANGUAGE/SPANISH` | 261 + 28 | `LOCAL:spanish/` | `SPANISH` |
| `LANGUAGE/USA` | 262 + 28 | `LOCAL:USA/` | `USA` |

`LANGUAGE/USA` even keeps the upper case, because `gLanguageNames[5]` is `"USA"`.
`LOCAL:menusystem/language/<Key>` (the legacy file) matches none of them.

The module therefore loads the per-language archives by default
(`LanguageResources::AddLanguageDirectory`) and keeps the final-component match as
a measured, documented fallback for archives that store a mount path — which is
exactly the legacy file's shape, so the fallback is testable on real data rather
than only on a fixture.

## 3. What the resolver does

`LanguageResources::Lookup(key)`, in order:

1. **Engine path** — normalise `"LOCAL:" + language_name + "/" + key` (ASCII
   upper-case, `\` → `/`, the same two steps `Resource_Create`/`Resource_IsLoaded`
   perform) and match it against the archives' stored names. This is the form the
   engine builds and the form the shipped archives use; `form == EnginePath`.
2. **Final component** — normalise the key's last path component and match that
   against the entries' final components. A deviation, kept because it is what
   makes a mount-path archive usable; reported as `form == FinalComponent` on every
   result so the deviation is measured, never assumed.
3. **Miss** — return exactly `"RM: " + key` with `status == EngineMiss`, mirroring
   `menumanager.c:849-851`.

If no archive could be loaded at all, `Lookup` returns the **raw key** with
`status == NoLanguageData`: that is the editor's behaviour before this module, and
the task requires a missing or unreadable language archive to degrade to it rather
than crash or blank a label. The two cases are deliberately distinct, because the
engine's `RM:` string only means something when there *is* language data to miss
against.

Details that matter:

* **Case and separators.** Matching is case-insensitive and treats `\` as `/`,
  because the engine upper-cases and fixes slashes on both sides
  (`src/res/resource.c:69-70`, `:134-137`). `LOCAL:english/Main Menu` and
  `LOCAL:ENGLISH/MAIN MENU` are the same key.
* **Duplicates.** Retail archives contain a few exactly-duplicated names
  (`LOCAL:english/KEY_SEPERATOR`, `LOCAL:english/Quit Level`); their payloads are
  byte-identical, and no two distinct stored names normalise to the same key with
  different text (measured), so the choice of winner cannot be observed.
* **Encoding.** CSTR payloads are NUL-terminated bytes and are passed through
  unchanged: the non-English archives are Windows-1252, e.g. German `Main Menu` →
  `Hauptmen\xfc`, French `French` → `Fran\xe7ais`. Transcoding for display is the
  renderer's business, not this module's.
* **The field read.** `ResolveMenuLabels` walks a `MenuDefinition` depth-first and
  resolves the `Text Resource` field of every `MenuText` / `MenuTextConditional`
  widget (the declared schema in `docs/igi-re-parity/MENU_SYSTEM.md` §6; the same
  string as `menu_field::kTextResource` in `menu_ui.h`). A widget whose
  `Text Resource` is empty is skipped: the engine would answer `"RM: "` for an
  empty key (`menumanager.c:837-849` builds `LOCAL:ENGLISH/`), but an empty
  `Text Resource` is not a label, and neither retail menu file has one (measured:
  0 of 35 and 0 of 81).
* **Parsing is not duplicated.** The archives are read with `RES_BuildIndex` and
  `RES_ReadEntry` (`source/renderer/res_writer.h`). `RES_BuildIndex` keeps only the
  final path component of each entry (`res_writer.cpp:233-240`) and keeps the
  archive's own spelling, so `AddFile` re-keys its map on the normalised name and
  makes one `RES_ForEachEntry` pass for the full stored names the engine-path form
  needs. No second `.res` parser was written.

## 4. Measured results

```
[menu-text] language ENGLISH, 2 archive(s), 288 distinct keys
[menu-text]   /Users/haseeb-mir/Downloads/IGI/LANGUAGE/ENGLISH/menusystem.res
[menu-text]   /Users/haseeb-mir/Downloads/IGI/LANGUAGE/ENGLISH/missions.res
[menu-text] menusystem/ingamemenu.qvm: 35 labels, 35 resolved (35 engine-path, 0 final-component), 0 fell back (0 miss, 0 no data)
[menu-text]   text differs from key: 'Restart Game' -> 'Restart Mission'
[menu-text]   text differs from key: 'Reset to Default' -> 'Reset to Default Settings'
[menu-text] menusystem/MAINMENU.QVM: 81 labels, 80 resolved (80 engine-path, 0 final-component), 1 fell back (1 miss, 0 no data)
[menu-text]   text differs from key: 'main menu' -> 'Main Menu'
[menu-text]   text differs from key: 'Single Player' -> 'Play Project IGI'
[menu-text]   text differs from key: 'Performance Level' -> 'Graphics detail'
[menu-text]   text differs from key: 'Reset To Default' -> 'Reset to Default Settings'
[menu-text]   text differs from key: 'French' -> 'Fran\xe7ais'
[menu-text]   text differs from key: 'German' -> 'Deutsch'
[menu-text]   text differs from key: 'Italian' -> 'Italiano'
[menu-text]   text differs from key: 'Spanish' -> 'Espa\xf1ol'
[menu-text]   text differs from key: 'Full Size Textures' -> 'Full-Size Textures'
[menu-text]   text differs from key: 'Lightmaps' -> 'Lightmaps Enabled'
[menu-text]   text differs from key: 'Read Me First' -> 'Read Me First!'
[menu-text]   text differs from key: 'Ok' -> 'OK'
[menu-text]   no entry: 'Geometry High Detail'
```

Two consequences worth stating plainly:

* **Resolution is not identity.** 154 of the 262 English `menusystem.res` entries
  have text that differs from their key, and 16 of the 116 labels in the two menus
  change. The labels the player reads are the archive's text
  (`Restart Mission`, `Play Project IGI`, `Graphics detail`), not the key the menu
  script declares.
* **The one miss is the engine's miss.** `Geometry High Detail` is a `Text Resource`
  in `MAINMENU.QVM` and has no entry in any of the six language directories, so the
  retail engine renders `RM: Geometry High Detail`. The module reproduces that
  exactly, which is the point of keeping the miss string verbatim.

The ingame main screen (screen 900) is asserted label by label: its six labels
`Game Paused`, `Resume Game`, `Restart Game`, `Sound`, `Controls`,
`Quit to Main Menu` all resolve, all through the engine path form.

## 5. Verified / not verified

**Verified by test against the retail install**

* The two archives the engine loads, their 288 distinct keys, and the engine-path
  form as the matching form for every one of the 115 resolving labels.
* The exact text of named labels, including the ones that differ from their key.
* The miss path (`"RM: Editor Options"`, `"RM: "` for an empty key) and the
  no-data path (raw key).
* The same convention in the other shipped languages, with their Windows-1252
  bytes preserved (German, French; Spanish/Italian/USA share the code path).
* The legacy `menusystem/ENGLISH.RES` matching only by final component, and its
  text differing from the menu system's — i.e. the correction in §2.
* Both path forms, case/slash insensitivity and the error paths on a synthetic
  archive, and the `ResolveMenuLabels` field/empty-key behaviour on a hand-built
  definition.

**Not verified**

* I did not run retail `IGI.exe`, so the resolution is established from the
  decompiled engine plus the measured archive naming, not from an observed label on
  a running game. The chain in §1 is complete (the registered name is the stored
  name, upper-cased, and the lookup builds the same string), but that is static
  evidence.
* The `"LANGUAGE:"` alias (`profile.c:148`) is set by `Config_Apply` and never
  referenced by the decompiled code; its purpose is outside this reader and is not
  modelled.
* `LANGUAGE/USA` is treated as an ordinary language name (`gLanguageNames[5]`), as
  the archives' own naming implies. Which language a given retail build selects by
  default is a profile setting, and the editor passes the language name in
  explicitly.
* Widget behaviour is untouched: this module resolves label text only.

## 6. Test-output note (found while writing this)

The archives store Windows-1252, and the host runner decodes a suite's stdout as
UTF-8 (`tools/host-tests/host_test_runner.py:341`). A test that prints a payload
byte directly aborts the runner with `UnicodeDecodeError` — which is what happened
the first time this suite ran, on the English text of the `French` key
(`Français`). The test therefore prints and compares archive-derived strings
through an ASCII-escaped form (`Printable()`), so a failure is readable and can
never break the runner. Any other suite that prints retail text should do the same.

## 7. Commands and observed output

```
$ python3 tools/igi-re-parity/tu-check.py source/menu/menu_text_res.cpp tests/test_menu_text_res.cpp
```

needs the build database refreshed first: `build-mingw/compile_commands.json` is
written at CMake configure time and belongs to the build-gate stream, so a file
that was just added to `CMakeLists.txt` has no entry yet (the tool reports
`error: no compile-command entry matches ...`). Until that reconfigure, the same
compiler invocations were replayed directly from the analogous entries already in
the database (`source/menu/menu_ui.cpp`, `tests/test_menu_ui.cpp`) — same flags,
same MinGW cross compiler, `-o`/`-MF` redirected to a private directory, exactly as
`tu-check.py` does:

```
==> source/menu/menu_text_res.cpp  (flags from menu_ui.cpp)   exit=0   [editor target entry]
==> source/menu/menu_text_res.cpp  (flags from menu_ui.cpp)   exit=0   [igi_tests entry]
==> tests/test_menu_text_res.cpp   (flags from test_menu_ui.cpp)  exit=0
```

```
$ IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_menu_text_res.cpp bash scripts/run-host-tests.sh
--- test_menu_text_res  [lazy link]  exit=0  0.53s
    [  PASSED  ] 10 tests.
   PASS  test_menu_text_res                   10 passed    0 failed  (10 tests, exit 0)
   tests passed          : 10
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
   game corpus           : /Users/haseeb-mir/Downloads/IGI
OK: 10 tests passed in 1 suites
==> run-host-tests.sh exit=0
```

```
$ IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS="tests/test_menu_qvm.cpp tests/test_menu_ui.cpp tests/test_menu_text_res.cpp" \
    bash scripts/run-host-tests.sh --show-output
   PASS  test_menu_qvm                        12 passed    0 failed  (12 tests, exit 0)
   PASS  test_menu_text_res                   10 passed    0 failed  (10 tests, exit 0)
   PASS  test_menu_ui                         31 passed    0 failed  (31 tests, exit 0)
   tests passed          : 53
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
OK: 53 tests passed in 3 suites
==> run-host-tests.sh exit=0
```

`--show-output` is what makes the printed inventory in §4 appear; without it the
runner keeps only the gtest summary lines. The two existing suites are unchanged in
behaviour because this change is additive: no file they build or assert on is
modified, and their counts are identical (12 and 31 passed, 0 failed).

## 8. API

```cpp
#include "menu/menu_text_res.h"

igi::menu_text::LanguageResources resources;
std::string error;
// The archives the engine loads for one language (menumanager.c:254-255).
resources.AddLanguageDirectory(game_root, "ENGLISH", error);

igi::menu_text::TextResult r = resources.Lookup("Restart Game");
// r.text   == "Restart Mission"
// r.status == TextStatus::Resolved
// r.form   == LookupForm::EnginePath
```

`LookupCounters` aggregates results (`engine_path`, `final_component`,
`engine_miss`, `no_language_data`, plus `resolved()` / `fallback()`), and
`AddFile` accepts any single archive for callers that want to point at one file.

## 9. Wiring into the editor (not done here — outside this task's write scope)

The editor's menu model is built by `menu_ui.*` and driven by `app_menu.cpp`; both
are owned by other tasks and are untouched. The wiring is one call per label:

```cpp
const std::string label = resources.Resolve(widget->FieldText("Text Resource"));
```

`ResolveMenuLabels(menu, resources)` already does that for a whole
`MenuDefinition`, returning `{widget_id, widget_type, widget_name, key, result}` per
label widget, so a caller that wants the resolved text for its own model does not
need to walk the tree itself. Recommended policy for the caller: use
`result.text`, and treat `status == NoLanguageData` as "no language data
installed" (the raw key is what is shown, and the editor may want to say so once)
rather than as a per-label miss.
