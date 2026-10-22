# Checks the vendored conformance corpus against its own manifest and its
# recorded source, by the corpus README's rule ("Running the cases", step 1
# and 2), with CMake's own SHA-256:
#
#   - conformance/SOURCE.json names the reference's repository, the tag and
#     commit this script expects, and the manifest's corpus_hash;
#   - manifest.json is corpus version 1 and lists case files by kind, file
#     and case_count, with nothing else;
#   - the SHA-256 of the case files' bytes, concatenated in the manifest's
#     order, is the manifest's corpus_hash;
#   - each case file is case_count lines, each ended by a newline.
#
# A mismatch fails the check. It is never repaired by rewriting a hash or a
# count: the vendored files are the tag's bytes, and a new corpus is vendored
# whole, with this script's expected tag and commit, from a new tag.
#
#   cmake -DUXID_CORPUS_DIR=conformance -P scripts/check_corpus.cmake
cmake_minimum_required(VERSION 3.20)

set(_expected_repo "riddler/uxid-ex")
set(_expected_tag "v2.10.0")
set(_expected_sha "57235777cb29c70f751552ce7aa20ab5fe71fb74")

if(NOT DEFINED UXID_CORPUS_DIR)
  message(FATAL_ERROR "check_corpus.cmake: pass -DUXID_CORPUS_DIR=<the conformance directory>")
endif()
set(_dir "${UXID_CORPUS_DIR}")
set(_errors 0)

macro(_corpus_fail text)
  message(SEND_ERROR "corpus: ${text}")
  math(EXPR _errors "${_errors} + 1")
endmacro()

# Reads key from the JSON object json into out, failing the check when the
# key is absent.
function(_corpus_json out json what)
  string(JSON _value ERROR_VARIABLE _error GET "${json}" ${ARGN})
  if(_error)
    message(FATAL_ERROR "corpus: ${what}: ${_error}")
  endif()
  set(${out} "${_value}" PARENT_SCOPE)
endfunction()

# Fails the check unless the JSON object json has exactly the keys given.
function(_corpus_keys json what)
  string(JSON _count LENGTH "${json}")
  list(LENGTH ARGN _want)
  if(NOT _count EQUAL _want)
    message(FATAL_ERROR "corpus: ${what} has ${_count} keys, expected exactly: ${ARGN}")
  endif()
  foreach(_key IN LISTS ARGN)
    string(JSON _type ERROR_VARIABLE _error TYPE "${json}" ${_key})
    if(_error)
      message(FATAL_ERROR "corpus: ${what} has no key ${_key}")
    endif()
  endforeach()
endfunction()

file(READ "${_dir}/SOURCE.json" _source)
_corpus_keys("${_source}" "SOURCE.json" repo tag sha corpus_hash)
_corpus_json(_repo "${_source}" "SOURCE.json" repo)
_corpus_json(_tag "${_source}" "SOURCE.json" tag)
_corpus_json(_sha "${_source}" "SOURCE.json" sha)
_corpus_json(_source_hash "${_source}" "SOURCE.json" corpus_hash)
if(NOT _repo STREQUAL _expected_repo)
  _corpus_fail("SOURCE.json repo is \"${_repo}\", expected \"${_expected_repo}\"")
endif()
if(NOT _tag STREQUAL _expected_tag)
  _corpus_fail("SOURCE.json tag is \"${_tag}\", expected \"${_expected_tag}\"")
endif()
if(NOT _sha STREQUAL _expected_sha)
  _corpus_fail("SOURCE.json sha is \"${_sha}\", expected \"${_expected_sha}\"")
endif()

file(READ "${_dir}/manifest.json" _manifest)
_corpus_keys("${_manifest}" "manifest.json" corpus_version files corpus_hash)
_corpus_json(_version "${_manifest}" "manifest.json" corpus_version)
_corpus_json(_manifest_hash "${_manifest}" "manifest.json" corpus_hash)
if(NOT _version STREQUAL "1")
  _corpus_fail("manifest.json corpus_version is ${_version}; this runner knows version 1")
endif()
if(NOT _manifest_hash STREQUAL _source_hash)
  _corpus_fail("manifest.json corpus_hash ${_manifest_hash} differs from SOURCE.json's ${_source_hash}")
endif()

string(JSON _file_count LENGTH "${_manifest}" files)
if(_file_count EQUAL 0)
  _corpus_fail("manifest.json lists no case files")
endif()
set(_all "")
set(_cases 0)
math(EXPR _last "${_file_count} - 1")
foreach(_i RANGE ${_last})
  string(JSON _entry GET "${_manifest}" files ${_i})
  _corpus_keys("${_entry}" "manifest.json files[${_i}]" kind file case_count)
  _corpus_json(_file "${_entry}" "manifest.json files[${_i}]" file)
  _corpus_json(_case_count "${_entry}" "manifest.json files[${_i}]" case_count)
  if(NOT _file MATCHES "^[a-z]+\\.json$")
    message(FATAL_ERROR "corpus: manifest.json files[${_i}] names \"${_file}\", not a case file name")
  endif()
  file(READ "${_dir}/${_file}" _content)
  string(APPEND _all "${_content}")
  # One case per line, each line ended by a newline: the newlines are the count.
  string(LENGTH "${_content}" _with)
  string(REPLACE "\n" "" _without "${_content}")
  string(LENGTH "${_without}" _without_length)
  math(EXPR _lines "${_with} - ${_without_length}")
  if(NOT _lines EQUAL _case_count)
    _corpus_fail("${_file} has ${_lines} lines, manifest.json says case_count ${_case_count}")
  endif()
  if(NOT _with EQUAL 0)
    math(EXPR _end "${_with} - 1")
    string(SUBSTRING "${_content}" ${_end} 1 _tail)
    if(NOT _tail STREQUAL "\n")
      _corpus_fail("${_file} does not end with a newline")
    endif()
  endif()
  if(_content MATCHES "\n\n" OR _content MATCHES "^\n")
    _corpus_fail("${_file} has an empty line")
  endif()
  math(EXPR _cases "${_cases} + ${_case_count}")
endforeach()

string(SHA256 _hash "${_all}")
if(NOT "sha256:${_hash}" STREQUAL _manifest_hash)
  _corpus_fail("the case files hash to sha256:${_hash}, manifest.json says ${_manifest_hash}")
endif()

if(NOT _errors EQUAL 0)
  message(FATAL_ERROR "corpus: ${_errors} check(s) failed in ${_dir}")
endif()
message(STATUS "corpus: ${_tag} (${_sha}), ${_file_count} case files, ${_cases} cases, sha256:${_hash}")
