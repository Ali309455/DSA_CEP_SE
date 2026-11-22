# TextEditor - CS-218 DSA Complex Engineering Problem

A text editor in C++ where the text engine is built from our own data structures.
The GUI (DevPad, Qt Widgets) is a thin layer on top; all editing logic lives in `engine/`.

## Status

| Phase | Content | State |
|---|---|---|
| **1** | DynArray, Stack, linked-list Document, file open/save, console editor, tests | **done** |
| **2** | Command records + Undo/Redo (two stacks), grouped edits, replace-range | **done** |
| **3** | KMP search (find all, locations), replace one / replace all, whole-word / ignore-case | **done** |
| **4a** | Bracket checker (stack), Trie autocomplete | **done** |
| **4b** | Hash-table snippets (`engine/HashTable.h`, `Snippets.*`), console + GUI | **done** |
| **4c** | Diagnostics: compiler (`g++ -fsyntax-only`) + bracket errors in one list; red / yellow squiggles, Problems tab, console `check` | **done** |
| **5** | **DevPad**: Qt Widgets GUI built with CMake - multi-tab editing, line move / copy, auto-pairs and wrapping selections in `() [] {} "" ''`, Ctrl+Tab, Quick Open, rename file, find / replace, file tree | **done** |
| **5b** | **DevPad refining**: Command Palette, code folding, Outline and Undo History panels, breadcrumbs, word wrap, clickable `file:line` links in the terminal, 14 themes with 10 syntax colours each | **done** |
| 6 | Report, complexity table, screenshots | |

## Build (Windows, MSYS2 MinGW64 shell)

```
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
cmake -S . -B build -G Ninja
cmake --build build
./build/engine_tests.exe      # run the test suite
./build/console_editor.exe    # try the editor
```
Linux / macOS: same, without `-G Ninja` if you prefer Makefiles.
No extra libraries are needed for the engine, tests and console editor.

### DevPad (phase 5, Qt GUI)

Needs Qt 6 (Qt 5.15 also works). With CMake, point it at your Qt install:

```
# Windows (MSYS2):  pacman -S mingw-w64-x86_64-qt6-base
# Linux (Debian):   sudo apt install qt6-base-dev
# Windows (Qt installer): add  -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/mingw_64
cmake -S . -B build -G Ninja
cmake --build build
./build/devpad [file-or-folder ...]
ctest --test-dir build          # engine tests + headless GUI tests
```
If Qt is not found the GUI is skipped and everything else still builds; `-DBUILD_GUI=OFF` skips it on purpose.
On Windows run `windeployqt build/devpad.exe` once to copy the Qt DLLs next to the program.

## Layout

```
engine/DynArray.h     own dynamic array (replaces std::vector)
engine/Stack.h        own stack on top of DynArray (replaces std::stack)
engine/Document.h/cpp doubly linked list of lines + cursor cache
engine/Command.h      Command / Group records (what an edit did, and how to reverse it)
engine/UndoManager.h  the undo stack and redo stack, history cap (100 steps), read-only peek for the Undo History panel
engine/Editor.h/cpp   Document + history: every edit goes through here; jumpToHistory, describeStep
engine/Search.h/cpp   KMP: failure table, streaming matcher, findAll / findNext on a Document
engine/Brackets.h/cpp bracket checker: stack scan, skips strings / comments, recovery, match at cursor
engine/Trie.h/cpp     prefix tree: insert / remove / weights / top-k completion (own code, no std::map)
engine/Assist.h/cpp   autocomplete (keywords trie + document-words trie), applyCompletion through the Editor
engine/FileIO.h/cpp   open / save / save-as, errors returned not thrown
engine/EditOps.h/cpp  code-editor commands: move / duplicate / indent / comment lines, auto-pair, wrap selection, smart Enter
engine/Workspace.h/cpp folder listing + fuzzy file search (Quick Open, Command Palette)
engine/Folding.h/cpp  code folding: fold ranges (bracket pairs / indentation stack), FoldSet line <-> row map (binary search)
engine/Outline.h/cpp  symbols for the Outline panel: class / struct / enum / function headers, nesting by stack
gui/CodeView.h/cpp    the text area: custom-painted Qt widget on top of Editor (no QTextDocument); folding, word wrap, breadcrumbs
gui/MainWindow.h/cpp  tabs, menus, shortcuts, find bar, Quick Open, Command Palette, rename, Run / Stop group
gui/FileExplorer.h/cpp  the file manager in the left sidebar
gui/WelcomePage.h/cpp   shown when no file is open: start actions, recent files / folders, shortcuts
gui/Settings.h/cpp      the user's preferences (QSettings), one signal when anything changes
gui/SettingsDialog.*    Settings window (Ctrl+,), gui/Controls.* its switches, steppers, swatches
gui/ShortcutsDialog.*   Keyboard Shortcuts window (F1): searchable key-cap cards
gui/TerminalPanel.*     built-in terminal under the editor (Ctrl+`), clickable file:line links
gui/ProblemsPanel.*     Problems tab next to the terminal (Ctrl+Shift+M): errors / warnings, click to jump
gui/SyntaxChecker.*     runs g++ -fsyntax-only on a copy of the text in the background (QProcess)
gui/SidePanels.*        Outline (Ctrl+F12) and Undo History (Ctrl+Shift+H) panels on the right
gui/Breadcrumbs.*       folder > file > class > function bar at the top of each editor (click to jump)
gui/SnippetStore.*      the shared snippet table + snippets.txt in the app config folder; gui/SnippetDialog.* the Snippets window
engine/Diagnostics.*    compiler-output parser, bracket issues as diagnostics, squiggle length, syntax-check runner, file:line finder for the terminal
engine/HashTable.h, Snippets.*  snippet hash table (chaining), `$0` cursor marker, save / load
gui/Runner.h/cpp        what F5 runs for each file type
gui/Theme.h/cpp         the 14 themes (Midnight, Daylight, Nord, Dracula, Monokai, Gruvbox, Tokyo Night, Jellyfish, ...): palettes, style sheet, UI and code fonts (one place for every colour)
gui/Icons.h/cpp         all icons, drawn in code: line icons, file-type badges, folders, the app icon
gui/WinTheme.h/cpp      dark / light title bars on Windows 10 / 11 (dwmapi), following the theme
gui/devpad.ico, devpad.rc.in   the program icon for devpad.exe
gui/main.cpp          entry point (DEVPAD_SNAPSHOT=out.png saves a picture of the window and quits, see instructions.md)
app/console_main.cpp  command-line front end (1-based line/column numbers)
tests/test_main.cpp   test suite incl. random stress test against a model
tests/gui_test.cpp    headless GUI tests: real key presses against the real widgets
```

## Design (phase 1)

**Document = doubly linked list of lines; each line = `DynArray<char>`.**
Adding or removing a whole line only rewires pointers, no shifting of the rest
of the document. Edits inside a line touch one short array. A *cursor cache*
remembers the last line visited, so lookup walks from the nearest of
{head, tail, cache}: sequential access (typing, printing, scrolling) is O(1)
per step, not O(n).

**Two primitives, everything else built on them.**
`insertText(pos, text)` and `deleteRange(a, b)`. Insert/delete a line, word,
character, or range are all expressed through these. In phase 2 the inverse of
one is the other, which is what makes the undo stack simple and correct.

**Invariant:** a document always has at least one line (empty = one empty line).

**Files:** a file is lines ending in `\n`. `\r` is dropped on load. A trailing
newline ends the last line, it does not start a new one. An empty document
saves as an empty file.

**Errors:** bad positions throw `std::out_of_range` / `std::invalid_argument`
from the engine and the front end prints them as messages. File problems come
back as `IOResult{ok, message}`. A failed operation never changes the document.

## Undo / Redo (phase 2)

Every edit is made of two primitives, so the history only needs two kinds of
record. `Insert(start, end, text)` is undone by deleting `start..end`;
`Delete(start, end, text)` is undone by inserting `text` at `start`.

* `Editor` does the edit on the `Document`, then records it. If the edit throws
  (bad position), nothing is changed and nothing is recorded.
* `UndoManager` holds two `Stack<Group>`: undo and redo. A new edit is pushed on
  undo and **empties redo**. Undo pops from undo and pushes on redo. Redo does the
  reverse. The undo stack keeps the newest 100 steps (`setHistoryLimit`).
* A `Group` is one step as the user sees it. Replace = delete + insert in one
  group. "Replace all" (phase 3) will put every replacement in one group, so one
  Undo reverses all of them. Undo runs a group's commands newest first; redo runs
  them oldest first.
* Opening a file or starting a new document clears the history.

Console: `undo`, `redo`, `history`, `replace <l1> <c1> <l2> <c2> <text>`.

## Search and replace (phase 3)

**Algorithm: Knuth-Morris-Pratt.** `buildFailureTable` computes, for each prefix of
the pattern, the length of its longest proper prefix that is also a suffix. On a
mismatch the matcher falls back along that table instead of moving back in the
text, so every text character is read once: O(n + m) worst case, O(m) extra
memory. (The naive search is O(n*m) on inputs like `aaaa...ab`; the test suite runs
exactly that case on 2 million characters.)

**Streaming over the linked list.** `KmpMatcher` takes one character at a time.
`findAll` feeds it line 0, a `'\n'`, line 1, a `'\n'`, ... so the document is never
copied into one big string, and a pattern may contain `'\n'` and match across
lines. Because the matcher keeps its state between lines, no match is missed at a
line boundary. After a match it continues from the failure table, so overlapping
matches (`aa` in `aaa`) are all found.

**Options.** `matchCase=false` folds ASCII letters. `wholeWord=true` rejects a match
that touches a letter, digit or `_`; a side is only checked if the search text itself
starts / ends with a word character (so `(foo)` still matches).

**Replace.** `Editor::replaceAll` finds the non-overlapping matches first, then replaces
from the LAST to the FIRST, so earlier positions never shift. All of it is one `Group`:
one Undo reverses everything. It is all-or-nothing: if anything fails half way the
commands already applied are reversed and the history is left untouched. Matches that
already equal the replacement are skipped (no empty history steps).
`replaceNth` replaces the n-th occurrence as numbered by `find`; `replaceNext(from, ...)`
replaces the first occurrence at/after a position and returns where to continue (for the
GUI's "Replace" button), with optional wrap-around.

**Errors handled** (nothing changes, nothing is recorded in any of them):

| Situation | Result |
|---|---|
| empty search text (or only `\r`) | `std::invalid_argument` |
| searching / replacing in an empty document | 0 matches, not a crash (console says the document is empty) |
| text not found | `found == 0`; `replaceNth` -> `out_of_range` "not found" |
| occurrence number 0 or too large | `std::out_of_range` with the real count |
| pattern longer than the document | immediate "no match" (O(1)) |
| `findNext` from an invalid position | `std::out_of_range` |
| replacement equals the found text | skipped, no history step |
| failure during replace all | rolled back, history unchanged |
| bad console input (missing quote, missing argument, extra arguments, bad number) | message with the correct usage |

Console: `find [-i] [-w] <text>`, `replaceall [-i] [-w] <old> <new>`,
`replacenth [-i] [-w] <n> <old> <new>`. Use "quotes" for text with spaces or an empty
replacement (`""`); inside quotes `\n` `\t` `\"` `\\` are escapes.

## Bracket checker (phase 4a)

**Algorithm: one stack, one pass.** Each `(` `[` `{` is pushed with its position; each closer
pops and must be the same kind. Whatever is left at the end was never closed. The document is
scanned line by line (never copied); the lexer state is kept between lines so `/* ... */` and
raw strings may span lines. Brackets inside `"strings"`, `'c'`, `R"x(raw)x"`, `// comments`
(a trailing `\` continues them) and `/* comments */` are ignored; `1'000'000` is a number, not
a character literal. `< >` are not checked (they are operators as often as brackets).

**Recovery: one typo, one message.** Closer on an empty stack -> `UnmatchedClose`. Closer that
matches an opener further down -> the openers above it are reported `Unclosed` and popped, then
it pairs normally. Closer that matches nothing -> `Mismatch`, the top opener is consumed.
`BracketReport` also lists every matched pair (with nesting depth, for coloured brackets) and
`matchNearCursor` finds the partner of the bracket at / just before the cursor.

| Situation | Result |
|---|---|
| empty document | OK, 0 pairs |
| `)` with nothing open | `UnmatchedClose` at its position |
| `(` closed by `]` | `Mismatch` (shows where the `(` is and what was expected) |
| `/*` never closed / `"` with no closing quote / raw string never ended | `UnterminatedComment` / `UnterminatedString` |
| more than `maxIssues` (200) problems | list cut, `truncated = true` |
| 200 000 levels of nesting | fine: explicit stack, no recursion |
| cursor outside the document | `std::out_of_range` |

Known limits: a string continued with a trailing `\` onto the next line is reported as unterminated;
trigraphs / digraphs are not understood; `matchOf` is a linear search of the pair list.

## Trie autocomplete (phase 4a)

**Trie with sorted child arrays.** A node = one prefix; the path from the root spells it. Each
node keeps its children as a small array sorted by character (binary search), so memory
follows the characters really used instead of 256 pointers per node. Nodes live in one
`DynArray` and refer to each other by index, so growing the array can never leave a dangling
pointer. Collecting results is an iterative depth-first walk (no recursion), so words come out
alphabetically and the best `k` are kept in a small sorted array.

`Autocomplete` owns two tries: the C++ dictionary (keywords, preprocessor words, common `std`
names) and the words of the open document (`refresh`, O(N)). A suggestion's score is its weight
in the dictionary plus its weight in the document (uses). Accepting a suggestion raises its
dictionary weight, so favourites float up. Because each trie only returns its own top `m`,
`suggest` uses a *threshold rule*: any word outside both top-`m` lists scores at most the
sum of their `m`-th weights; if the `k`-th best score is above that, the answer is exact,
otherwise `m` doubles. (A brute-force comparison runs in the tests.)

`applyCompletion(editor, cursor, word)` inserts the missing tail at the cursor as ONE undo step
(label "autocomplete").

| Situation | Result |
|---|---|
| empty / invalid prefix (blank, control character, > 64 chars) | no suggestions |
| word that is empty, has blanks, or is too long | `insert` returns false, trie unchanged |
| `maxResults == 0` | no suggestions |
| numbers and 1-letter words in the document | ignored |
| stale suggestion that does not start with the typed prefix | `std::invalid_argument`, text unchanged, nothing recorded |
| bad cursor | `std::out_of_range` |
| weight overflow | saturates, never wraps |

Console: `brackets`, `match <l> <c>`, `complete <prefix>` / `complete <l> <c>`,
`accept <l> <c> <word>`, `dictadd <word>`, `dictdel <word>`.

## DevPad - phase 5 (Qt GUI)

Every edit still goes through `Editor`, so undo / redo, KMP search, the bracket checker and the Trie
autocomplete are the code from phases 1-4, not Qt's. `CodeView` only draws the `Document` and turns
key presses into engine calls.

### Keyboard shortcuts (Help > Keyboard Shortcuts, F1)

| Area | Keys |
|---|---|
| Tabs | `Ctrl+Tab` / `Ctrl+Shift+Tab` next / previous (also `Ctrl+PgDn` / `Ctrl+PgUp`), `Alt+1`..`Alt+9` go to tab (`Alt+9` = last), `Ctrl+N` new, `Ctrl+W` close |
| Files | `Ctrl+P` fuzzy file search ("mw" finds `MainWindow.cpp`), `F2` rename file, `Ctrl+O` open, `Ctrl+Shift+O` open folder, `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+Alt+S` save / save as / save all, `Ctrl+B` shows / hides the sidebar (so does the x in the explorer header) |
| Lines | `Alt+Up` / `Alt+Down` move line (or all selected lines), `Shift+Alt+Up` / `Shift+Alt+Down` copy line up / down (`Ctrl+D` copy down), `Ctrl+Shift+K` delete line, `Ctrl+Enter` / `Ctrl+Shift+Enter` new line below / above, `Ctrl+]` / `Ctrl+[` indent / outdent, `Ctrl+/` toggle comment, `Ctrl+L` select line |
| Brackets, quotes | with text selected type `(` `[` `{` `"` `'` `` ` `` to wrap it (selection stays on the text); the same key again, or the closing bracket, removes the pair; typing an opener adds the closer, typing the closer steps over it, Backspace between an empty pair removes both |
| Typing | smart Enter keeps the indent and opens a block between `{ }`; Tab / Shift+Tab indent / outdent selected lines; Backspace in leading spaces goes back one tab stop; `Home` jumps to the first non-blank character first |
| Search | `Ctrl+F` find, `Ctrl+H` replace, `F3` / `Shift+F3` next / previous, `Ctrl+G` go to line, `Ctrl+Space` autocomplete, `Ctrl+Shift+P` Command Palette (run any menu command by name) |
| Folding | `Ctrl+Shift+[` fold the block at the cursor (again: the block around it), `Ctrl+Shift+]` unfold; click the arrow next to a line number; Edit > Folding > Fold All / Unfold All |
| Panels | `Ctrl+F12` Outline, `Ctrl+Shift+H` Undo History, `Ctrl+\`` Terminal, `Ctrl+Shift+M` Problems, `Alt+Z` word wrap |
| Edit | `Ctrl+Z`, `Ctrl+Y` / `Ctrl+Shift+Z`, cut / copy / paste (copy or cut with nothing selected takes the whole line), `Ctrl+Wheel` / `Ctrl+=` / `Ctrl+-` zoom |
| Explorer (sidebar focused) | click / `Enter` open file or open/close folder, `F2` rename in place, `Del` delete to the Recycle Bin, `Shift+Del` delete permanently, `Ctrl+X` / `Ctrl+C` / `Ctrl+V` cut / copy / paste files, drag onto a folder to move (`Ctrl`+drag copies) |

### Settings, terminal and Run (GUI refining)

- **Settings** (`Ctrl+,`, the gear in the activity bar, File > Settings): font size and family, tab width, auto-close
  brackets, wrap selection, suggest-while-typing, cursor style and blinking, accent colour, line numbers, indent
  guides, current-line and bracket highlight, status bar, word wrap, breadcrumbs, terminal font / shell / scrollback,
  reopen last session. Everything applies immediately and is remembered; the search box filters the rows.
- **Terminal** (`Ctrl+\``): runs commands in the open folder (cmd on Windows, `$SHELL` elsewhere, or the shell chosen
  in Settings), shows ANSI colours, remembers `cd`, `Up`/`Down` history, `Ctrl+C` stops, `Ctrl+L` clears.
  **File references are links**: hover `main.cpp:12:5`, `src/a.h:7`, `C:\dir\x.cpp:3:1` or a Python
  `File "x.py", line 4` and it is underlined; a click opens the file at that line and column (relative paths are
  looked up in the terminal's folder, then in the open folder). Times (`12:30`), web addresses and names without
  an extension are left alone. Dragging still selects text.
- **Run / Stop buttons**: one rounded group at the right end of the tab strip, centred on it, doing the same as
  `F5` / `Shift+F5`. Run is green and Stop red only while they can be used; otherwise they are dimmed.
- **Auto save** (Settings > General, off by default): saves modified files that already have a name once you have stopped
  typing for the chosen delay (1 to 300 seconds, 3 by default).
- **Snippets in suggestions**: the autocomplete list shows matching snippet triggers first (marked "snippet"); accepting one
  expands it. Edit > Manage Snippets... adds, edits and deletes them.
- **Run** (`F5`): saves the file, builds it if needed and runs it in the terminal, in the file's folder. `Shift+F5`
  stops it. Needs the tool on PATH: `g++`/`gcc` (C/C++), `python`, `node`, `java`, `go`, `rustc`, `ruby`, `php`, `lua`,
  `bash`; `.bat`/`.cmd`/`.ps1` run directly. A compile error stops the run, so an old build never runs by mistake.

### Look

DevPad has 14 themes (`Theme.h`), each with its own syntax colours: **Midnight** (the default dark look),
**Daylight**, **Nord**, **Solarized Light**, **Dracula**, **Alucard**, **Monokai**, **Paper**, **Gruvbox Dark**,
**Gruvbox Light**, **Tokyo Night**, **One Light**, **Jellyfish** and **Jellyfish Light**. Pick one in Settings > Appearance > Theme or in View > Theme;
`Ctrl+Shift+L` (or the theme button in the status bar) switches between the light and dark partner
(Midnight <-> Daylight, Nord <-> Solarized Light, Dracula <-> Alucard, Monokai <-> Paper, Gruvbox Dark <-> Gruvbox
Light, Tokyo Night <-> One Light, Jellyfish <-> Jellyfish Light). The choice is remembered and applies at once to the editor, the syntax colours,
the sidebar, the terminal, the dialogs and the Windows title bar. Code gets ten colours per theme: plain text,
keywords, types (including capitalised class names such as `CodeView`), strings, numbers, comments,
`#include` / preprocessor lines, function calls (`setIcon(...)`), operators (`->`, `::`, `+=`, `<=`) and constants
(`true`, `nullptr`, `MAX_SIZE`). **Jellyfish** is a deep-sea navy with glowing pink, cyan, violet and mint; **Jellyfish
Light** is the same family on a pale background. The accent colour (blue by default) is chosen
separately and works with every theme. Every theme has rounded inputs and menus and thin scroll bars. Every
icon is drawn in code by `Icons.h`, so there are no image files and icons stay sharp at any screen scale. The
Explorer and the tabs show a coloured badge per file type. The activity bar on the far left has Explorer,
Find, Go to File and Keyboard Shortcuts. Code uses Cascadia Code / Cascadia Mono when installed (Windows 11),
otherwise JetBrains Mono, Fira Code or Consolas.

### Navigating and reading code

- **Command Palette** (`Ctrl+Shift+P`, Search > Command Palette...): every menu command (including the themes and
  the Lines / Folding submenus) in one searchable list, ranked by the same fuzzy score as `Ctrl+P`, with each
  command's shortcut on the right. With an empty search, the 8 commands you used last come first.
- **Code folding**: hover the line-number gutter to see arrows next to every block of 3 or more lines; click one to
  fold the block to its first line (a small "⋯" marks it), click again to open it. C-like code folds by bracket pairs
  (the line with the closing `}` stays visible, so `} else {` never hides code); Python and scripts fold by
  indentation. Arrow keys, clicks and scrolling skip folded blocks; moving the cursor into one (Go to Line, search,
  the Outline) opens it. Folds move with the text when lines are added or removed above them.
- **Outline** (`Ctrl+F12`, right side): namespaces, classes, structs, enums and functions of the current file as a
  tree with coloured badges. Click to jump; the symbol around the cursor is highlighted; the box filters. Calls with
  lambdas (`connect(...) {`), `if` / `for` blocks and `= {` initializers are not mistaken for functions. Python:
  `def` and `class`. It is a reading of the text, not a full C++ parser, so unusual macros can be missed.
- **Undo History** (`Ctrl+Shift+H`, right side): the undo and redo stacks as one timeline, oldest at the top
  (`insert "hello"  line 3`); undone steps are dimmed. Click any row to undo or redo up to that point.
- **Breadcrumbs**: the bar above the text shows `folder › file › class › function` for the cursor. A folder crumb
  shows it in the Explorer, the file crumb opens a menu of every symbol in the file, a symbol crumb jumps to it.
  Narrow windows drop crumbs from the left. Settings > Appearance or View > Breadcrumbs turns it off.
- **Word wrap** (`Alt+Z`, View > Word Wrap, Settings; off by default): long lines continue on the next row,
  breaking after the last space that fits, with no sideways scroll bar. Up / Down move one screen row and keep the
  column; clicks, selection, search highlights, squiggles and folding all follow the wrapped rows. Off for files
  over about 3 MB to keep typing fast.

### Welcome page

When no file is open (at start-up without a session, or after closing the last tab) DevPad shows a welcome
page instead of an empty editor: New File / Open File / Open Folder / Go to File, the recently used folders
and files (kept in the settings, missing ones are skipped), and the handiest shortcuts.

### File manager (left sidebar)

The Explorer sidebar is a small file manager for the open folder:

- **Toolbar:** new file, new folder, refresh, collapse all. The header shows the folder name.
- **Filter box:** searches every file below the folder with the same fuzzy ranking as `Ctrl+P` (`Workspace.h`).
- **Right-click menu:** open, new file / folder, cut / copy / paste, duplicate (`name copy.ext`), copy path, copy relative path, reveal in File Explorer, rename, delete.
- **Drag and drop:** drag onto a folder to move, `Ctrl`+drag to copy, or drop files in from Windows Explorer.
- **Open tabs follow the files.** Renaming or moving a file or folder updates the tabs' paths and keeps unsaved edits and undo history. Deleting closes the tabs, after a warning if they have unsaved changes.
- **The active tab's file is selected in the tree**, opening its folders as needed.

Every operation is in the engine (`FileIO.h`: `checkFileName`, `createFile`, `createFolder`, `renamePath`,
`movePath`, `copyPath`, `isSameOrInside`), tested in `engine_tests` ("File manager"). Nothing is ever
overwritten, and a folder cannot be moved or copied into itself. Deleting goes to the Recycle Bin, with
an offer to delete permanently when the drive has none.

### Where the data structures are used

| Feature | Data structure / algorithm | Cost |
|---|---|---|
| Text | doubly linked list of lines with a cursor cache | line access O(distance from the last line used) |
| Undo / redo | two stacks of command records; a run of key presses is merged into one record (`typeText`, `eraseCoalescing`) | O(1) per key press, O(size of the step) per undo |
| Move / copy lines, comment, indent, wrap in `()`, smart Enter | one `EditGroup` each, so every command is exactly one undo step | O(characters in the lines touched) |
| Find / replace | KMP | O(n + m) |
| Bracket status bar, matching-bracket highlight | stack scan, rerun 250 ms after the last edit | O(n) |
| Autocomplete popup | Trie (dictionary + words of the document) | O(prefix + k) |
| Ctrl+P file search | dynamic-programming subsequence score, top-k kept in a small sorted array | O(files x query x name length), never sorts the whole list |
| Rename file | validates the name (no folders, no Windows-forbidden characters, never overwrites), keeps text and undo history | O(1) |
| Explorer filter | same fuzzy score + top-k as Ctrl+P over the cached file list | O(files x query x name length) |
| Explorer move / rename of a folder with open tabs | each tab's path is compared with the folder path (prefix test) | O(tabs x path length) |
| Line endings | the engine text has no `\r`; CRLF files are written back as CRLF | O(n) on open / save |
| Command Palette | the Ctrl+P fuzzy score over the menu commands, top-k in a small sorted array; recent commands as a most-recently-used list | O(commands x query x name length) |
| Fold ranges (`Folding.h`) | bracket pairs from the bracket checker, bucketed by their opening line (counting sort); scripts: a stack of open indentation blocks | O(lines + pairs) |
| Folded lines <-> screen rows (`FoldSet`) | folded ranges merged into disjoint hidden intervals with prefix sums of hidden lines; binary search | O(log k) per lookup, k = folds |
| Outline (`Outline.h`) | `{` pairs put in opening order by counting sort; each header classified by one scan; nesting from a `Stack` of open symbols | O(N + pairs) |
| Undo History panel | reads the two undo / redo stacks with `Stack::peek`; jumping = repeated undo / redo | O(1) per row, O(steps) per jump |
| Word wrap | first row of every line as prefix sums; row -> line by binary search; rebuilt lazily after an edit, a fold or a resize | O(log n) per lookup, O(N) per rebuild |
| Terminal links (`findFileRefs`) | one left-to-right scan for `path:line[:col]` and Python `File "x", line N` | O(length of the line) |

## Complexity (phase 1)

N = characters in document, L = length of one line, n = lines, k = distance to a line.

| Operation | Time | Notes |
|---|---|---|
| `push_back` (DynArray / Stack push) | O(1) amortized | capacity doubles |
| Stack pop / top | O(1) | |
| Find line k | O(min(k, n-k, dist to cache)) | O(1) when sequential |
| `getLine`, `lineLength` | O(k + L) | |
| `insertText` of m chars | O(k + m + L) | tail of the line is moved once |
| `deleteRange` | O(k + deleted chars + lines removed) | |
| Delete whole line(s) | O(k + chars in them) | pointer rewiring |
| `loadFromString` / `toString` | O(N) | |
| Open / save | O(N) | |
| Undo / redo one step | O(1) stack move + cost of the edit it runs | |
| Record an edit | O(1) amortized + copy of its text | |
| **Phase 3** (N = chars in document, m = pattern length, k = matches) | | |
| `buildFailureTable` | O(m) time, O(m) space | |
| `findAll` / `findNext` | O(N + m) time, O(m + k) space | text read once; no copy of the document |
| Whole-word check | O(1) per match | single-line patterns; multi-line patterns re-read one line |
| `replaceAll` | O(N + m + k + cost of k edits) | each edit O(distance + L); one undo step |
| `replaceNth` | O(N + m) + one edit | |
| Undo of replace all | O(k) commands | |

| **Phase 4a** (L = word length, s = distinct characters in use <= 256, p = prefix length) | | |
| `checkBrackets` | O(N) time, O(depth + pairs) space | one pass, one stack |
| `matchNearCursor` | O(pairs) | linear search of the pair list |
| `Trie::insert / contains / remove / weightOf` | O(L log s) | binary search per level |
| `Trie::complete(prefix, k)` | O(p log s + S*k) | S = nodes under the prefix; no full sort |
| `Autocomplete::refresh` | O(N log s) | rebuilds the document trie |
| `Autocomplete::suggest` | O(complete) x O(log) rounds worst case | usually one round |
| `applyCompletion` | O(L + one insert) | one undo step |

Space: O(N) for text, plus two pointers and one array header per line.

## Console commands

Type `help` inside the editor. Lines and columns start at 1.
`new open save saveas show info append addline ins nl del delword delrange dellines replace find replaceall replacenth undo redo history brackets match complete accept dictadd dictdel expand snippet check quit`

## Snippets, error checking and the Problems tab (phase 4b / 4c)

- **Snippets** live in our own chained hash table. `$0` in a body marks where the cursor lands, `$$` is a real
  dollar sign, and every line after the first gets the indentation of the line you expanded on. Console:
  `expand [line col]`, `snippet list|show|add|del|load|save|defaults|stats`. GUI: type the trigger and press
  `Tab` (or `Ctrl+J`, Edit > Expand Snippet); Edit > Manage Snippets... opens the Snippets window: search, New, Duplicate, Delete, edit the trigger or
  the body and Save (renaming works too), plus import, export and reset to the built-ins.
  The GUI keeps the list in `snippets.txt` in the app config folder.
- **Error checking** has two sources: the instant bracket checker (while typing) and the compiler
  (`g++ -fsyntax-only` on a temporary copy of the text, about 1 s after you stop typing, so unsaved edits are
  checked and the file is never touched). Both feed one list of `Diagnostic` (file, line, column, severity,
  message). Errors get a red wavy underline and a red dot in the line-number gutter, warnings a yellow one;
  hover the underline for the message.
- **Problems tab** (`Ctrl+Shift+M`, or click the status-bar counter) sits beside Terminal; click a row to jump to
  the line. `F7` checks now; Run > Check Code While Typing turns the background check off. The compiler check
  needs `g++` (or `gcc` for `.c`) on PATH; without it the brackets are still checked and the panel says so.
- Console `check` prints the same list (brackets + compiler) with `line:col`.
