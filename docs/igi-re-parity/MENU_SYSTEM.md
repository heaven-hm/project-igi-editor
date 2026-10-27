# The retail menu system as data — `source/menu/qvm_image.*` (task-13)

Status: **complete** for the two retail menu containers. Every number and rule below
is reproduced by `tests/test_menu_qvm.cpp` on the real files, and every claim about
the engine is cited to IGI-RE.

Scope of change: `source/menu/qvm_image.cpp` (the reader),
`tests/test_menu_qvm.cpp` (12 host tests), `CMakeLists.txt` (two registrations:
`tests/test_menu_qvm.cpp` in the `igi_tests` sources, `source/menu/qvm_image.cpp` in
`target_sources(igi_tests PRIVATE)`). `source/menu/qvm_image.h` is the interface,
written by the lead; it is not modified here. Container parsing is **not**
reimplemented — `QVM_Parse` (`source/level/qvm_parser.h`) does it, and this reader
only walks the `QVMFile` it returns.

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE`, a verified C decompilation of
retail `IGI.exe`. `HOOK(0xADDR, Name)` marks the original address.

## TL;DR

`menusystem/ingamemenu.qvm` and `menusystem/MAINMENU.QVM` are `LOOP` 8.5
containers holding *a script whose top-level statements are `Task_DeclareParameters`
and a single `Task_New`*. The menu definition is the argument tree of that
`Task_New`: argument 0/1/2 are the task id, type and name, the arguments after them
are the declared parameters, and the arguments after **those** that are themselves
`Task_New` calls are the widget's children. The reader decodes that tree:

| | `ingamemenu.qvm` | `MAINMENU.QVM` |
|---|---|---|
| file bytes | 11308 | 25549 |
| name table / strings | 4 / 139 | 4 / 249 |
| decoded instructions | 2225 | 5014 |
| `CALL` instructions | 90 | 206 |
| top-level statements | 10 (9 declare + 1 `Task_New`) | 16 (15 declare + 1 `Task_New`) |
| argument blocks (one per argument) | 980 | 2207 |
| nested `Task_New` calls | 80 | 190 |
| **widgets recovered** | **81** | **191** |
| widget types | 10 | 16 |
| widgets with an id ≠ -1 | 17 | 36 |
| decode problems | 0 | 0 |

Two input paths produce the same model: the container (`FromQvmFile`/`FromQvm`) and
`QVM_DecompileToString` text (`FromDecompiledSource`). The test suite **proves they
agree on both retail files**, widget for widget, field for field.

## 1. The container

`QVM_Parse` handles the header and the three tables; its fidelity on these files was
checked separately (name table == NUL split, string table == NUL split with 139/249
entries and strictly increasing offsets, `Σ(len+1) == sz_svalue`,
`ver_major == 8`, `ver_minor == 5`). The engine's loader is
`src/qvm/qvm.c:433-486` `Qvm_Load` 0x4BD220 (signature `"LOOP"`, version 8, `unk08 >= 5`).

| header field | `ingamemenu.qvm` | `MAINMENU.QVM` |
|---|---|---|
| `of_itable` / `sz_itable` | 60 / 16 | 60 / 16 |
| `of_stable` / `sz_stable` | 119 / 556 | 119 / 996 |
| `of_ivalue` / `sz_ivalue` | 76 / 43 | 76 / 43 |
| `of_svalue` / `sz_svalue` | 675 / 2773 | 1115 / 6656 |
| `of_ctable` / `sz_ctable` | 3448 / 7860 | 7771 / 17778 |
| identifiers, in order | `Task_DeclareParameters`, `Task_New`, `FALSE`, `TRUE` | `Task_DeclareParameters`, `Task_New`, `TRUE`, `FALSE` |

The identifier order differs between the two files (`FALSE`/`TRUE` are swapped), so a
reader that hard-coded an index would be wrong for one of them. The names come from
the table; `QVM_Parse` already resolves them.

## 2. A statement is `CALL` + `argPc` + `BRA` + one `BRK` block per argument

The engine's call encoding is `src/qvm/qvm.c:641-689` `Qvm_OpCall` 0x4BD880:

```
code[pc] == OP_CALL
argc      = *(int32*)(code + pc + 1)
argPc[i]  = *(int32*)(code + pc + 5 + 4*i)   // i = 0 .. argc-1
// the interpreter pushes argc and returns to pc + argc*4 + 5
```

The compiler emits, for every call statement (`src/qvm/qscgen.c:296-345`):

```
PUSHIIB <callee>      ; the function name, immediately before the CALL
CALL(argc, argPc...)  ; one code offset per argument
BRA <end of args>     ; skips the argument code
<arg 0 code> BRK
<arg 1 code> BRK
...
```

so the code that *evaluates* argument *i* is at `argPc[i]`, is contiguous, and ends
at a `BRK`. `QVM_Parse` decodes the whole code section linearly
(`source/level/qvm_parser.cpp:202-227`), which gives this reader both the operand
sizes and `QVMInstruction::call_targets` for free — the `argPc` array, already
bounds-checked.

The reader follows the interpreter: at a `CALL` it takes the callee from the
instruction before it, requires the `BRA` after the argument table, checks that each
`argPc[i]` equals the end of the previous argument's block (contiguity) and that the
`BRA` target equals the end of the last block, then continues at that target. Any
disagreement is reported with the code address instead of being worked around.

## 3. Operand layouts, pinned to the retail bytes

`tests/test_menu_qvm.cpp:OperandLayoutsPinnedByTheRetailBytes` reads
`menusystem/ingamemenu.qvm` and checks the following offsets (relative to
`of_ctable = 3448`). These are the checks that would catch a wrong operand size:

| offset | bytes | meaning |
|---|---|---|
| `0` | `0d 00` | `PUSHIIB` + index 0 → `identifiers[0]` = `Task_DeclareParameters`, 2 bytes |
| `2` | `18` | `CALL`, 1 byte |
| `3` | `07 00 00 00` | `argc = 7` |
| `7…34` | `28,2b,2e,31,34,37,3a` | `argPc` = 40, 43, 46, 49, 52, 55, 58 |
| `40,43,…,58` | `09 <idx> 00` | each argument: `PUSHSIB` + string index + `BRK`, 3 bytes |
| `35` | `14` | `BRA`, 5 bytes |
| `36` | `15 00 00 00` | displacement 21 = `61 − (35 + 5)`, i.e. `35+5+21 = 61` = end of the last argument |
| `61` | `12` | `POP` (1 byte) — the statement's value is dropped |
| `62` | `0d` | the next statement begins |
| `936` | `0d 01` | `PUSHIIB` + index 1 → `identifiers[1]` = `Task_New` |
| `938` | `18` | `CALL`; `argc = 14` at `939` |
| `1004` | `10 2c 00` | argument 0 of the root: `PUSH1`, `MINUS`, `BRK` → the integer **-1** |
| `1007` | `09 00 00` | argument 1: `PUSHSIB 0` → `strings[0]` = `MenuManager` |
| `1020` | `0d 02 00` | argument 5: `PUSHIIB 2` → `identifiers[2]` = `FALSE` (a Boolean value) |

`-1` is therefore encoded as `PUSH1` + `MINUS`; `PUSHM` never appears in either menu
file. The decompiler prints the same thing as `-1`
(`source/level/qvm_decompiler.cpp:295-342`).

## 4. The argument shapes that actually occur

A census over both files (3,187 argument blocks: 980 + 2,207) finds **exactly eight
shapes and zero anomalies**. A block produces one value; a push adds a value, a `CALL`
consumes its callee push and leaves its result:

| block shape | `ingamemenu.qvm` | `MAINMENU.QVM` |
|---|---|---|
| `PUSHSIB <string index>` | 507 | 1087 |
| `PUSHB <int8>` | 156 | 360 |
| `PUSHW <int16>` | 102 | 276 |
| `PUSHIIB <name> CALL` (nested `Task_New`) | 80 | 190 |
| `PUSH1 MINUS` (the integer −1) | 64 | 155 |
| `PUSHIIB <name>` (`TRUE`/`FALSE`) | 38 | 83 |
| `PUSH1` | 19 | 42 |
| `PUSH0` | 14 | 14 |
| **total blocks** | **980** | **2207** |

Nothing else occurs: no `PUSHM`, `PUSHF`, `PUSHA`, `PUSHSI`, `PUSHSIW`, `PUSHIIW`,
no unary operator other than `MINUS`, no `INV`/`NOT`, no nested call with extra
values around it, and no call in a declared parameter position (§5). A script that
computed an argument (arithmetic, comparison, string concatenation) would be refused
with a problem naming the opcode and address rather than mis-decoded; the retail menu
scripts do not contain such code.

The 80/190 nested calls are exactly the number of children (81 − 1 and 191 − 1
widgets): every widget below the root is one argument of its parent.

## 5. The widget tree is the nesting of `Task_New` arguments

`src/level/level.c:168-246` `Level_NewTask` 0x4F0780 owns the semantics:

* `:187-190` — argument 0 is the task id, argument 1 the type name, argument 2 the
  instance name.
* `:214-221` — the arguments after those are parsed in the type's `ParamTable` order
  (`Param_GetTableEvent`), which is why the file's `Task_DeclareParameters` list (in
  `ParamTable` order, §7) gives each widget's parameter names.
* `:182-185`, `:209` — while parameters are parsed the `Task_New` symbol is disabled
  (`gLevelTaskNewEnabled = 0`), so a nested `Task_New` in a parameter position is an
  error. The reader reports it too; **neither retail file contains one** (checked for
  every widget).
* `:234-239` — once the parameters are parsed, the new task becomes the `Task_New`
  symbol context and the *remaining* arguments are evaluated. A `Task_New` there runs
  normally and creates a child of the new task. **That is the nesting rule.** A
  literal in an extra position is evaluated and discarded; it does not affect the
  tree.

The rules the reader applies, in order:

1. `Task_DeclareParameters(typeName, field1, type1, field2, type2, …)` statements
   (odd argument count) define the parameter list of each widget type, in file order.
   The runtime's `Task_DeclareParameters` is a **no-op returning 0**
   (`src/level/level.c:156-166`), so this list is data the script carries for its own
   reader, not something the engine consumes; its *order* is still the order
   `Level_NewTask` parses in (`:214-221`) and it matches the C `ParamTable` (§7).
2. The first top-level `Task_New` is the root; every other `Task_New` anywhere is a
   widget (that is `widget_count`).
3. For a widget, arguments `0/1/2` are id/type/name; the next *N* arguments are the
   *N* declared parameters (`MenuField::values[0]`); every argument after those that
   is a `Task_New` call is a child, in order. A non-`Task_New` call, or a call inside
   the declared parameters, is an error.
4. A type with no declaration has no parameters — `ControlsMenu` is the real case
   (§7): `Task_New(954, "ControlsMenu", "")` has no fields and no children.

Example, `ingamemenu.qvm`, screen 900 (all of this is asserted in the tests):

```
Task_New(-1, "MenuManager", "Menu manager for the ingame menu", 900,
         "LOCAL:menusystem/ingamemenu.res", FALSE,
    Task_New(900, "MenuScreen", "Main Menu", "", 85, 80, 555, 410,
             "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", "", "",
        Task_New(-1, "MenuFrame", "", "", 100, 77, 440, 26, 0, 4, 4, FALSE,
            Task_New(-1, "MenuText", "", "Game Paused", "font3.fnt", 1, "", "")),
        Task_New(-1, "MenuFrame", "", "", 100, 97, 440, 200, 1, 4, 4, FALSE,
            Task_New(-1, "MenuTextConditional", "resume", "Resume Game", "font3.fnt", 2,
                     "!LevelFlow_LevelFailed()", "TRUE",
                     "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", ""),
            Task_New(-1, "MenuText", "restart", "Restart Game", "font3.fnt", 2,
                     "MenuManager_ActivatePopupScreen(932)\n", ""),
            Task_New(-1, "MenuText", "sound", "Sound", "font3.fnt", 2,
                     "MenuManager_PushScreen(902)\n", ""),
            Task_New(-1, "MenuText", "controls", "Controls", "font3.fnt", 2,
                     "MenuManager_PushScreen(903)\n", ""),
            Task_New(-1, "MenuText", "quit", "Quit to Main Menu", "font3.fnt", 2,
                     "MenuManager_ActivatePopupScreen(931)\n", ""))),
    Task_New(901, "MenuScreen", "Graphics Configuration", ...), ...)
```

(`\n` inside a string is a real newline in the file; the decompiler prints it escaped.)
The frame and item nesting is not assumed: it is what the argument structure says.
A flat reading (all widgets as children of the root) would put 81 widgets at depth 2
and would lose the frames' ownership of their items; the file has 8 root children,
and the five items of the main menu are at depth 3.

## 6. What the two retail files contain

### `menusystem/ingamemenu.qvm` — 81 widgets

```
MenuManager 1   MenuScreen 4   MenuFrame 28   MenuText 34   MenuTextConditional 1
ListBox 2       SlideBar 5     ToggleBox 1    ControlsMenu 1   DialogWindow 4
```

Root: `id -1`, `"Menu manager for the ingame menu"`, `Current menuscreen = 900`,
`Resource path = "LOCAL:menusystem/ingamemenu.res"`, `isOwnDisplay = FALSE`.
Its eight children: `MenuScreen` 900 "Main Menu", 901 "Graphics Configuration",
902 "Sound Configuration", 903 "Controls Configuration", and `DialogWindow`
930 "Reset Controls", 931 "quit to main", 932 "restart mission", 933 "".

The 17 widgets that carry an id (all unique):

| id | type | name |
|---|---|---|
| 900 | MenuScreen | Main Menu |
| 901 | MenuScreen | Graphics Configuration |
| 902 | MenuScreen | Sound Configuration |
| 903 | MenuScreen | Controls Configuration |
| 930 | DialogWindow | Reset Controls |
| 931 | DialogWindow | quit to main |
| 932 | DialogWindow | restart mission |
| 933 | DialogWindow | (empty) |
| 950, 951 | ListBox | (empty) |
| 952, 953, 955, 956, 958 | SlideBar | (empty) |
| 954 | ControlsMenu | (empty) |
| 957 | ToggleBox | (empty) |

Declared parameter lists (9 types; the file's own `Task_DeclareParameters`):

| type | n | parameters (name : type) |
|---|---|---|
| MenuManager | 3 | Current menuscreen : Int32, Resource path : String256, isOwnDisplay : bool8 |
| MenuFrame | 9 | Sprite Resource : String32, X-Pos : Int32, Y-Pos : Int32, Width : Int32, Height : Int32, LineUpMode : Int32, X-Spacing : Int16, Y-Spacing : Int16, Is Selectable : bool8 |
| MenuScreen | 8 | Background : String32, Frame x0/y0/x1/y1 : Int32, On Press Escape : VarString, On Init Data : VarString, On Change Data : VarString |
| MenuText | 5 | Text Resource : String32, Font Resource : String32, ColourIndex : Int32, On Click Script : VarString, Click Sound Resource : String32 |
| SlideBar | 8 | Sprite Resource : String32, Horizontal : bool8, Size : Int32, Get Data Script : VarString, Set Data Script : VarString, Modify Data Script : VarString, Carret Speed : VarString, Number of options : Int32 |
| MenuTextConditional | 7 | Text Resource : String32, Font Resource : String32, ColourIndex : Int32, isEnabled Expression : VarString, isVisible Expression : VarString, On Click Script : VarString, Click Sound Resource : String32 |
| ListBox | 7 | Sprite Resource : String32, Font Resource : String32, Width : Int32, Collect Items Script : VarString, Get Data Script : VarString, Set Data Script : VarString, Modify Data Script : VarString |
| ToggleBox | 2 | Get Data Script : VarString, Set Data Script : VarString |
| DialogWindow | 5 | X-Pos : Int32, Y-Pos : Int32, Width : Int32, Height : Int32, SwapKeys : bool8 |

`ControlsMenu` is **not** declared by either file; see §7.

### `menusystem/MAINMENU.QVM` — 191 widgets

```
MenuManager 1   MenuScreen 12  MenuFrame 68  MenuText 79  MenuTextConditional 2
ListBox 3       SlideBar 5     ToggleBox 5   ControlsMenu 1  PictureBox 2
InputBox 3      AsciiBox 1     TextBox 1     ScrollListBox 1  TypeWriterBox 1
DialogWindow 6
```

Root: `id -1`, `"Menu manager for the main menus"`,
`Resource path = "LOCAL:menusystem/menusystem.res"`, `isOwnDisplay = TRUE`.
Screens, in file order: 900 "Main Menu", 904 "Configuration", 905 "Graphics
Configuration", 906 "Sound Configuration", 907 "Controls Configuration", 910 "language
select screen", 911 "Advanced Graphic Options", 912 "Player Config.", 913 "Readme
screen", 914 "Select Level ", 915 "Content Control", 916 "Credits Screen".
Dialog windows: 930 "Delete Player Popup", 931 "Reset Controls", 932 "Enter Password",
933 "Select Difficulty", 934 "Quit Main Menu", 935 "Reset Controls".
36 widgets carry a unique id (the 12 screens, 6 dialog windows and 18 list/slider/
toggle/input/scroll widgets with ids 950-955, 957-960 and 962-969).

It declares 15 types — the nine above plus `InputBox` 6, `AsciiBox` 4,
`ScrollListBox` 7, `PictureBox` 4, `TextBox` 5, `TypeWriterBox` 2.

Labels recovered from the `Text Resource` parameters include `Single Player`, `Quit`,
`Configuration`, `Read Me`, `Credits`, `Graphics`, `Sound`, `Controls`, `Language`,
`Content Control`, `OK`, `Cancel`, `Music Volume`, `FX Volume`.

## 7. Cross-check: the declared parameters against the engine's C `ParamTable`

This is what turns "the file says so" into "the engine agrees". Each widget type
registers a `ParamTable` in C; the file's `Task_DeclareParameters` list is exactly the
entries whose flag has bit 1 set (flag 7 = declared, flag 6 = not), **in table order**,
and the file's type names match the C `Param_*` types.

| type | C site | table size | declared in file |
|---|---|---|---|
| MenuManager | `src/game/menumanager.c:89-96` | 5 (flags 6,7,7,6,7) | 3 — `screen` Int32, `resPath` String256, `fastFade` Bool8 |
| MenuScreen | `src/menuscreen/menuscreen.c:58-66` | 8 (all 7) | 8 |
| MenuText | `src/menuscreen/menutext.c:295-300` | 5 | 5 |
| MenuTextConditional | `src/menuscreen/menutext.c:71-78` | 7 | 7 |
| MenuFrame | `src/widget/menuframe.c:48-57` | 9 | 9 |
| SlideBar | `src/menuscreen/slidebar.c:71-79` | 8 | 8 |
| ListBox | `src/menuscreen/listbox.c:71-78` | 7 | 7 |
| ToggleBox | `src/ui/togglebox.c:36-38` | 2 | 2 |
| DialogWindow | `src/menu/dialogwindow.c:56-61` | 5 | 5 |
| ControlsMenu | `src/widget/controlsmenu.c:80-101` | **none registered** | 0 — `Task_New(954, "ControlsMenu", "")` |

`ControlsMenu_Init` (`src/widget/controlsmenu.c:81-101`) creates the type and its
handlers but never calls `ParamTable_New`/`ParamTable_Add`, so the type has no
parameters at all — which is exactly what the file shows: three arguments and no
fields, and no children. The six MAINMENU-only types are likewise registered types in
IGI-RE (`src/ui/inputbox.c:53`, `src/ui/asciibox.c:52`, `src/ui/scrolllistbox.c:64`,
`src/ui/picturebox.c:43`, `src/menu/textbox.c:32`, `src/menu/typewriterbox.c:72`), and
their declared counts match their tables.

The `MenuManager` row is the sharpest evidence: four of its five entries have flag 6,
and the three flag-7 entries (`screen`, `resPath`, `fastFade`) are the three
parameters both files declare, in the same order and with matching types.

## 8. The second entry point — decompiled text

`MenuDefinition::FromDecompiledSource` reads what `QVM_DecompileToString` prints
(`source/level/qvm_decompiler.cpp:159-183`, `:295-342`, `:556-566`): one
`Callee(arg, arg, ...);` statement per top-level call, a nested call argument on its
own indented line, strings quoted with `\n` and `"` escaped
(`source/level/qvm_decompiler.cpp:36-44`), integers and floats in decimal, identifiers
bare. The reader accepts exactly that and refuses everything else — `if`/`while`
blocks, binary operators, `{}` — with an error naming the construct and the offset,
because a menu script has no such statement and guessing at one would be inventing
structure.

It is **verified against the real decompiler output**, not just a fixture: the test
`DecompiledSourceAndContainerPathsAgree` decompiles both retail files and asserts the
two models are identical — same widget count, and for every widget the same id, type,
name, field names/types, value kinds, value text and child count. (Both the decompiler
and `source/logger.cpp` do compile and link in the host lane, so this runs on
`bash scripts/run-host-tests.sh` without any platform guard.) Note the escaping
asymmetry that makes this work: the printer escapes `\n` and `"` but **not** `\`, so a
string containing a backslash would not round-trip; neither retail menu file has one
(checked: 0 strings with `\` or `"`, 14 and 28 with `\n`).

The alternative — feeding the text to the repo's own `qsc::Parse` + `qvm::Compile`
(`source/level/qsc_parser.h`, `source/level/qvm_compiler.h`) and walking the emitted
container — was rejected because `QVM_Parse` is path-only
(`source/level/qvm_parser.h:53`), so it would need a temporary file round trip on
every load. If a future change adds an in-memory `QVM_Parse`, that path becomes the
cheaper one and this reader can be deleted.

## 9. What is recovered, and what is not

**Recovered (verified against both files):** the container's tables; the statement
encoding (`CALL`/`argPc`/`BRA`/`BRK`); every operand size the reader uses; the eight
argument shapes and their counts; the declared parameter names, types and order for
every type in both files, cross-checked against the C `ParamTable`s; the widget tree
with ids, types, names, parameter values and children; all 81 and 191 widgets; the
root parameters and resource paths; the screen ids and names; the widget type census;
and the equality of the two entry points on real data.

**Not recovered, deliberately:**

* *Widget behaviour.* What a click does lives in the script text of the callback
  parameters (`On Click Script`, `isEnabled Expression`, …); the reader carries that
  text verbatim and does not interpret it.
* *Anything computed.* Only the literal and unary forms in §4 are evaluated. A
  computed argument is rejected with a problem rather than approximated; no retail
  menu file contains one.
* *`MenuValueKind::Call`.* The interface has the kind, but the only way to produce it
  is a nested call in a declared parameter position, which `Level_NewTask` rejects
  (`src/level/level.c:209`) and the reader reports as an error. So it is a diagnostic
  kind, not something a caller normally observes. (Reported to the lead as an
  interface question, not silently worked around.)

**Verified separately, not a defect here:** `QVM_OperandSize`
(`source/level/qvm_parser.cpp:8-31`) gives `BLK` and `JSR` one byte, while the
interpreter reads `BLK` as `pc += 5 + read32(pc+1)` (`src/qvm/qvm.c:340`). The
assembler emits them as one byte (`src/qasm/asm.c:44-61`), so the parser matches the
assembler; and none of `PUSHA`, `BLK` or `JSR` occurs in **any** of the 1043 retail
`.qvm` files, so it cannot affect this reader. It is reported to the lead because the
shared parser has other call sites.

## 10. Commands and observed output

```
$ python3 tools/igi-re-parity/tu-check.py source/menu/qvm_image.cpp
==> source/menu/qvm_image.cpp
OK    (source/menu/qvm_image.cpp)
==> source/menu/qvm_image.cpp
OK    (source/menu/qvm_image.cpp)

checked 2 translation unit(s), 0 failed          (exit 0)
```

```
$ IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_menu_qvm.cpp bash scripts/run-host-tests.sh
--- test_menu_qvm  [lazy link]  exit=0  0.72s
    [  PASSED  ] 12 tests.

 HOST TEST SUMMARY
   PASS  test_menu_qvm                        12 passed    0 failed  (12 tests, exit 0)
   suites not host-portable (compile): 0
   suites not host-portable (link)   : 0
   suites not host-portable (runtime unresolved symbol): 0
   game corpus           : /Users/haseeb-mir/Downloads/IGI
OK: 12 tests passed in 1 suites
==> run-host-tests.sh exit=0
```

`MenuQvm.PrintRecoveredInventory` prints the inventory it recovered, so a failing
corpus shows its own numbers:

```
[menu-qvm] ingamemenu.qvm: 139 strings, 4 names, 2225 instructions, 90 calls, 81 widgets
[menu-qvm]   root: id -1 'Menu manager for the ingame menu' resource 'LOCAL:menusystem/ingamemenu.res'
[menu-qvm]   screens: 900 'Main Menu'  901 'Graphics Configuration'  902 'Sound Configuration'  903 'Controls Configuration'
[menu-qvm]   widgets: ControlsMenu=1 DialogWindow=4 ListBox=2 MenuFrame=28 MenuManager=1 MenuScreen=4
            MenuText=34 MenuTextConditional=1 SlideBar=5 ToggleBox=1
[menu-qvm] MAINMENU.QVM: 249 strings, 4 names, 5014 instructions, 206 calls, 191 widgets
[menu-qvm]   root: id -1 'Menu manager for the main menus' resource 'LOCAL:menusystem/menusystem.res'
[menu-qvm]   screens: 900 'Main Menu'  904 'Configuration'  905 'Graphics Configuration'  ...
[menu-qvm]   widgets: AsciiBox=1 ControlsMenu=1 DialogWindow=6 InputBox=3 ListBox=3 MenuFrame=68
            MenuManager=1 MenuScreen=12 MenuText=79 MenuTextConditional=2 PictureBox=2 ScrollListBox=1
            SlideBar=5 TextBox=1 ToggleBox=5 TypeWriterBox=1
```

The corpus is `$IGI_GAME_PATH` (`scripts/run-host-tests.sh` exports it, default
`/Users/haseeb-mir/Downloads/IGI`); the corpus tests skip cleanly when
`menusystem/ingamemenu.qvm` or `menusystem/MAINMENU.QVM` is absent.

## 11. The write side — `source/menu/menu_qvm_writer.*` (issue #74)

The reader above turns a container into `MenuDefinition`; the writer turns an edited
`MenuDefinition` back into the container. It adds no format of its own: it builds the
QSC syntax tree the retail scripts have and hands it to `qvm::Compile`
(`source/level/qvm_compiler.cpp`), which is this repository's translation of the
engine's last two producer stages.

| stage | engine | where |
| --- | --- | --- |
| QSC -> assembler text | `QscGen_Compile` HOOK 0x4BBCB0 | `src/qvm/qscgen.c:86-142`; statements at `:199-272`, calls at `:322-345`, the single trailing `BRK` at `:134` |
| text -> bytes | `QvmAsm_OpPush` HOOK 0x4C0A10 | `src/qasm/asm.c:74-124`; pool indices from `Asm_AddString`/`Asm_AddName` HOOK 0x4BB030/0x4BB180 (`src/script/asm.c:138-157`, `:183-200`) |
| bytes + tables -> "LOOP" | `QvmAsm_Assemble` HOOK 0x4BB270 | `src/qvm/qvmasm.c:103-276`, header at `:227-251` |
| the loader that must accept it | `Qvm_Load` HOOK 0x4BD220 | `src/qvm/qvm.c:433-486` — "LOOP", version 8, `unk08 >= 5` |

Two rules the writer inherits from the engine and that an editor edit can disturb:

* **The argument layout is the file's, not the tree's.** `Task_New(id, type, name,
  <declared parameters in ParamTable order>, <children>)` is what `Level_NewTask`
  HOOK 0x4F0780 parses (`src/level/level.c:187-190`, `:214-221`, `:234-239`), and the
  declared-parameter list comes from the file's own `Task_DeclareParameters`
  statements. `MenuDefinition::declarations` therefore carries those statements in
  file order; without them a save reorders the string pool (measured: same 11308
  bytes, first difference at offset 151).
* **A negative literal is a magnitude push plus `MINUS`.** The QSC parser never hands
  `QscGen_WriteInt` (HOOK 0x4BC880, `src/qvm/qscgen.c:441-448`) a negative value —
  measured over the installed corpus: 41497 `MINUS` opcodes, each immediately preceded
  by a magnitude push, and `PUSHM` (which `QvmAsm_OpPush` emits only for a literal
  -1, `src/qasm/asm.c:79-83`) exactly 0 times. The writer therefore emits unary minus
  on the magnitude, and applies `QvmAsm_OpPush`'s PUSH0/PUSH1 shortening to that
  magnitude as well, so `-1.0` is `PUSH1 + MINUS` exactly as the engine's compiler
  would emit it.

`MenuQvmDocumentFromDefinition` uses `MenuDefinition::declarations` verbatim, so the
ordinary save is byte-faithful; when a definition was built by hand and carries no
declaration list, the writer recovers one from the widgets' own fields in depth-first
first-use order (complete for both retail files, but not the file's statement order,
so the bytes then differ inside the string pool).

`MenuQvmWrite` refuses, with a message, a document the engine could not have produced:
a widget whose field count disagrees with its declaration, a declared parameter with
no value or more than one, a call in a parameter position (`Level_NewTask` disables
the `Task_New` symbol while it parses parameters, `src/level/level.c:182-185`, `:209`),
an undeclared type carrying fields, a duplicate declaration, a `bool8` that is neither
`TRUE` nor `FALSE`, and an integer whose magnitude does not fit the assembler's 32-bit
literal.

The editor's entry point is `app_menu::SaveEditorMenu` (`source/menu/menu_ui.h`,
implemented in `source/app_menu.cpp`): it writes `menu.definition` to the given path,
or to the definition's own `source` when the path is empty, and refuses a menu that
was not built from a retail container — a fallback menu can never overwrite
`ingamemenu.qvm`.

**The write side is live.** The editor's pause menu is the loaded tree
(`source/app_pause_menu.cpp`, `source/renderer/renderer_draw.cpp`), and its
`Menu Text` and `Save Menu` rows are `app_menu::EditEditorMenuText` (an edit through
`MenuDefinition`'s mutable accessors, then a model rebuild) and
`app_menu::SaveEditorMenu`. A Wine run of the i686 editor clicked both rows and the
container on disk came back with the edit in it (11307 bytes, `IGI EDITOR` present,
`Game Paused` gone); see `MENU_UI.md` "The live path, verified under Wine".

### What is verified

`tests/test_menu_qvm.cpp` (suite `MenuQvmWrite`, 8 tests) and `tests/test_menu_ui.cpp`
(suite `MenuUiSave`, 3 tests) hold the writer to the shipped files:

* both retail containers round-trip **byte for byte** (11308 and 25549 bytes, zero
  differing bytes);
* the written container satisfies the loader's rules and `QvmAsm_Assemble`'s layout
  (`symOffsets == 60`, every section abutting the next, `code + codeSize == size`);
* it decodes, through the repository's own reader, to the shipped file's structure
  field for field — including screen 900's authored geometry (85, 80 -> 555, 410;
  title frame 100, 77, 440x26 LineUpMode 0; item frame 100, 97, 440x200 LineUpMode 1);
* an edit (a `Text Resource` and a `LineUpMode`) survives save + reload, with the
  widget count and ids unchanged and the replaced game text gone from the container;
* a hand-built definition (no declaration list) still round-trips through the reader;
* every refusal above is exercised, and a refused save leaves no file behind.

**Not verified here:** that retail `igi.exe` loads the editor's file. This host cannot
run the game or the editor's GUI; the strongest available claim is that the written
bytes are identical to the shipped container's for an unedited save, and that they
pass the engine's own loader validation and the repository's reader.

| check | command | result |
| --- | --- | --- |
| build gate | `bash scripts/build-windows.sh` | **exit 0** — x64 link gate OK, i686 compile gate OK |
| host lane | `bash scripts/run-host-tests.sh` | **exit 0** — 1197 tests passed in 69 suites, 0 failed |
| write suites (host) | `IGI_HOST_TESTS=tests/test_menu_qvm.cpp bash scripts/run-host-tests.sh` | 20 passed, 0 failed (8 of them `MenuQvmWrite`) |
| write suites (host) | `IGI_HOST_TESTS=tests/test_menu_ui.cpp bash scripts/run-host-tests.sh` | 34 passed, 0 failed (3 of them `MenuUiSave`) |
| Windows suite | i686 rebuild, then `wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*'` with `WINEPATH` and `IGI_GAME_PATH` set | **exit 0** — 1641 tests ran, 1629 passed, 0 failed, 12 skipped; all 8 `MenuQvmWrite` and 3 `MenuUiSave` tests pass |

The Windows suite must be rebuilt as i686 *after* `build-windows.sh`, because the
gate's x64 half relinks `bin/igi_tests.exe` as PE32+.
