# Quick start (copy / paste)

```
cmake -S . -B build            # add -DCMAKE_PREFIX_PATH=<Qt folder> if Qt is not found
cmake --build build
ctest --test-dir build --output-on-failure
./build/devpad
```
Windows MSYS2: `cmake -S . -B build -G Ninja` and run `./build/devpad.exe`.
Details, the manual checklist and the list of handled errors: see `PHASE5_REPORT.md`.
