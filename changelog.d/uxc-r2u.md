### Added

- `uxid_generate` writes a time-based id into a caller-owned buffer and returns a `uxid_status`: the prefix and delimiter, a standard (48-bit) or compact (40-bit) time and the random field, at sizes xs, s, m, l and xl (xl by default), in lower or upper case.
- `uxid_options` and `uxid_options_init` carry the options: prefix (empty or NULL is no prefix), delimiter (`_` by default), size, an explicit random width, compact time, letter case, a fixed time, fixed random bytes for reproducible vectors, and a byte source with a context pointer.
- The default byte source is the platform's secure generator: `getrandom` on Linux, `arc4random_buf` on macOS and the BSDs, `BCryptGenRandom` on Windows.
- `uxid_status` names the wire format's generation errors and this library's own three (`UXID_ERR_BUFFER_TOO_SMALL`, `UXID_ERR_RANDOM_SOURCE`, `UXID_ERR_INVALID_ARGUMENT`); `uxid_status_name` gives each its name, the conformance corpus's spelling for the six generation errors.
- `UXID_BODY_MAX_LENGTH` (26) sizes an output buffer, and `UXID_API` marks the exported functions.

### Changed

- On Windows the library links `bcrypt`, which the pkg-config file lists in `Libs.private`.
- `-Werror` and `/WX` apply only when uxid is the top-level CMake project, through the new `UXID_WARNINGS_AS_ERRORS` option; a project that adds uxid with `add_subdirectory` or `FetchContent` still sees the warnings.
