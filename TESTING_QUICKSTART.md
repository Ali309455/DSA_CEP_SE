# Quick start (copy / paste)

```
cmake -S . -B build            # add -DCMAKE_PREFIX_PATH=<Qt folder> if Qt is not found
cmake --build build
ctest --test-dir build --output-on-failure
./build/devpad
```
Windows MSYS2: `cmake -S . -B build -G Ninja` and run `./build/devpad.exe`.
Details, the manual checklist and the list of handled errors: see `PHASE5_REPORT.md`.

## Windows: "Building for: NMake Makefiles ... nmake failed"
That means CMake picked the Visual Studio generator but you are not in a Visual Studio prompt.
Pick MinGW explicitly (same toolchain as phases 1-4). Either use a preset:
```
cmake --preset mingw          # or: cmake --preset mingw-make   (if you have no Ninja)
cmake --build build
```
or the plain flags:
```
cmake -S . -B build -G Ninja              # or -G "MinGW Makefiles"
cmake --build build
```
Requirements: `g++` and `cmake` (and `ninja` for the first form) on PATH - easiest is the **MSYS2 MinGW64** shell with
`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-qt6-base`.
If you use the Qt installer's MinGW kit instead, add `-DCMAKE_PREFIX_PATH=C:/Qt/<version>/mingw_64` and put
`C:/Qt/Tools/mingw<version>_64/bin` and `C:/Qt/<version>/mingw_64/bin` on PATH.
If you delete `build/` first when switching generators: a folder configured with NMake cannot be reused.

## Windows with the Qt installer (no Qt Creator, no PATH, no Qt path)
If Qt and its MinGW compiler were installed to `C:\Qt` by the Qt installer, CMake now finds Qt by itself
(it searches `C:\Qt\6.*\mingw_64`, newest first) and the `qt` preset puts the compiler on PATH for you.
In an ordinary Command Prompt, inside the project folder:
```
rmdir /s /q build
cmake --preset qt
cmake --build build
build\devpad.exe
```
The Qt DLLs are copied next to `devpad.exe` after the build, so the exe also starts by double click.
Needs the Qt installer's `Tools\mingw1310_64` and `Tools\Ninja`; if your compiler folder has another name,
edit the two `mingw1310_64` entries in `CMakePresets.json` (the `qt` preset).
