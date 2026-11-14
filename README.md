# TextEditor - CS-218 DSA Complex Engineering Problem

A text editor in C++ where the text engine is built from our own data structures.
The Qt GUI (phase 5, `gui/`) is a thin layer on top; all editing logic lives in `engine/`.

## Status

| Phase | Content | State |
|---|---|---|
| **1** | DynArray, Stack, linked-list Document, file open/save, console editor, tests | **done** |
| **2** | Command records + Undo/Redo (two stacks), grouped edits, replace-range | **done** |
| **3** | KMP search (find all, locations), replace one / replace all, whole-word / ignore-case | **done** |
| **4a** | Bracket checker (stack), Trie autocomplete | **done** |
| **4b** | Hash-table snippets | **done** (moved to `engine/`, now built and tested) |
| **5** | Qt Widgets GUI (DevPad): editor, find/replace, autocomplete, snippets, brackets, themes, Run button | **done** - see `PHASE5_REPORT.md` |
| 6 | Report, complexity table, screenshots | |

## Build and run the GUI (phase 5)

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=<your Qt folder>     # Qt 6 (or 5.15) with Widgets
cmake --build build
./build/devpad            # the editor     (build\devpad.exe on Windows)
ctest --test-dir build --output-on-failure
```
Full instructions for Windows / Linux / macOS, the test list and a manual checklist: `PHASE5_REPORT.md`.
Without Qt, CMake prints a warning and still builds the engine, tests and console editor.

## Build the engine only (Windows, MSYS2 MinGW64 shell)

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
engine/Command.h      Command / Group records (what an edit did, and how to reverse it)
engine/UndoManager.h  the undo stack and redo stack, history cap (100 steps)
engine/Editor.h/cpp   Document + history: every edit goes through here
engine/Search.h/cpp   KMP: failure table, streaming matcher, findAll / findNext on a Document
engine/Brackets.h/cpp bracket checker: stack scan, skips strings / comments, recovery, match at cursor
engine/Trie.h/cpp     prefix tree: insert / remove / weights / top-k completion (own code, no std::map)
engine/Assist.h/cpp   autocomplete (keywords trie + document-words trie), applyCompletion through the Editor
engine/HashTable.h    own hash table (chaining, FNV-1a, rehash at load 0.75)
engine/Snippets.h/cpp snippet table on the hash table, $0 cursor marker, indentation-aware expansion
engine/FileIO.h/cpp   open / save / save-as, errors returned not thrown
gui/                  phase 5: Qt Widgets front end (CodeEditor, FindBar, MainWindow, RunManager, ...)
tests/gui_tests.cpp   headless Qt tests incl. a random widget-vs-engine fuzz test
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
`new open save saveas show info append addline ins nl del delword delrange dellines replace find replaceall replacenth undo redo history brackets match complete accept dictadd dictdel quit`
