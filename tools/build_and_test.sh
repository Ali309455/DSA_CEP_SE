#!/bin/sh
# One command, no CMake needed:  sh tools/build_and_test.sh        (Linux / macOS / MSYS2 shell)
# Add  san  to also run the whole suite under AddressSanitizer + UBSan:  sh tools/build_and_test.sh san
set -e
cd "$(dirname "$0")/.."
mkdir -p build_quick
SRC="engine/Assist.cpp engine/Brackets.cpp engine/Document.cpp engine/Editor.cpp engine/FileIO.cpp engine/Search.cpp engine/Snippets.cpp engine/Trie.cpp"
echo "== normal build (-Wall -Wextra -pedantic) =="
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -Iengine $SRC tests/test_main.cpp -o build_quick/engine_tests
(cd build_quick && ./engine_tests | tail -3)
if [ "$1" = "san" ]; then
  echo "== AddressSanitizer + UBSan build =="
  g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Iengine $SRC tests/test_main.cpp -o build_quick/engine_tests_san
  (cd build_quick && ./engine_tests_san | tail -3)
fi
echo "ALL GOOD"
