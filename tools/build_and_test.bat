@echo off
rem One command, no CMake needed (MSYS2 MinGW64 g++ on PATH):  tools\build_and_test.bat
cd /d "%~dp0\.."
if not exist build_quick mkdir build_quick
g++ -std=c++17 -O2 -Wall -Wextra -Iengine engine\Assist.cpp engine\Brackets.cpp engine\Document.cpp engine\Editor.cpp engine\FileIO.cpp engine\Search.cpp engine\Snippets.cpp engine\Trie.cpp tests\test_main.cpp -static -o build_quick\engine_tests.exe
if errorlevel 1 exit /b 1
cd build_quick
engine_tests.exe | findstr /C:"checks passed" /C:"FAIL"
cd ..
