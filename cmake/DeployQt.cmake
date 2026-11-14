# Run by a post-build step on Windows: copies the Qt DLLs next to devpad.exe so that it starts
# by double click. Never fails the build: if anything is missing it only prints a note.
if(NOT TOOL OR NOT EXISTS "${TOOL}")
  message(STATUS "windeployqt not found: run devpad.exe from a shell that has Qt's bin folder on PATH")
  return()
endif()
get_filename_component(_tooldir "${TOOL}" DIRECTORY)
set(ENV{PATH} "${_tooldir};${COMPILER_DIR};$ENV{PATH}")
execute_process(COMMAND "${TOOL}" --no-translations --no-system-d3d-compiler --no-opengl-sw "${EXE}"
                RESULT_VARIABLE _r OUTPUT_QUIET ERROR_QUIET)
if(_r EQUAL 0)
  get_filename_component(_exedir "${EXE}" DIRECTORY)
  # Ensure offscreen platform plugin and Qt6Test DLL exist for headless tests
  if(EXISTS "${_tooldir}/../plugins/platforms/qoffscreen.dll")
    file(COPY "${_tooldir}/../plugins/platforms/qoffscreen.dll" DESTINATION "${_exedir}/platforms")
  endif()
  if(EXISTS "${_tooldir}/Qt6Test.dll")
    file(COPY "${_tooldir}/Qt6Test.dll" DESTINATION "${_exedir}")
  endif()
  message(STATUS "Qt DLLs copied next to devpad.exe")
else()
  message(STATUS "windeployqt returned ${_r}: run devpad.exe from a shell that has Qt's bin folder on PATH")
endif()
