# Line coverage of the library source (src/) from the coverage preset's
# CTest run, with clang's source-based coverage, at a minimum percentage.
#
#   cmake -DUXID_BUILD_DIR=build/coverage -DUXID_SOURCE_DIR=. \
#         -DUXID_MIN_LINES=90 -P scripts/coverage.cmake
#
# The floor is never met by an empty measurement: when no line of src/ was
# measured, the run passes only while the built library defines no external
# symbol (a library with no code yet); a library that defines one and has no
# measured line fails. Once a line is measured, every C source under src/
# must appear in the export.

foreach(_var IN ITEMS UXID_BUILD_DIR UXID_SOURCE_DIR UXID_MIN_LINES)
  if("${${_var}}" STREQUAL "")
    message(FATAL_ERROR "coverage.cmake needs -D${_var}=...")
  endif()
endforeach()
get_filename_component(_build "${UXID_BUILD_DIR}" ABSOLUTE)
get_filename_component(_source "${UXID_SOURCE_DIR}" REALPATH)

# The C compiler the coverage build used, from its CMake cache, followed to
# its real file: on Linux /usr/bin/clang is a link into the LLVM directory
# that also holds that release's llvm-profdata and llvm-cov, whose profile
# format matches the compiler's.
set(_compiler_dir "")
if(EXISTS "${_build}/CMakeCache.txt")
  file(STRINGS "${_build}/CMakeCache.txt" _compiler_line
       REGEX "^CMAKE_C_COMPILER:[A-Z]+=")
  if(_compiler_line MATCHES "^CMAKE_C_COMPILER:[A-Z]+=(.+)$")
    get_filename_component(_compiler_real "${CMAKE_MATCH_1}" REALPATH)
    get_filename_component(_compiler_dir "${_compiler_real}" DIRECTORY)
  endif()
endif()

# The LLVM tools: through xcrun on macOS, else from the compiler's real
# directory, else from PATH. Each tool has its own result variable, since
# find_program does not search again for a variable that is already set.
function(uxid_llvm_tool out name)
  find_program(_xcrun xcrun)
  if(_xcrun)
    execute_process(COMMAND "${_xcrun}" -f ${name}
      OUTPUT_VARIABLE _path OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _result ERROR_QUIET)
    if(_result EQUAL 0 AND EXISTS "${_path}")
      set(${out} "${_path}" PARENT_SCOPE)
      return()
    endif()
  endif()
  if(NOT _compiler_dir STREQUAL "")
    find_program(_uxid_beside_${name} NAMES ${name}
      PATHS "${_compiler_dir}" NO_DEFAULT_PATH)
    if(_uxid_beside_${name})
      set(${out} "${_uxid_beside_${name}}" PARENT_SCOPE)
      return()
    endif()
  endif()
  find_program(_uxid_path_${name} NAMES ${name})
  if(NOT _uxid_path_${name})
    message(FATAL_ERROR "coverage needs ${name}, found neither through xcrun, "
                        "beside the compiler (${_compiler_dir}) nor on PATH")
  endif()
  set(${out} "${_uxid_path_${name}}" PARENT_SCOPE)
endfunction()

function(uxid_run)
  execute_process(COMMAND ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
  if(NOT _result EQUAL 0)
    string(REPLACE ";" " " _cmd "${ARGN}")
    message(FATAL_ERROR "failed (${_result}): ${_cmd}\n${_out}\n${_err}")
  endif()
  set(UXID_RUN_OUTPUT "${_out}" PARENT_SCOPE)
endfunction()

uxid_llvm_tool(_profdata llvm-profdata)
uxid_llvm_tool(_cov llvm-cov)
uxid_llvm_tool(_nm llvm-nm)
foreach(_tool IN ITEMS _profdata _cov _nm)
  message(STATUS "coverage: ${_tool} is ${${_tool}}")
endforeach()

file(GLOB _profiles "${_build}/profiles/*.profraw")
if(_profiles STREQUAL "")
  message(FATAL_ERROR
    "no profiles under ${_build}/profiles: run the coverage preset's CTest first")
endif()
set(_merged "${_build}/profiles/merged.profdata")
uxid_run("${_profdata}" merge -sparse ${_profiles} -o "${_merged}")

file(GLOB _tests "${_build}/tests/test_*")
list(FILTER _tests EXCLUDE REGEX "\\.(dSYM|pdb|ilk)$")
if(_tests STREQUAL "")
  message(FATAL_ERROR "no test executables under ${_build}/tests")
endif()
list(POP_FRONT _tests _first)
set(_objects "")
foreach(_test IN LISTS _tests)
  list(APPEND _objects -object "${_test}")
endforeach()

uxid_run("${_cov}" export -summary-only "-instr-profile=${_merged}"
  "${_first}" ${_objects})
# Sum the line counts of the files under src/ only; the tests' own lines are
# in the export too and never count toward the floor.
set(_count 0)
set(_covered 0)
set(_measured "")
string(JSON _files_len LENGTH "${UXID_RUN_OUTPUT}" data 0 files)
if(_files_len GREATER 0)
  math(EXPR _last "${_files_len} - 1")
  foreach(_i RANGE ${_last})
    string(JSON _name GET "${UXID_RUN_OUTPUT}" data 0 files ${_i} filename)
    get_filename_component(_name "${_name}" REALPATH)
    string(FIND "${_name}" "${_source}/src/" _at)
    if(NOT _at EQUAL 0)
      continue()
    endif()
    string(JSON _file_count GET "${UXID_RUN_OUTPUT}" data 0 files ${_i} summary lines count)
    string(JSON _file_covered GET "${UXID_RUN_OUTPUT}" data 0 files ${_i} summary lines covered)
    math(EXPR _count "${_count} + ${_file_count}")
    math(EXPR _covered "${_covered} + ${_file_covered}")
    file(RELATIVE_PATH _rel "${_source}" "${_name}")
    list(APPEND _measured "${_rel} ${_file_covered}/${_file_count}")
  endforeach()
endif()
foreach(_line IN LISTS _measured)
  message(STATUS "coverage: ${_line}")
endforeach()

# The export reads the test executables, so a source the tests never link
# would be missing from it and from the floor's denominator: once anything is
# measured, every C source under src/ must be in the export.
if(NOT _count EQUAL 0)
  file(GLOB _sources RELATIVE "${_source}" "${_source}/src/*.c")
  foreach(_src IN LISTS _sources)
    set(_found FALSE)
    foreach(_line IN LISTS _measured)
      if(_line MATCHES "^${_src} ")
        set(_found TRUE)
      endif()
    endforeach()
    if(NOT _found)
      message(FATAL_ERROR
        "coverage: ${_src} is not in the coverage export: no test links its code")
    endif()
  endforeach()
endif()

if(_count EQUAL 0)
  file(GLOB _libs "${_build}/libuxid.a" "${_build}/libuxid.so*"
       "${_build}/libuxid*.dylib" "${_build}/uxid.lib")
  if(_libs STREQUAL "")
    message(FATAL_ERROR "no built uxid library under ${_build}")
  endif()
  set(_symbols "")
  foreach(_lib IN LISTS _libs)
    uxid_run("${_nm}" --extern-only --defined-only --just-symbol-name "${_lib}")
    string(REGEX MATCHALL "[^\n]+" _lines "${UXID_RUN_OUTPUT}")
    foreach(_line IN LISTS _lines)
      # Skip archive member headers and the profiling runtime's own symbols.
      if(_line MATCHES ":$" OR _line MATCHES "^_*(llvm_|covrec_)")
        continue()
      endif()
      list(APPEND _symbols "${_line}")
    endforeach()
  endforeach()
  if(NOT _symbols STREQUAL "")
    list(JOIN _symbols ", " _names)
    message(FATAL_ERROR
      "coverage: the library defines ${_names} but no line of src/ was measured")
  endif()
  message(STATUS "coverage: src/ has no executable line yet "
                 "(the library defines no external symbol); nothing to measure")
  return()
endif()

math(EXPR _percent_x100 "(${_covered} * 10000) / ${_count}")
math(EXPR _whole "${_percent_x100} / 100")
math(EXPR _frac "${_percent_x100} % 100")
if(_frac LESS 10)
  set(_frac "0${_frac}")
endif()
math(EXPR _needed "${UXID_MIN_LINES} * ${_count}")
math(EXPR _have "${_covered} * 100")
if(_have LESS _needed)
  message(FATAL_ERROR
    "coverage: ${_covered} of ${_count} lines of src/ (${_whole}.${_frac}%), "
    "below the ${UXID_MIN_LINES}% minimum")
endif()
message(STATUS
  "coverage: ${_covered} of ${_count} lines of src/ (${_whole}.${_frac}%), "
  "minimum ${UXID_MIN_LINES}%")
