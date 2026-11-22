# How to Build and Run DevPad

DevPad has two front ends on one engine:

- the **console editor** (`console_editor`), which shows every required operation as a typed command, and
- the **DevPad GUI** (`devpad`), a Qt Widgets editor.

The console editor needs only a C++17 compiler. The GUI also needs Qt.
Every command below is given for **PowerShell** (Windows) and for a **terminal** (Git Bash on Windows, or Linux / macOS).
Please run all commands from the project root, the folder that contains `CMakeLists.txt`.

---

## 1. Requirements

### Console editor (and the engine tests)

| Requirement | Notes |
|---|---|
| C++17 compiler with `<filesystem>` | GCC 9 or newer (MinGW-w64 on Windows), Clang 9+, or MSVC 2019+ |
| CMake 3.16+ | Optional: only needed for the CMake build in Section 2.2 |
| `g++` on `PATH` at run time | Optional: only the `check` command uses it; every other command works without it |

No other libraries are needed.

> **Note:** old MinGW installations (for example GCC 6.3 in `C:\MinGW`) cannot build the project and stop with
> `fatal error: filesystem: No such file or directory`. Please use a recent MinGW-w64, for example the one Qt installs in
> `C:\Qt\Tools\mingw1310_64`.

### DevPad GUI

| Requirement | Notes |
|---|---|
| Everything listed for the console editor | |
| Qt 6 (Qt 5.15 also works), Widgets module | Tested with Qt 6.12.0, MinGW 64-bit kit (`C:\Qt\6.12.0\mingw_64`) |
| The compiler that matches the Qt kit | For the Qt MinGW kit: `C:\Qt\Tools\mingw1310_64`. Mixing compilers causes build or start-up errors |
| CMake 3.16+ and Ninja | Ninja is installed by the Qt installer in `C:\Qt\Tools\Ninja` |
| `windeployqt` (comes with Qt) | Copies the Qt DLLs next to `devpad.exe`; the build runs it automatically |

The paths above are examples. If Qt is installed elsewhere, please change them in the commands below.

---

## 2. Console editor

### 2.1 With the compiler only (one command)

**PowerShell**

```powershell
& "C:\Qt\Tools\mingw1310_64\bin\g++.exe" -std=c++17 -O2 -Iengine app\console_main.cpp (Get-ChildItem engine\*.cpp).FullName -static -o console_editor.exe
.\console_editor.exe
```

**Terminal**

```bash
g++ -std=c++17 -O2 -Iengine app/console_main.cpp engine/*.cpp -static -o console_editor.exe
./console_editor.exe
```

On Linux or macOS, leave out `-static` and run `./console_editor`. On Windows, `-static` makes the `.exe` run on another PC without extra DLLs.
Compilation takes up to a minute.

### 2.2 With CMake (also builds the test suite)

**PowerShell**

```powershell
cmake -S . -B build-console -G Ninja -DBUILD_GUI=OFF -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe"
cmake --build build-console --parallel
.\build-console\console_editor.exe
.\build-console\engine_tests.exe
```

**Terminal**

```bash
cmake -S . -B build-console -DBUILD_GUI=OFF
cmake --build build-console --parallel
./build-console/console_editor
./build-console/engine_tests
```

`engine_tests` should end with `18984/18984 checks passed`.

### 2.3 Using the console editor

Type `help` to list the commands. Lines and columns start at 1.
`saveas <name>` saves into a `files` folder in the project root (created automatically) and prints the full location;
`open <name>` looks in that folder first. A full path such as `C:\temp\notes.txt` is used exactly as typed. A short session:

```text
> open demo.cpp
> show
> find count
> replaceall count total
> undo
> save
> quit
```

---

## 3. DevPad GUI

### 3.1 Configure and build

**PowerShell**

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" `
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" `
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/Ninja/ninja.exe" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/mingw_64"
cmake --build build --parallel
```

**Terminal (Git Bash on Windows)**

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER="C:/Qt/Tools/mingw1310_64/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/Ninja/ninja.exe" \
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/mingw_64"
cmake --build build --parallel
```

**Terminal (Linux, Debian / Ubuntu)**

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja
cmake --build build --parallel
```

If Ninja is already on `PATH`, the `-DCMAKE_MAKE_PROGRAM=...` line may be left out.
This builds `devpad`, `console_editor`, `engine_tests` and, if the Qt Test module is installed, `gui_tests`.

### 3.2 Run

**PowerShell**

```powershell
.\build\devpad.exe
```

**Terminal**

```bash
./build/devpad.exe        # Windows (Git Bash)
./build/devpad            # Linux / macOS
```

A file or folder may be passed as an argument, for example `.\build\devpad.exe .` to open the project folder.

### 3.3 A short tour

Please open the project folder (`.\build\devpad.exe .`) and a `.cpp` file from it, then try:

| Feature | How to try it |
|---|---|
| Command Palette | `Ctrl+Shift+P`, type `nord` or `toggle terminal`, press Enter |
| Code folding | hover the line numbers and click the arrow next to a `{`, or press `Ctrl+Shift+[` / `Ctrl+Shift+]` |
| Outline | `Ctrl+F12`; click a function to jump to it, or type in the filter box |
| Undo History | `Ctrl+Shift+H`; make a few edits, then click an earlier row to go back to it |
| Breadcrumbs | the bar above the text; click the file name for a menu of every function |
| Word wrap | `Alt+Z` with a long line in the file |
| Clickable errors | introduce a syntax error, press `F5`, then click the `file.cpp:line:col` in the terminal |
| Themes | View > Theme > Jellyfish, or `Ctrl+Shift+L` to switch between a theme's light and dark versions |

`F1` lists every keyboard shortcut. Word wrap and the breadcrumbs bar can also be switched in Settings (`Ctrl+,`) > Appearance.

### 3.4 Saving a picture of the window (optional)

For screenshots in the report, DevPad can save an image of its own window and close again. It then uses
separate, throw-away settings, so your session and recent files are not changed.

**PowerShell**

```powershell
$env:DEVPAD_SNAPSHOT = "$PWD\devpad.png"; $env:DEVPAD_THEME = "jellyfish"; $env:DEVPAD_PANELS = "terminal outline"
.\build\devpad.exe . .\gui\CodeView.cpp
Remove-Item Env:DEVPAD_SNAPSHOT, Env:DEVPAD_THEME, Env:DEVPAD_PANELS
```

**Terminal**

```bash
DEVPAD_SNAPSHOT="$PWD/devpad.png" DEVPAD_THEME=jellyfish DEVPAD_PANELS="terminal outline" ./build/devpad.exe . gui/CodeView.cpp
```

`DEVPAD_THEME` takes a theme id (`midnight`, `daylight`, `nord`, `solarized-light`, `dracula`, `alucard`, `monokai`,
`paper`, `gruvbox-dark`, `gruvbox-light`, `tokyo-night`, `one-light`, `jellyfish`, `jellyfish-light`).
`DEVPAD_PANELS` may contain `terminal`, `problems`, `outline` and `history`. Both are optional.

### 3.5 Run the tests

**PowerShell**

```powershell
ctest --test-dir build --output-on-failure
```

**Terminal**

```bash
ctest --test-dir build --output-on-failure
```

`ctest` runs the engine tests and the GUI tests. The GUI tests run without a window (`QT_QPA_PLATFORM=offscreen`).

---

## 4. Known difficulties and their solutions

| Problem | Solution |
|---|---|
| `fatal error: filesystem: No such file or directory` | The compiler is too old. Use MinGW-w64 GCC 9+ (for example `C:\Qt\Tools\mingw1310_64`) and pass it with `-DCMAKE_CXX_COMPILER=...` |
| CMake reports that the C++ compiler is broken, or the build mixes two compilers | CMake keeps the first compiler it found in the build folder. Delete the build folder (`Remove-Item -Recurse -Force .\build` or `rm -rf build`) and configure again with the Qt compiler |
| CMake cannot find Qt (`Qt not found - skipping`) | Pass the Qt folder: `-DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/mingw_64"` |
| `devpad.exe` reports missing `Qt6Core.dll`, `Qt6Gui.dll` or `Qt6Widgets.dll` | Run `& "C:\Qt\6.12.0\mingw_64\bin\windeployqt.exe" "build\devpad.exe"`, then start the program from `build\`. Also check that `build\platforms\qwindows.dll` exists |
| Missing `libgcc_s_seh-1.dll`, `libstdc++-6.dll` or `libwinpthread-1.dll` | Copy them from `C:\Qt\Tools\mingw1310_64\bin` into the `build` folder |
| `gui_tests` fails to start when run by hand (for example exit code `-1073740286`, without any output) | Run it through `ctest`, which sets `QT_QPA_PLATFORM=offscreen`. If it still fails, copy `C:\Qt\6.12.0\mingw_64\plugins\platforms\qoffscreen.dll` into `build\platforms\` |
| Rebuilding stops with `ld returned 1 exit status` while linking `devpad.exe` | DevPad is still running, and Windows does not allow a running program to be replaced. Please close every DevPad window and build again |
| A Windows PowerShell build stops with `engine/*.cpp: No such file` | PowerShell does not expand `*`; please use the PowerShell command in Section 2.1, which lists the files itself |
| `windeployqt` warns about `dxcompiler.dll`, `dxil.dll` or skips `qopensslbackend.dll` | These warnings can be ignored; DevPad does not use those components |
| Compiler warnings during the build | Warnings do not stop the build; only errors do |
| The `check` command or the Problems tab reports that the compiler was not found | Add the compiler folder to `PATH`, for example `$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$env:PATH"` (PowerShell) or `export PATH="/c/Qt/Tools/mingw1310_64/bin:$PATH"` (Git Bash) |

Thank you for taking the time to build and evaluate DevPad.
