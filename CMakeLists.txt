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
  engine/EditOps.cpp
  engine/Editor.cpp
  engine/FileIO.cpp
  engine/Search.cpp
  engine/Trie.cpp
  engine/Workspace.cpp)
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

# ---- Phase 5: DevPad, the Qt Widgets GUI (Qt 6, or Qt 5 as a fallback) ----
# Configure with -DBUILD_GUI=OFF to build only the engine, the tests and the console editor.
option(BUILD_GUI "Build the Qt GUI (DevPad)" ON)
if(BUILD_GUI)
  find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets QUIET)
  if(QT_FOUND)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
    message(STATUS "DevPad GUI: building with Qt ${QT_VERSION}")
    add_executable(devpad WIN32
      gui/main.cpp
      gui/MainWindow.cpp
      gui/MainWindow.h
      gui/CodeView.cpp
      gui/CodeView.h
      gui/FileExplorer.cpp
      gui/FileExplorer.h
      gui/WelcomePage.cpp
      gui/WelcomePage.h
      gui/Icons.cpp
      gui/Icons.h
      gui/Theme.cpp
      gui/Theme.h
      gui/WinTheme.cpp
      gui/WinTheme.h
      gui/Runner.cpp gui/Runner.h gui/Settings.cpp
      gui/Settings.h
      gui/Controls.cpp
      gui/Controls.h
      gui/SettingsDialog.cpp
      gui/SettingsDialog.h
      gui/ShortcutsDialog.cpp
      gui/ShortcutsDialog.h
      gui/TerminalPanel.cpp
      gui/TerminalPanel.h)
    set_target_properties(devpad PROPERTIES AUTOMOC ON)
    target_include_directories(devpad PRIVATE gui)
    target_link_libraries(devpad PRIVATE engine Qt${QT_VERSION_MAJOR}::Widgets)
    if(WIN32)
      # dark title bars (dwmapi) and the program icon shown by Explorer / the taskbar
      target_link_libraries(devpad PRIVATE dwmapi)
      enable_language(RC)
      set(DEVPAD_ICON "${CMAKE_CURRENT_SOURCE_DIR}/gui/devpad.ico")
      configure_file(gui/devpad.rc.in "${CMAKE_CURRENT_BINARY_DIR}/devpad.rc" @ONLY)
      target_sources(devpad PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/devpad.rc")

      # Copy the Qt DLLs and plugins next to the .exe after every build, so it starts by double-click
      # without adding anything to PATH by hand. Needs windeployqt, which sits in Qt's bin folder.
      get_target_property(_qt_core_loc Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
      find_program(DEVPAD_WINDEPLOYQT NAMES windeployqt6 windeployqt
                   HINTS "${Qt${QT_VERSION_MAJOR}_DIR}/../../../bin" "${CMAKE_PREFIX_PATH}/bin")
      if(NOT DEVPAD_WINDEPLOYQT AND _qt_core_loc)
        get_filename_component(_qt_bin "${_qt_core_loc}" DIRECTORY)
        find_program(DEVPAD_WINDEPLOYQT NAMES windeployqt6 windeployqt HINTS "${_qt_bin}")
      endif()
      if(DEVPAD_WINDEPLOYQT)
        message(STATUS "DevPad GUI: Qt DLLs will be copied next to the programs by ${DEVPAD_WINDEPLOYQT}")
        add_custom_command(TARGET devpad POST_BUILD
          COMMAND "${DEVPAD_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:devpad>"
          COMMENT "windeployqt: copying Qt DLLs for devpad")
      else()
        message(STATUS "DevPad GUI: windeployqt not found - copy the Qt DLLs next to devpad.exe yourself")
      endif()
    endif()

    # GUI smoke tests: drive the real widgets without a screen (QT_QPA_PLATFORM=offscreen)
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Test)
    if(Qt${QT_VERSION_MAJOR}Test_FOUND)
      add_executable(gui_tests tests/gui_test.cpp gui/MainWindow.cpp gui/MainWindow.h gui/CodeView.cpp gui/CodeView.h gui/FileExplorer.cpp gui/FileExplorer.h gui/WelcomePage.cpp gui/WelcomePage.h
                             gui/Icons.cpp gui/Icons.h gui/Theme.cpp gui/Theme.h gui/WinTheme.cpp gui/WinTheme.h
                             gui/Runner.cpp gui/Runner.h gui/Settings.cpp gui/Settings.h gui/Controls.cpp gui/Controls.h gui/SettingsDialog.cpp gui/SettingsDialog.h gui/ShortcutsDialog.cpp gui/ShortcutsDialog.h gui/TerminalPanel.cpp gui/TerminalPanel.h)
      set_target_properties(gui_tests PROPERTIES AUTOMOC ON)
      target_include_directories(gui_tests PRIVATE gui)
      target_link_libraries(gui_tests PRIVATE engine Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
      if(WIN32)
        target_link_libraries(gui_tests PRIVATE dwmapi)
        if(DEVPAD_WINDEPLOYQT)
          add_custom_command(TARGET gui_tests POST_BUILD
            COMMAND "${DEVPAD_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:gui_tests>"
            COMMENT "windeployqt: copying Qt DLLs for gui_tests")
        endif()
      endif()
      add_test(NAME gui_tests COMMAND gui_tests)
      set_tests_properties(gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
  else()
    message(STATUS "DevPad GUI: Qt not found - skipping (install Qt 6 and set CMAKE_PREFIX_PATH, or use -DBUILD_GUI=OFF)")
  endif()
endif()
