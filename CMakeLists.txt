cmake_minimum_required(VERSION 3.20)

# The version is read from include/uxid.h, the one place it is written, so
# the CMake package version, the pkg-config version and the header cannot
# differ.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/include/uxid.h" _uxid_version_lines
     REGEX "^#define UXID_VERSION_(MAJOR|MINOR|PATCH|STRING) ")
# A change to the header, a version bump included, re-runs the configure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/include/uxid.h")
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

# The tests build when uxid is the top-level project, unless UXID_BUILD_TESTS
# or the CTest convention BUILD_TESTING is turned off.
option(UXID_BUILD_TESTS "Build the uxid tests" ${_uxid_top_level})
option(UXID_INSTALL "Generate the uxid install rules" ON)
# Warnings are errors when uxid is the top-level project (the gate and CI);
# a project that consumes it through add_subdirectory or FetchContent gets
# the warnings without -Werror or /WX, so a newer compiler's new warning does
# not break that project's build.
option(UXID_WARNINGS_AS_ERRORS "Treat warnings in the uxid build as errors"
  ${_uxid_top_level})

# The warning flags follow the compiler's command-line style, not its id:
# clang-cl reports the id Clang but takes cl's options (it reads -Wall as
# -Weverything), and CMake's MSVC variable is true for cl and clang-cl alike.
if(MSVC)
  set(UXID_WARNING_FLAGS /W4)
  if(UXID_WARNINGS_AS_ERRORS)
    list(APPEND UXID_WARNING_FLAGS /WX)
  endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  set(UXID_WARNING_FLAGS -Wall -Wextra -Wpedantic)
  if(UXID_WARNINGS_AS_ERRORS)
    list(APPEND UXID_WARNING_FLAGS -Werror)
  endif()
else()
  set(UXID_WARNING_FLAGS "")
endif()

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
  EXPORT_NAME uxid
  C_VISIBILITY_PRESET hidden)

# A shared library exports the functions uxid.h marks with UXID_API: on
# Windows through __declspec(dllexport) while it is built, elsewhere through
# default visibility over the hidden preset above.
get_target_property(_uxid_type uxid TYPE)
if(_uxid_type STREQUAL "SHARED_LIBRARY")
  target_compile_definitions(uxid PRIVATE UXID_BUILDING_SHARED)
endif()

# The default byte source on Windows is BCryptGenRandom, from bcrypt. On
# POSIX the monotonic generator's lock is a pthread mutex, from the platform
# thread library, linked with -pthread; on Windows it is an SRWLOCK, from the
# system libraries every program links.
set(UXID_PC_LIBS_PRIVATE "")
set(UXID_NEEDS_THREADS OFF)
if(WIN32)
  target_link_libraries(uxid PRIVATE bcrypt)
  set(UXID_PC_LIBS_PRIVATE "-lbcrypt")
else()
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(uxid PRIVATE Threads::Threads)
  set(UXID_PC_LIBS_PRIVATE "-pthread")
  set(UXID_NEEDS_THREADS ON)
endif()

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
  # tree can be moved or installed with `cmake --install --prefix`. The
  # relative path is taken under the build directory, an absolute path on
  # every platform ("/" is not one on Windows).
  file(RELATIVE_PATH UXID_PC_PREFIX_FROM_PCFILEDIR
    "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    "${PROJECT_BINARY_DIR}")
  string(REGEX REPLACE "/$" "" UXID_PC_PREFIX_FROM_PCFILEDIR
    "${UXID_PC_PREFIX_FROM_PCFILEDIR}")
  configure_file(cmake/uxid.pc.in ${PROJECT_BINARY_DIR}/uxid.pc @ONLY)
  install(FILES ${PROJECT_BINARY_DIR}/uxid.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(UXID_BUILD_TESTS AND (NOT DEFINED BUILD_TESTING OR BUILD_TESTING))
  enable_testing()
  add_subdirectory(tests)
endif()
