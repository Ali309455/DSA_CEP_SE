# TextEditor - CS-218 DSA Complex Engineering Problem

A text editor in C++ where the text engine is built from our own data structures.
The GUI (later phase) is a thin layer on top; all editing logic lives in `engine/`.

## Status

| Phase | Content | State |
|---|---|---|
| **1** | DynArray, Stack, linked-list Document, file open/save, console editor, tests | **done** |
| 2 | Command objects + Undo/Redo (two stacks), replace-one/all plumbing | next |
| 3 | KMP search (find all, locations), replace one / replace all | |
| 4 | Bracket checker (stack), Trie autocomplete, hash-table snippets | |
| 5 | Dear ImGui + GLFW window: custom text rendering, menus, themes, Run button | |
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
No internet or extra libraries are needed in phase 1.

## Layout

```
engine/DynArray.h     own dynamic array (replaces std::vector)
engine/Stack.h        own stack on top of DynArray (replaces std::stack)
engine/Document.h/cpp doubly linked list of lines + cursor cache
engine/FileIO.h/cpp   open / save / save-as, errors returned not thrown
app/console_main.cpp  command-line front end (1-based line/column numbers)
tests/test_main.cpp   test suite incl. random stress test against a model
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

Space: O(N) for text, plus two pointers and one array header per line.

## Console commands

Type `help` inside the editor. Lines and columns start at 1.
`new open save saveas show info append addline ins nl del delword delrange dellines quit`
