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
