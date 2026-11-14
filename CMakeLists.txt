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
  engine/Snippets.cpp
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

# ---- Phase 5: Qt GUI (DevPad). Needs Qt 6 (or Qt 5.15) Widgets. Turn off with -DBUILD_GUI=OFF ----
option(BUILD_GUI "Build the Qt GUI (phase 5)" ON)
if(BUILD_GUI)
  find_package(Qt6 COMPONENTS Widgets Test QUIET)
  if(Qt6_FOUND)
    set(QT_NS Qt6)
  else()
    find_package(Qt5 5.15 COMPONENTS Widgets Test QUIET)
    if(Qt5_FOUND)
      set(QT_NS Qt5)
    endif()
  endif()
  if(QT_NS)
    message(STATUS "Qt GUI enabled (${QT_NS})")
    set(CMAKE_AUTOMOC ON)
    add_library(devpad_gui STATIC
      gui/CodeEditor.cpp gui/CppHighlighter.cpp gui/FindBar.cpp gui/MainWindow.cpp
      gui/RunManager.cpp gui/SnippetDialog.cpp)
    target_include_directories(devpad_gui PUBLIC gui)
    target_link_libraries(devpad_gui PUBLIC engine ${QT_NS}::Widgets)

    add_executable(devpad WIN32 gui/main.cpp)
    target_link_libraries(devpad PRIVATE devpad_gui)

    if(TARGET ${QT_NS}::Test)
      add_executable(gui_tests tests/gui_tests.cpp)
      target_link_libraries(gui_tests PRIVATE devpad_gui ${QT_NS}::Test)
      add_test(NAME gui_tests COMMAND gui_tests)
      set_tests_properties(gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
      message(STATUS "Qt Test not found: gui_tests skipped")
    endif()
  else()
    message(WARNING "Qt not found: the GUI is skipped (engine, tests and console editor still build). "
                    "Install Qt 6 Widgets or pass -DCMAKE_PREFIX_PATH=<Qt folder>.")
  endif()
endif()
