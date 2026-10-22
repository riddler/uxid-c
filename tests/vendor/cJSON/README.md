# cJSON, vendored for the corpus runner

This directory holds `cJSON.c`, `cJSON.h` and `LICENSE` from cJSON release
1.7.19, unchanged. cJSON is Dave Gamble's JSON parser, under the MIT license
in `LICENSE`.

- Source: https://github.com/DaveGamble/cJSON, tag `v1.7.19`, commit
  `c859b25da02955fef659d658b8f324b5cde87be3`.
- Release archive: https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz,
  SHA-256 `7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562`.
- The project publishes no checksum for the archive, so the three files were
  checked against the tag instead: each one's git blob id equals the blob the
  tag's commit tree holds (`cJSON.c` `6e4fb0dd369cd905923da515be87ab06db6c1ee0`,
  `cJSON.h` `cab5feb427725f8e5c82287f7fe59481b609b9b5`, `LICENSE`
  `78deb0406d713ab9730e3c2447be1abdbd70b9a2`).

It is test-only code. `tests/CMakeLists.txt` compiles it into the corpus
runner, `test_conformance`, and nothing else: the `uxid` library never
includes or links it, and no install rule names it, so it is not part of an
installed library. A release's source archive is a `git archive` of its tag,
so it carries these files as it carries every test.

The files are never edited. They are built with the same warning flags as
the tests, warnings as errors, with one exemption for this target alone:
`-Wno-deprecated-declarations` with gcc and clang, since `cJSON.c` calls
`sprintf`, which the macOS SDK marks deprecated in the sanitizer builds. They
are left out of the gate's clang-format check and clang-tidy run, which cover
this repository's own code. `.claude/firewall-vendor.txt` records the
SHA-256 of `cJSON.c` and `cJSON.h` as released. A new cJSON release replaces
all three files at once and updates this file and that one.
