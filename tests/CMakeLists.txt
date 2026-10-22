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

# The conformance corpus. check_corpus checks the vendored files against their
# manifest and conformance/SOURCE.json (scripts/check_corpus.cmake, which the
# gate also runs on its own). test_conformance runs every case the manifest
# lists, reading JSON through the vendored cJSON (tests/vendor/cJSON),
# which is compiled here and nowhere else: the uxid library never links it
# and no install rule names it.
add_test(NAME check_corpus
  COMMAND ${CMAKE_COMMAND}
    -DUXID_CORPUS_DIR=${PROJECT_SOURCE_DIR}/conformance
    -P ${PROJECT_SOURCE_DIR}/scripts/check_corpus.cmake)

add_library(uxid_test_cjson STATIC vendor/cJSON/cJSON.c)
target_include_directories(uxid_test_cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cJSON)
# A static library linked into one test executable: cJSON's functions are
# neither exported from a DLL nor imported.
target_compile_definitions(uxid_test_cjson PUBLIC CJSON_HIDE_SYMBOLS)
target_compile_options(uxid_test_cjson PRIVATE ${UXID_WARNING_FLAGS})
# The one exemption, for this vendored target alone: cJSON calls sprintf,
# which the macOS SDK marks deprecated in the sanitizer builds, and the
# vendored file is never edited. Every other warning stays an error here, and
# nothing about the library's or the other tests' flags changes. (cJSON itself
# turns off MSVC's deprecation of the same functions.)
if(NOT MSVC AND CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  target_compile_options(uxid_test_cjson PRIVATE -Wno-deprecated-declarations)
endif()
set_target_properties(uxid_test_cjson PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

uxid_add_test(test_conformance)
target_link_libraries(test_conformance PRIVATE uxid_test_cjson)
target_compile_definitions(test_conformance PRIVATE
  UXID_CORPUS_DIR="${PROJECT_SOURCE_DIR}/conformance")

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

# The README's basic-usage block, built and run as it stands: the first
# ```c block after the "## Basic usage" heading is cut out of README.md at
# configure time (and again whenever README.md changes), so the example
# cannot drift from the library. The test exits non-zero when any of its
# checks fails.
#
# (sabotage: a wrong expected id in the README block -> test_readme_example
# exits 1)
set(_uxid_readme ${PROJECT_SOURCE_DIR}/README.md)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_uxid_readme})
file(READ ${_uxid_readme} _uxid_readme_text)
string(FIND "${_uxid_readme_text}" "\n## Basic usage\n" _uxid_usage_at)
if(_uxid_usage_at EQUAL -1)
  message(FATAL_ERROR "README.md has no \"## Basic usage\" heading")
endif()
string(SUBSTRING "${_uxid_readme_text}" ${_uxid_usage_at} -1 _uxid_usage_text)
string(FIND "${_uxid_usage_text}" "\n```c\n" _uxid_fence_at)
if(_uxid_fence_at EQUAL -1)
  message(FATAL_ERROR "README.md has no c code block after \"## Basic usage\"")
endif()
math(EXPR _uxid_code_at "${_uxid_fence_at} + 6")
string(SUBSTRING "${_uxid_usage_text}" ${_uxid_code_at} -1 _uxid_usage_text)
string(FIND "${_uxid_usage_text}" "\n```\n" _uxid_code_end)
if(_uxid_code_end EQUAL -1)
  message(FATAL_ERROR "README.md's c code block after \"## Basic usage\" is not closed")
endif()
string(SUBSTRING "${_uxid_usage_text}" 0 ${_uxid_code_end} _uxid_example)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/readme_example.c.new "${_uxid_example}\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/readme_example.c.new
  ${CMAKE_CURRENT_BINARY_DIR}/generated/readme_example.c COPYONLY)

add_executable(test_readme_example ${CMAKE_CURRENT_BINARY_DIR}/generated/readme_example.c)
target_link_libraries(test_readme_example PRIVATE uxid::uxid)
target_compile_options(test_readme_example PRIVATE ${UXID_WARNING_FLAGS})
set_target_properties(test_readme_example PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
add_test(NAME test_readme_example COMMAND test_readme_example)

# Every public declaration in include/uxid.h has a comment directly above it.
add_test(NAME check_header_docs
  COMMAND ${CMAKE_COMMAND}
    -DUXID_HEADER=${PROJECT_SOURCE_DIR}/include/uxid.h
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_header_docs.cmake)
