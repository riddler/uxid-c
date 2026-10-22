# Fails when a public declaration in include/uxid.h has no comment directly
# above it. Run as: cmake -DUXID_HEADER=<path to uxid.h> -P check_header_docs.cmake
#
# A declaration is a line that starts a macro (other than the include guard
# and UXID_API, whose comment sits above its #if), a typedef, a UXID_API
# function, an enum constant, or a struct field (a line indented four spaces
# that ends in ";" or ","). The private union inside uxid_monotonic is deeper
# than four spaces and is covered by the comment above the struct's field.
#
# (sabotage: a comment removed from above a declaration -> the check lists it
# and fails)

if(NOT UXID_HEADER)
  message(FATAL_ERROR "UXID_HEADER is not set")
endif()

file(READ "${UXID_HEADER}" _content)
# Keep ";" and "[" "]" out of the list the lines become.
string(REPLACE ";" "<SEMI>" _content "${_content}")
string(REPLACE "[" "<LB>" _content "${_content}")
string(REPLACE "]" "<RB>" _content "${_content}")
string(REPLACE "\n" ";" _lines "${_content}")

set(_in_comment FALSE)
set(_previous "")
set(_missing "")
set(_declarations 0)
set(_number 0)
foreach(_line IN LISTS _lines)
  math(EXPR _number "${_number} + 1")
  string(STRIP "${_line}" _text)
  set(_is_declaration FALSE)
  if(_in_comment)
    if(_text MATCHES "\\*/$")
      set(_in_comment FALSE)
    endif()
  elseif(_text MATCHES "^/\\*")
    if(NOT _text MATCHES "\\*/$")
      set(_in_comment TRUE)
    endif()
  elseif(_text STREQUAL "")
    # A blank line separates a comment from what follows.
  elseif(_text MATCHES "^#define +UXID_(H|API)( |$)")
    # The guard has no comment; UXID_API's sits above its #if.
  elseif(_text MATCHES "^#define +UXID_" OR _text MATCHES "^typedef " OR _text MATCHES "^UXID_API ")
    set(_is_declaration TRUE)
  elseif(_text MATCHES "^UXID_[A-Z0-9_]+ *(=|,)")
    set(_is_declaration TRUE)
  elseif(_line MATCHES "^    [^ ].*[;,]$")
    set(_is_declaration TRUE)
  endif()
  if(_is_declaration)
    math(EXPR _declarations "${_declarations} + 1")
    if(NOT _previous MATCHES "\\*/$")
      string(REPLACE "<SEMI>" ";" _shown "${_text}")
      string(APPEND _missing "  line ${_number}: ${_shown}\n")
    endif()
  endif()
  if(NOT _text STREQUAL "")
    set(_previous "${_text}")
  endif()
endforeach()

if(_declarations LESS 50)
  message(FATAL_ERROR "check_header_docs found only ${_declarations} declarations in ${UXID_HEADER}: the scan is broken")
endif()
if(_missing)
  message(FATAL_ERROR "declarations in ${UXID_HEADER} with no comment directly above:\n${_missing}")
endif()
message(STATUS "check_header_docs: ${_declarations} declarations, all commented")
