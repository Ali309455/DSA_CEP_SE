# Phase 5 report - DevPad Qt GUI

## 1. What was built

A Qt Widgets code editor ("DevPad") compiled through CMake. **All text operations still run on your own data structures**; Qt only draws the text and captures input.

| Feature | Data structure / algorithm behind it |
|---|---|
| Text buffer | doubly linked list of lines (`Document`) - the widget mirrors it |
| Ctrl+Z / Ctrl+Y | two stacks (`UndoManager`), typing merged into one step per word, Tab/indent/replace-all = one step |
| Find / Replace / Replace all (Ctrl+F / Ctrl+H, F3) | KMP (`findAll`, `replaceNext`, `replaceAll`); match case + whole word options; "3 of 12" counter |
| Bracket checker | stack scan; red wavy underline, red dot in the gutter, Problems panel (click to jump), matching-bracket highlight, F8 next problem |
| Autocomplete (pops up while typing, Ctrl+Space) | Trie of C++ words + Trie of the words in your file; Enter / Tab accepts (one undo step) |
| Snippets (type `for`, `if`, `main`... then Tab) | hash table; `$0` cursor marker; keeps indentation; Edit > Manage Snippets (add / remove / import / export, shows bucket count + longest chain) |
| Run button (F5), Stop (Shift+F5), Build only (F7) | compiles the buffer with g++ / clang++ (C++17), shows output, clickable errors, stdin box, Stop kills runaway programs |
| Explorer sidebar (File > Open Folder, Ctrl+Shift+O; Ctrl+B hides it) | folder tree like VS Code: double click opens a file, right click: New File / New Folder / Rename (F2) / Delete (to Trash) / Copy Path / Show in File Manager / Show Hidden Files; the editor follows renames and deletes; the last folder is reopened at start; `devpad <folder>` opens a folder |
| Look | Midnight (dark) / Daylight (light) themes, C++ syntax colours, line numbers, current-line highlight, zoom (Ctrl+wheel, Ctrl +/-), word wrap, auto-indent, auto-close brackets/quotes, Ctrl+/ comment, Ctrl+Shift+D duplicate line |
| View > Engine inspector | live numbers: lines, undo/redo depth, bracket pairs, trie sizes, hash-table buckets (good screenshots for the report) |

## 2. How to build and run (CMake)

### Linux (Ubuntu / Debian)
```bash
sudo apt install build-essential cmake qt6-base-dev
cmake -S . -B build
cmake --build build -j
./build/devpad                 # or: ./build/devpad myfile.cpp
```

### Windows (MSYS2 MinGW64 shell - same one you used for phase 1)
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-qt6-base
cmake -S . -B build -G Ninja
cmake --build build
./build/devpad.exe
```
(The `-static` flag in `CMakeLists.txt` only applies to the console tools; the GUI uses Qt's DLLs. The MSYS2 shell finds them. To run `devpad.exe` by double click, run `windeployqt6 build/devpad.exe` once.)

### Windows with the official Qt installer + Qt Creator / Visual Studio
Tell CMake where Qt lives:
```
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.7.0/mingw_64
cmake --build build
```
In Qt Creator just "Open Project" -> `CMakeLists.txt` -> Run.

### macOS
```bash
brew install qt cmake
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build && ./build/devpad
```

### Other options
* `-DBUILD_GUI=OFF` builds only the engine, `engine_tests` and `console_editor` (phase 1-4).
* If Qt is not found, CMake prints a **warning** and skips the GUI instead of failing.
* Qt 6 is what was tested (Qt 6.4.2). Qt 5.15 is accepted by CMake but was not tested.
* The Run button needs `g++` (or `clang++`) on your PATH; otherwise it shows a clear message.

## 3. How to test

```bash
ctest --test-dir build --output-on-failure        # runs engine_tests and gui_tests
./build/engine_tests                               # 13 214 checks (phases 1-4 + snippets + phase-5 additions)
QT_QPA_PLATFORM=offscreen ./build/gui_tests        # 32 headless GUI tests (no window needed)
```
Windows PowerShell: `$env:QT_QPA_PLATFORM="offscreen"; .\build\gui_tests.exe`
Deeper random test: `GUI_FUZZ_SEEDS=300 ./build/gui_tests fuzzWidgetEngineStayInSync` (120 000 random actions: typing, emoji, paste, cut, Tab, Enter, undo, redo; after every action the widget text must equal the engine text).

Results on the delivery machine: `engine_tests` 13214/13214, `gui_tests` 32/32, fuzz 300 seeds x 400 actions with no divergence.

### Manual checklist (5 minutes, good for screenshots)
1. Start `devpad`. Type `#include <iostream>` + `int main() {` + Enter -> braces close and indent by themselves.
2. Type `ret` -> popup shows `return`; Enter accepts. Ctrl+Z puts back `ret`.
3. Type `for` + Tab -> loop template, cursor inside the parentheses. Ctrl+Z removes it in one step.
4. Delete one `)` -> red dot in gutter, wavy underline, entry in Problems. Put it back -> clears.
5. Ctrl+F, type a word -> all matches highlighted, "k of N". Ctrl+H, type a replacement, "Replace all" -> one Ctrl+Z reverses all.
6. Type a long sentence, press Ctrl+Z repeatedly -> goes back one word at a time; Ctrl+Y goes forward.
7. File > Save As, close, reopen via File > Open Recent. Edit and close the window -> asks to save.
8. Open a binary file / a folder / a file you cannot read -> a clear message, nothing breaks.
9. Write a program, press F5 -> output panel. Introduce a typo, F5 -> error appears in Problems, double click jumps to the line. Write `while(true){}`, F5, then Shift+F5 stops it.
10. View > Theme > Daylight; View > Engine inspector.
11. File > Open Folder: pick your project. Double click a file to open it, right click > New File, Rename (F2), Delete. Rename the open file: the title follows. Delete it: the text stays and Save asks for a name.

## 4. Errors that are handled

| Situation | What happens |
|---|---|
| File missing / is a folder / unreadable / > 20 MB | message box, current document untouched |
| Binary file (NUL bytes) | refused with a message |
| Not valid UTF-8 (old Latin-1 files) | asks first; opens with U+FFFD replacement characters, marked modified |
| UTF-8 BOM, CRLF or lone CR line ends | BOM dropped, all line ends become LF (documented limit of the engine) |
| Save to a bad folder / read-only / disk full | message box; uses `QSaveFile` (writes a temp file then renames) so the old file is never half-written |
| Unsaved changes on New / Open / Quit / drop | Save / Discard / Cancel |
| Empty search text | no error, no highlight; replace-all says "type the text first" |
| Stale autocomplete word / unknown snippet trigger / bad snippet text | status-bar message; text unchanged; exceptions from the engine never reach the user as a crash |
| Emoji and accents (UTF-8 bytes vs Qt UTF-16 columns) | all conversions in `gui/TextUtil.h`; tested with random emoji input |
| Pasted text with `\r\n`, `\r`, U+2028/2029, NUL, broken surrogates | normalised before it reaches the engine |
| Widget and engine ever disagree (should not happen) | detected at once and repaired by `resyncEngineFromWidget()`; saving re-checks first |
| No compiler installed / compile > 2 min / runaway program / program crash | clear message / watchdog / Stop button / "crashed" line |
| Highlighter or find-bar signals that change colours but not text | ignored (compared by content) so they never create fake undo steps |

## 5. Files

**New:** `gui/*` (CodeEditor, FileExplorer, CppHighlighter, FindBar, MainWindow, RunManager, SnippetDialog, TextUtil.h, Theme.h, main.cpp), `tests/gui_tests.cpp`, `PHASE5_REPORT.md`, `TESTING_QUICKSTART.md`.
**Moved (git mv) and fixed:** `next_part_snippets/{HashTable.h,Snippets.h,Snippets.cpp}` -> `engine/` (they did not compile: `DynArray::resize` was missing).
**Edited (old code kept, small additive changes):** `engine/DynArray.h` (+`resize`), `engine/Editor.h/.cpp` (+`loadText`, `markSaved`, `inGroup`), `engine/Assist.h/.cpp` (`refresh(doc, skipAt)` optional argument), `CMakeLists.txt` (Snippets + GUI + tests), `tests/test_main.cpp` (+2 test functions), `README.md`.

## 6. Known limits (honest list)
* One document per window (no tabs). The engine is single-document by design.
* Saving always ends the file with one newline and uses LF line ends.
* "Undo" label shows the last committed step; the word being typed joins the history when you press space, pause, move, or undo.
* Syntax colouring is a small hand-written lexer, not a full C++ parser (raw strings are coloured as ordinary strings).
* Tested with Qt 6.4 on Linux (headless). Windows and macOS builds follow the same CMake file but were not run here.
