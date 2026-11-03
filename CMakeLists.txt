cmake_minimum_required(VERSION 3.16)
project(TextEditor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()

# ---- Engine: all the data structures and algorithms. No GUI code in here. ----
add_library(engine STATIC
  engine/Assist.cpp
  engine/Brackets.cpp
  engine/Document.cpp
  engine/Editor.cpp
  engine/FileIO.cpp
  engine/Search.cpp
  engine/Trie.cpp)
target_include_directories(engine PUBLIC engine)

# ---- Console test suite (use its output as evidence in the report) ----
add_executable(engine_tests tests/test_main.cpp)
target_link_libraries(engine_tests PRIVATE engine)

# ---- Console editor ----
add_executable(console_editor app/console_main.cpp)
target_link_libraries(console_editor PRIVATE engine)

if(MINGW)
  # single .exe, no missing-DLL errors on another PC
  target_link_options(engine_tests PRIVATE -static)
  target_link_options(console_editor PRIVATE -static)
endif()

enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)

# Snippets (hash table) are in next_part_snippets/ and join the build in part 4b.
# The GUI (Dear ImGui + GLFW) is added in a later phase.
