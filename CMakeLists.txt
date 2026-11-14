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
  # Find a Qt installed by the Qt installer without the user passing -DCMAKE_PREFIX_PATH.
  # (A path you pass yourself, or the CMAKE_PREFIX_PATH / QTDIR environment variables, win.)
  set(_qt_roots C:/Qt D:/Qt E:/Qt "$ENV{USERPROFILE}/Qt" "$ENV{HOME}/Qt" /opt/Qt)
  if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(_qt_kits msvc2022_64 msvc2019_64)
  elseif(WIN32)
    set(_qt_kits mingw_64)
  elseif(APPLE)
    set(_qt_kits macos)
  else()
    set(_qt_kits gcc_64)
  endif()
  set(_qt_found "")
  foreach(_root IN LISTS _qt_roots)
    foreach(_kit IN LISTS _qt_kits)
      file(GLOB _cands "${_root}/6.*/${_kit}")
      list(APPEND _qt_found ${_cands})
    endforeach()
  endforeach()
  if(_qt_found)
    list(SORT _qt_found COMPARE NATURAL ORDER DESCENDING)     # newest Qt first
    list(GET _qt_found 0 _qt_best)
    list(APPEND CMAKE_PREFIX_PATH "${_qt_best}")
    message(STATUS "Qt search path added automatically: ${_qt_best}")
  endif()
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
      gui/CodeEditor.cpp gui/CppHighlighter.cpp gui/FileExplorer.cpp gui/FindBar.cpp gui/MainWindow.cpp
      gui/RunManager.cpp gui/SettingsDialog.cpp gui/SnippetDialog.cpp)
    target_include_directories(devpad_gui PUBLIC gui)
    target_link_libraries(devpad_gui PUBLIC engine ${QT_NS}::Widgets)

    add_executable(devpad WIN32 gui/main.cpp)
    target_link_libraries(devpad PRIVATE devpad_gui)

    if(WIN32)   # copy the Qt DLLs next to devpad.exe (best effort, never fails the build)
      find_program(WINDEPLOYQT NAMES windeployqt6 windeployqt
                   HINTS "${CMAKE_PREFIX_PATH}/bin" "${Qt6_DIR}/../../../bin" "${Qt5_DIR}/../../../bin")
      get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
      add_custom_command(TARGET devpad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DTOOL=${WINDEPLOYQT} -DEXE=$<TARGET_FILE:devpad> -DCOMPILER_DIR=${_cxx_dir}
                -P ${CMAKE_SOURCE_DIR}/cmake/DeployQt.cmake
        VERBATIM)
    endif()

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
