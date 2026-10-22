# Run by CTest as `cmake -P`: installs the built library to a scratch prefix,
# checks the installed files, then builds and runs a consumer through
# find_package(uxid CONFIG) and, unless the compiler takes cl's command-line
# style (cl or clang-cl, UXID_MSVC_STYLE), through pkg-config: the -I, -L
# and -l options pkg-config prints are not cl's, and pkg-config is not part
# of a Windows toolchain.

foreach(_var IN ITEMS UXID_BINARY_DIR UXID_SCRATCH UXID_CONSUMER_SOURCE
                      UXID_EXPECTED_VERSION UXID_GENERATOR UXID_C_COMPILER
                      UXID_LIBDIR UXID_INCLUDEDIR)
  if("${${_var}}" STREQUAL "")
    message(FATAL_ERROR "install_consume.cmake needs -D${_var}=...")
  endif()
endforeach()

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

function(uxid_expect_version output how)
  string(STRIP "${output}" _printed)
  if(NOT _printed STREQUAL UXID_EXPECTED_VERSION)
    message(FATAL_ERROR
      "the consumer built through ${how} printed \"${_printed}\", "
      "expected \"${UXID_EXPECTED_VERSION}\"")
  endif()
endfunction()

set(_prefix "${UXID_SCRATCH}/prefix")
file(REMOVE_RECURSE "${UXID_SCRATCH}")

set(_config_args "")
if(NOT "${UXID_CONFIG}" STREQUAL "")
  set(_config_args --config "${UXID_CONFIG}")
endif()
uxid_run("${CMAKE_COMMAND}" --install "${UXID_BINARY_DIR}"
  --prefix "${_prefix}" ${_config_args})

# The installed layout: the header, the package files and the .pc file.
foreach(_file IN ITEMS
    ${UXID_INCLUDEDIR}/uxid.h
    ${UXID_LIBDIR}/cmake/uxid/uxidConfig.cmake
    ${UXID_LIBDIR}/cmake/uxid/uxidConfigVersion.cmake
    ${UXID_LIBDIR}/cmake/uxid/uxidTargets.cmake
    ${UXID_LIBDIR}/pkgconfig/uxid.pc)
  if(NOT EXISTS "${_prefix}/${_file}")
    message(FATAL_ERROR "the install did not write ${_file}")
  endif()
endforeach()
file(GLOB _libs "${_prefix}/${UXID_LIBDIR}/*uxid*")
list(FILTER _libs EXCLUDE REGEX "/(cmake|pkgconfig)$")
if(_libs STREQUAL "")
  message(FATAL_ERROR "the install wrote no library under ${UXID_LIBDIR}/")
endif()

# 1. find_package(uxid CONFIG), from the scratch prefix only. A Visual Studio
# generator picks its compiler from the toolset, not from CMAKE_C_COMPILER.
set(_cmake_build "${UXID_SCRATCH}/find_package")
set(_compiler_arg "-DCMAKE_C_COMPILER=${UXID_C_COMPILER}")
if(UXID_GENERATOR MATCHES "^Visual Studio")
  set(_compiler_arg "")
endif()
uxid_run("${CMAKE_COMMAND}"
  -S "${UXID_CONSUMER_SOURCE}" -B "${_cmake_build}"
  -G "${UXID_GENERATOR}"
  ${_compiler_arg}
  "-DCMAKE_C_FLAGS=${UXID_C_FLAGS}"
  "-DCMAKE_EXE_LINKER_FLAGS=${UXID_EXE_LINKER_FLAGS}"
  "-DCMAKE_PREFIX_PATH=${_prefix}"
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
  "-DUXID_EXPECTED_VERSION=${UXID_EXPECTED_VERSION}")
uxid_run("${CMAKE_COMMAND}" --build "${_cmake_build}" ${_config_args})
find_program(_consumer NAMES consumer
  PATHS "${_cmake_build}" "${_cmake_build}/${UXID_CONFIG}"
  NO_DEFAULT_PATH)
if(NOT _consumer)
  message(FATAL_ERROR "the find_package consumer was not built")
endif()
uxid_run("${_consumer}")
uxid_expect_version("${UXID_RUN_OUTPUT}" "find_package")

# 2. pkg-config, reading only the installed uxid.pc.
if(UXID_MSVC_STYLE)
  message(STATUS "pkg-config route not run: the compiler takes cl's "
                 "command-line style, not the options pkg-config prints")
  return()
endif()
find_program(_pkg_config NAMES pkg-config pkgconf REQUIRED)
set(ENV{PKG_CONFIG_LIBDIR} "${_prefix}/${UXID_LIBDIR}/pkgconfig")
unset(ENV{PKG_CONFIG_PATH})
uxid_run("${_pkg_config}" --modversion uxid)
string(STRIP "${UXID_RUN_OUTPUT}" _modversion)
if(NOT _modversion STREQUAL UXID_EXPECTED_VERSION)
  message(FATAL_ERROR
    "pkg-config reports uxid ${_modversion}, expected ${UXID_EXPECTED_VERSION}")
endif()
uxid_run("${_pkg_config}" --cflags --libs uxid)
separate_arguments(_pc_flags NATIVE_COMMAND "${UXID_RUN_OUTPUT}")
separate_arguments(_c_flags NATIVE_COMMAND "${UXID_C_FLAGS}")
separate_arguments(_ld_flags NATIVE_COMMAND "${UXID_EXE_LINKER_FLAGS}")
set(_pc_build "${UXID_SCRATCH}/pkg_config")
file(MAKE_DIRECTORY "${_pc_build}")
set(_pc_consumer "${_pc_build}/consumer${CMAKE_EXECUTABLE_SUFFIX}")
uxid_run("${UXID_C_COMPILER}" ${_c_flags} -std=c11
  "${UXID_CONSUMER_SOURCE}/main.c" ${_pc_flags} ${_ld_flags}
  -o "${_pc_consumer}")
# A shared library under a scratch prefix is found the way a pkg-config user
# finds one outside the system paths: through the loader's search path.
set(ENV{DYLD_LIBRARY_PATH} "${_prefix}/${UXID_LIBDIR}")
set(ENV{LD_LIBRARY_PATH} "${_prefix}/${UXID_LIBDIR}")
uxid_run("${_pc_consumer}")
uxid_expect_version("${UXID_RUN_OUTPUT}" "pkg-config")
