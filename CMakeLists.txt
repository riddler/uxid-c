cmake_minimum_required(VERSION 3.20)

# The version is read from include/uxid.h, the one place it is written, so
# the CMake package version, the pkg-config version and the header cannot
# differ.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/include/uxid.h" _uxid_version_lines
     REGEX "^#define UXID_VERSION_(MAJOR|MINOR|PATCH|STRING) ")
foreach(_uxid_part IN ITEMS MAJOR MINOR PATCH)
  set(_uxid_${_uxid_part} "")
  foreach(_uxid_line IN LISTS _uxid_version_lines)
    if(_uxid_line MATCHES "^#define UXID_VERSION_${_uxid_part} +([0-9]+)$")
      set(_uxid_${_uxid_part} "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(_uxid_${_uxid_part} STREQUAL "")
    message(FATAL_ERROR "include/uxid.h has no integer UXID_VERSION_${_uxid_part}")
  endif()
endforeach()
set(_uxid_version "${_uxid_MAJOR}.${_uxid_MINOR}.${_uxid_PATCH}")
set(_uxid_string "")
foreach(_uxid_line IN LISTS _uxid_version_lines)
  if(_uxid_line MATCHES "^#define UXID_VERSION_STRING +\"([^\"]*)\"$")
    set(_uxid_string "${CMAKE_MATCH_1}")
  endif()
endforeach()
if(NOT _uxid_string STREQUAL _uxid_version)
  message(FATAL_ERROR
    "include/uxid.h: UXID_VERSION_STRING \"${_uxid_string}\" differs from "
    "the version macros (${_uxid_version})")
endif()

project(uxid VERSION ${_uxid_version} LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(_uxid_top_level ON)
else()
  set(_uxid_top_level OFF)
endif()

option(UXID_BUILD_TESTS "Build the uxid tests" ${_uxid_top_level})
option(UXID_INSTALL "Generate the uxid install rules" ON)

set(UXID_WARNING_FLAGS
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>)

# BUILD_SHARED_LIBS decides between a static and a shared library.
add_library(uxid src/uxid.c)
add_library(uxid::uxid ALIAS uxid)
target_include_directories(uxid
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(uxid PUBLIC c_std_11)
target_compile_options(uxid PRIVATE ${UXID_WARNING_FLAGS})
set_target_properties(uxid PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME uxid)

if(UXID_INSTALL)
  install(TARGETS uxid
    EXPORT uxidTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES include/uxid.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  set(UXID_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/uxid)
  install(EXPORT uxidTargets
    NAMESPACE uxid::
    DESTINATION ${UXID_CMAKE_DIR})
  configure_package_config_file(
    cmake/uxidConfig.cmake.in
    ${PROJECT_BINARY_DIR}/uxidConfig.cmake
    INSTALL_DESTINATION ${UXID_CMAKE_DIR})
  write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/uxidConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(FILES
    ${PROJECT_BINARY_DIR}/uxidConfig.cmake
    ${PROJECT_BINARY_DIR}/uxidConfigVersion.cmake
    DESTINATION ${UXID_CMAKE_DIR})

  # The .pc file locates its prefix from its own directory, so the installed
  # tree can be moved or installed with `cmake --install --prefix`.
  file(RELATIVE_PATH UXID_PC_PREFIX_FROM_PCFILEDIR
    "/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/")
  string(REGEX REPLACE "/$" "" UXID_PC_PREFIX_FROM_PCFILEDIR
    "${UXID_PC_PREFIX_FROM_PCFILEDIR}")
  configure_file(cmake/uxid.pc.in ${PROJECT_BINARY_DIR}/uxid.pc @ONLY)
  install(FILES ${PROJECT_BINARY_DIR}/uxid.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(UXID_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
