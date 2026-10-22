# The tests assert with plain assert(), so every test is compiled with NDEBUG
# undefined, whatever the build type (a Release build defines it). Each test
# source still refuses to compile if NDEBUG reaches it.

function(uxid_add_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE uxid::uxid)
  if(MSVC)
    set(_undefine_ndebug /UNDEBUG)
  else()
    set(_undefine_ndebug -UNDEBUG)
  endif()
  target_compile_options(${name} PRIVATE ${UXID_WARNING_FLAGS}
    ${_undefine_ndebug})
  target_compile_definitions(${name} PRIVATE
    UXID_TEST_PROJECT_VERSION="${PROJECT_VERSION}")
  set_target_properties(${name} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

uxid_add_test(test_version)
uxid_add_test(test_generate)
uxid_add_test(test_deterministic)
uxid_add_test(test_decode)
uxid_add_test(test_decode_fuzz)
uxid_add_test(test_monotonic)
# test_monotonic starts threads of its own: pthreads on POSIX, the Windows
# thread API on Windows.
if(NOT WIN32)
  target_link_libraries(test_monotonic PRIVATE Threads::Threads)
endif()

# test_sha256 checks the library's SHA-256 against the FIPS 180-4 example
# vectors. Its functions are static in src/uxid.c, so the test compiles them
# from a header copied out of that source: the region between the
# "sha256: begin" and "sha256: end" markers, copied again whenever the source
# changes. The test links nothing from the library.
set(_uxid_source_file ${PROJECT_SOURCE_DIR}/src/uxid.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_uxid_source_file})
file(READ ${_uxid_source_file} _uxid_source)
string(FIND "${_uxid_source}" "/* sha256: begin" _uxid_sha_begin)
string(FIND "${_uxid_source}" "/* sha256: end */" _uxid_sha_end)
if(_uxid_sha_begin EQUAL -1 OR _uxid_sha_end EQUAL -1 OR _uxid_sha_end LESS _uxid_sha_begin)
  message(FATAL_ERROR "src/uxid.c has no sha256: begin and sha256: end markers, in that order")
endif()
math(EXPR _uxid_sha_length "${_uxid_sha_end} - ${_uxid_sha_begin}")
string(SUBSTRING "${_uxid_source}" ${_uxid_sha_begin} ${_uxid_sha_length} _uxid_sha_region)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/uxid_sha256_region.h.new
  "/* Copied by tests/CMakeLists.txt from src/uxid.c; do not edit. */\n${_uxid_sha_region}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/uxid_sha256_region.h.new
  ${CMAKE_CURRENT_BINARY_DIR}/generated/uxid_sha256_region.h COPYONLY)

add_executable(test_sha256 test_sha256.c)
target_include_directories(test_sha256 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(MSVC)
  set(_uxid_sha_undefine_ndebug /UNDEBUG)
else()
  set(_uxid_sha_undefine_ndebug -UNDEBUG)
endif()
target_compile_options(test_sha256 PRIVATE ${UXID_WARNING_FLAGS} ${_uxid_sha_undefine_ndebug})
set_target_properties(test_sha256 PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
add_test(NAME test_sha256 COMMAND test_sha256)

# Installs the library to a scratch prefix under the build tree, then builds
# and runs a consumer through find_package and, with a compiler that takes
# the -I, -L and -l options pkg-config prints, through pkg-config.
if(UXID_INSTALL)
  add_test(NAME install_consume
    COMMAND ${CMAKE_COMMAND}
      -DUXID_BINARY_DIR=${PROJECT_BINARY_DIR}
      -DUXID_CONFIG=$<CONFIG>
      -DUXID_SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/install_consume
      -DUXID_CONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
      -DUXID_EXPECTED_VERSION=${PROJECT_VERSION}
      -DUXID_GENERATOR=${CMAKE_GENERATOR}
      -DUXID_LIBDIR=${CMAKE_INSTALL_LIBDIR}
      -DUXID_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}
      -DUXID_C_COMPILER=${CMAKE_C_COMPILER}
      -DUXID_MSVC_STYLE=$<BOOL:${MSVC}>
      "-DUXID_C_FLAGS=${CMAKE_C_FLAGS}"
      "-DUXID_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/install_consume.cmake)
endif()
