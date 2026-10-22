# Changelog

## 1.0.0 - 2026-10-21

The first release of `uxid` for C: prefixed, K-sortable identifiers a person
can read, wire-compatible with the Elixir `uxid` package. It covers time-based
generation, deterministic ids, decoding, the validity checks and a monotonic
generator, every output into a buffer the caller passes and every refusal a
status code. It is C11, one header (`uxid.h`) and one library, with no
dependency beyond the platform C library, `bcrypt` on Windows and the platform
thread library on POSIX. It builds with CMake 3.20 or later (the bundled
`CMakePresets.json` needs 3.21) and is found through `find_package(uxid)`, the
target `uxid::uxid`, or through pkg-config as `uxid`. CI builds it and runs its
tests with gcc and clang on Linux (Ubuntu 24.04), Apple clang on macOS 15 and
MSVC on Windows (Windows Server 2025, a static library), with AddressSanitizer
and UndefinedBehaviorSanitizer on Linux and macOS. It passes every case of the
uxid conformance corpus at uxid-ex's tag `v2.10.0`, vendored byte for byte and
run with no skip list.

Compatibility: the shared library's SOVERSION is the major version, 1, and the
CMake package's version file accepts a request for any 1.x version
(`SameMajorVersion`), so a later 1.x release keeps the API and the ABI of
1.0.0. Enum values are only ever added after the last one; `uxid_options`,
`uxid_decode_options`, `uxid_decoded` and `uxid_draw_source` keep their size
and the offset of every field from one version to the next, a later version
placing a new field in reserved slots; and `uxid_monotonic`, whose contents are
private, keeps its size.

### Added

- `uxid_options.from` and `uxid_options.from_len` ask `uxid_generate` for a deterministic id: the same prefix, input string and size always give the same id, a `z` (or `Z`) marker and the leading bits of a SHA-256 digest, at the size's standard body length. The SHA-256 is the library's own, so it still needs nothing beyond the platform C library.
- With `from` set, `uxid_generate` reads neither the time options, compact time nor the random width, and does not read the system clock or call the byte source.
- `uxid_generate` refuses fixed random bytes together with `from` as `UXID_ERR_RAND_WITH_FROM`.
- `uxid_decode` reads an id of a given length, which need not be NUL-terminated and is never read past that length, into a caller-owned `uxid_decoded`: the prefix (a pointer and a length into the id, or NULL for none), the size, whether the time is compact, whether the body is deterministic, and the time in milliseconds.
- `uxid_decode` returns a `uxid_decode_status`: `UXID_DECODE_OK`, or the wire format's refusals in the order a body is checked for them (`UXID_DECODE_EMPTY`, `UXID_DECODE_INVALID_LENGTH`, `UXID_DECODE_INVALID_CHARACTER`), or this library's own `UXID_DECODE_INVALID_ARGUMENT`. Malformed input of any bytes is answered with a status, never an abort.
- `uxid_decode_options` and `uxid_decode_options_init` carry the delimiter (`_` by default) and the decoder's clock, a function pointer with a context (the system clock by default), which supplies a compact time's high bits. A compact time more than one day ahead of the clock steps back one 2^40 ms window, with no floor at zero, so a clock in the first window can give a negative time.
- `uxid_is_valid` answers whether an id is well formed under a delimiter and, optionally, carries exactly a required prefix; `uxid_is_deterministic` answers whether it is a well-formed deterministic id under a delimiter. Both require a body `uxid_decode` would decode, which is stricter than the Elixir reference.
- `uxid_monotonic`, a monotonic generator the caller owns: `uxid_monotonic_init`, `uxid_monotonic_next` and `uxid_monotonic_destroy`. For one prefix and random width, each id it makes sorts after the last, also within one millisecond and when the clock moves back, following the wire format's step and overflow rules; it keeps that state per prefix and random width, one heap entry for each distinct pair, freed by `uxid_monotonic_destroy`.
- One generator may be shared between threads: a platform mutex inside the struct (a pthread mutex on POSIX, an SRWLOCK on Windows) orders the calls, so the ids it makes for one prefix and random width increase strictly whichever thread asks.
- `uxid_monotonic_next` takes the same `uxid_options` as `uxid_generate`, always uses compact time at size xs, and refuses `from` as `UXID_ERR_FROM_WITH_MONOTONIC` and fixed `rand` bytes as `UXID_ERR_RAND_WITH_MONOTONIC`. A refused call leaves the generator's state as it was.
- `uxid_draw_source` replaces a generator's seeds and steps with values of the caller's, to replay known draws in tests; by default the draws come from each call's byte source, the step masked to its range with no modulo bias.
- `UXID_ERR_OUT_OF_RESOURCES` (`out_of_resources`) answers a generator whose lock or state entry the system cannot provide.
- On POSIX the library links the platform thread library (`-pthread`), in the CMake target and in `uxid.pc`'s `Libs.private`.
- `uxid_generate` writes a time-based id into a caller-owned buffer and returns a `uxid_status`: the prefix and delimiter, a standard (48-bit) or compact (40-bit) time and the random field, at sizes xs, s, m, l and xl (xl by default), in lower or upper case.
- `uxid_options` and `uxid_options_init` carry the options: prefix (empty or NULL is no prefix), delimiter (`_` by default), size, an explicit random width, compact time, letter case, a fixed time, fixed random bytes for reproducible vectors, and a byte source with a context pointer.
- The default byte source is the platform's secure generator: `getrandom` on Linux, `arc4random_buf` on macOS and the BSDs, `BCryptGenRandom` on Windows.
- `uxid_status` names the wire format's generation errors and this library's own four (`UXID_ERR_BUFFER_TOO_SMALL`, `UXID_ERR_RANDOM_SOURCE`, `UXID_ERR_INVALID_ARGUMENT`, `UXID_ERR_OUT_OF_RESOURCES`); `uxid_status_name` gives each its name, the conformance corpus's spelling for the six generation errors.
- `UXID_BODY_MAX_LENGTH` (26) sizes an output buffer, and `UXID_API` marks the exported functions.
- On Windows the library links `bcrypt`, which the pkg-config file lists in `Libs.private`.
- `-Werror` and `/WX` apply only when uxid is the top-level CMake project, through the `UXID_WARNINGS_AS_ERRORS` option; a project that adds uxid with `add_subdirectory` or `FetchContent` still sees the warnings.
- `UXID_VERSION_MAJOR`, `UXID_VERSION_MINOR`, `UXID_VERSION_PATCH` and `UXID_VERSION_STRING` give the library's version; the CMake package version and the pkg-config version are read from them.
