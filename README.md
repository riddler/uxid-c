# uxid

[![MIT License][badge_license_url]](LICENSE)
[![CI][badge_ci_url]](https://github.com/riddler/uxid-c/actions/workflows/ci.yml)

User eXperience focused IDentifiers for C: prefixed, K-sortable, Stripe-style
ids a person can read, copy and route back to their resource. An id looks like
`usr_01hf7yat0004h1`: the prefix names the resource, and the body carries a
time and randomness in lower-case Crockford Base32. The ids follow the wire
format of the Elixir reference implementation,
[uxid-ex](https://github.com/riddler/uxid-ex), and the library is checked
against that implementation's conformance corpus, every case, with no skip
list.

It is C11, one header (`include/uxid.h`) and one source (`src/uxid.c`), with no
dependency beyond the platform C library. Every output goes into a buffer you
pass with its size, and every call that can fail returns a status code rather
than aborting or printing.

## Why UXID

An auto-increment key leaks how many rows a table holds and invites
enumeration; a random UUID fixes both but is long, unordered and anonymous, so
in a log line, a URL or a support thread nobody can tell what it points at, and
a double-click selects only part of it. A UXID names its resource in its
prefix, selects whole on a double-click, reads aloud without ambiguity, sorts
by creation time so it indexes well, and is generated in the application with
no coordination between nodes. Its size is tunable for low-cardinality
resources, a monotonic generator keeps a burst within one millisecond unique
and in order, and a deterministic mode maps the same input to the same id.

Because the format is shared, an id this library makes is read by the other
implementations of the format, and the other way round.

## Build and install

You need a C11 compiler and CMake 3.20 or later (the bundled
`CMakePresets.json` needs 3.21 or later).

```bash
cmake --preset default
cmake --build --preset default
cmake --install build/default --prefix /usr/local
```

The install puts `libuxid`, `uxid.h`, the CMake package (`uxidConfig.cmake`
and its version file) and `uxid.pc` under the prefix. Without presets,
`cmake -S . -B build && cmake --build build && cmake --install build` does the
same; `-DBUILD_SHARED_LIBS=ON` builds a shared library.

## Use it

### With CMake

```cmake
find_package(uxid 1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE uxid::uxid)
```

Add the install prefix to `CMAKE_PREFIX_PATH` when it is not a default one.
The target carries the include path and the platform libraries it needs.

To build it with your project instead, pin the release tag:

```cmake
include(FetchContent)
FetchContent_Declare(uxid
  GIT_REPOSITORY https://github.com/riddler/uxid-c.git
  GIT_TAG v1.0.0)
FetchContent_MakeAvailable(uxid)
target_link_libraries(app PRIVATE uxid::uxid)
```

A project that builds uxid this way gets the warnings but not warnings as
errors, and none of its tests.

### With pkg-config

```bash
cc -std=c11 app.c $(pkg-config --cflags --libs uxid) -o app
```

Set `PKG_CONFIG_PATH` to the install prefix's `lib/pkgconfig` when it is not
a default one. With a static library, `pkg-config --static` adds the platform
libraries the library needs.

## Basic usage

```c
#include <stdio.h>
#include <string.h>
#include <uxid.h>

int main(void)
{
    char id[3 + 1 + UXID_BODY_MAX_LENGTH + 1];
    uxid_options opts;

    /* A prefix names the resource; the body is a time and randomness. */
    uxid_options_init(&opts);
    opts.prefix = "usr";
    if (uxid_generate(id, sizeof id, &opts) != UXID_OK || strncmp(id, "usr_", 4) != 0) {
        return 1;
    }
    printf("%s\n", id);

    /* Shrink the random part for low-cardinality resources. */
    opts.prefix = "cus";
    opts.size = UXID_SIZE_S;
    if (uxid_generate(id, sizeof id, &opts) != UXID_OK || strlen(id) != 4 + 14) {
        return 1;
    }

    /* Deterministic: the same prefix and input give the same id, every time. */
    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.from = "alice@example.com";
    opts.from_len = strlen(opts.from);
    if (uxid_generate(id, sizeof id, &opts) != UXID_OK || strcmp(id, "usr_zcvt7epac0t1ebcsjfyf7cwz25") != 0) {
        return 1;
    }
    printf("%s\n", id);
    if (!uxid_is_deterministic(id, strlen(id), "_")) {
        return 1;
    }

    /* Read an id back. A malformed id is a status, never a crash. */
    {
        const char *text = "cus_01hf7yat0004h1";
        uxid_decoded decoded;

        if (uxid_decode(text, strlen(text), NULL, &decoded) != UXID_DECODE_OK || decoded.prefix_len != 3 ||
            decoded.size != UXID_SIZE_S || decoded.time != 1700000000000) {
            return 1;
        }
        if (!uxid_is_valid(text, strlen(text), "_", "cus")) {
            return 1;
        }
        if (uxid_decode("cus_", 4, NULL, &decoded) != UXID_DECODE_EMPTY) {
            return 1;
        }
    }

    /* Monotonic: ids from one generator increase strictly, also within one
     * millisecond. You hold the generator; the library keeps no global state. */
    {
        uxid_monotonic gen;
        char first[sizeof id];
        char second[sizeof id];

        uxid_options_init(&opts);
        opts.prefix = "usr";
        opts.size = UXID_SIZE_M;
        if (uxid_monotonic_init(&gen, NULL) != UXID_OK) {
            return 1;
        }
        if (uxid_monotonic_next(&gen, first, sizeof first, &opts) != UXID_OK ||
            uxid_monotonic_next(&gen, second, sizeof second, &opts) != UXID_OK || strcmp(first, second) >= 0) {
            uxid_monotonic_destroy(&gen);
            return 1;
        }
        uxid_monotonic_destroy(&gen);
    }

    return 0;
}
```

This block is built and run as a test (`test_readme_example`), straight from
this file, so it cannot drift from the library.

A refusal is a `uxid_status`; `uxid_status_name()` gives its name, the
conformance corpus's spelling for the six errors the wire format defines
(`compact_time_out_of_range`, `rand_width_mismatch`, `rand_size_too_large`,
`from_with_monotonic`, `rand_with_from`, `rand_with_monotonic`) and this
library's own for `UXID_ERR_BUFFER_TOO_SMALL`, `UXID_ERR_RANDOM_SOURCE`,
`UXID_ERR_INVALID_ARGUMENT` and `UXID_ERR_OUT_OF_RESOURCES`. On a refusal the
output buffer holds the empty string. Size a buffer for an id with
`UXID_BODY_MAX_LENGTH`, as the block above does.

`uxid_generate()` reads the system clock when it needs a time and the
platform's cryptographic random source when it needs random bytes.
`uxid_options.time` and `uxid_options.byte_source` replace them, and
`uxid_decode_options.clock` replaces the clock `uxid_decode()` reads for a
compact time. `uxid_options.rand` takes fixed random bytes for test vectors:
an id built from them is exactly as guessable as those bytes.

`uxid_is_valid()` and `uxid_is_deterministic()` take the delimiter the id was
generated with, and read no clock. `uxid_is_valid()` answers whether the id
decodes (a body of a length the format defines, in Crockford Base32
characters) and, given a required prefix, whether it carries exactly that
prefix. `uxid_is_deterministic()` answers whether the id decodes and its body
starts with the marker `z` or `Z`. Both are stricter than the Elixir
reference, whose `valid?` accepts any alphabet body of at least 8 characters
and whose `deterministic?` checks only the first body character: here a
malformed string is neither valid nor deterministic. The conformance corpus
does not fix these two answers.

Every public declaration is documented in `include/uxid.h`, which is the API
reference.

## Compatibility

- **C11.** The library is plain C11, and the
  header is usable from C++.
- **Platforms CI proves.** Linux with gcc and with clang, macOS with Apple
  clang, and Windows with MSVC, each building and running the tests; Linux and
  macOS also run them under AddressSanitizer and UndefinedBehaviorSanitizer,
  and the full gate runs on Linux with clang. A job builds with CMake 3.20, the
  minimum. Other POSIX systems with a C11 compiler should work but are not
  proved.
- **The random source.** The default byte source is the platform's
  cryptographically secure generator, every return value checked: `getrandom`
  on Linux, `arc4random_buf` on macOS and the BSDs and `BCryptGenRandom` on
  Windows. `rand()` and other seeded generators are never used. A failure is
  `UXID_ERR_RANDOM_SOURCE`.
- **No dependencies.** The library needs the platform C library; SHA-256, for
  deterministic ids, is written in `src/uxid.c`. It links `bcrypt` on Windows
  and the platform thread library (`-pthread`) on POSIX, both carried by the
  CMake target and by `uxid.pc`.
- **Thread safety.** A `uxid_monotonic` is safe to share between threads: a
  platform mutex inside it (a pthread mutex on POSIX, an SRWLOCK on Windows)
  orders its calls, so the ids it makes for one prefix and random width
  increase strictly in the order the calls take the lock, whichever thread
  asks. The lock is not recursive, and a draw source or byte source runs with
  it held: such a source must not call back into the generator that called it.
  `uxid_monotonic_init()` and `uxid_monotonic_destroy()` are not ordered by the
  lock. Two generators never affect each other, and ids from two generators
  carry no order between them within a millisecond. `uxid_generate()`,
  `uxid_decode()` and the validity checks keep no state and can be called from
  any thread; a byte source or clock you inject is called on the calling
  thread.
- **The monotonic generator's choices.** At size xs it always uses compact
  time (`compact_time` is not read there), because a standard xs id has no
  random field to step. A monotonic call the library refuses leaves the
  generator as it was, where the reference implementation advances its state
  first; the header's description of `uxid_monotonic_next()` says what the
  next call then does. A compact time keeps its order only within one 2^40 ms
  window, as every compact id does.

## Documentation

- Look up
  - [`include/uxid.h`](include/uxid.h): the API reference. Every public
    declaration carries a comment saying what it does, what it accepts and
    what it answers.
  - [The wire format](docs/wire-format.md): where the normative layout of an
    id lives, and which version of it this library follows.
  - [The changelog](CHANGELOG.md): what changed in each version.
  - [The conformance corpus](conformance): the cases every implementation of
    the wire format passes, vendored here from uxid-ex.
- Understand
  - [The reference spec, at v2.10.0][guide_wire_format_url]: the normative
    layout of an id - alphabet, bit packing, sizes, prefix grammar,
    deterministic and monotonic bodies, and decoding.
  - [Why a UXID has a prefix, a time and randomness][guide_why_url]: what each
    part of an id is for, and what you trade when you tune it.
  - [Why monotonic mode exists and what it costs][guide_monotonic_url]:
    same-millisecond uniqueness and ordering, and the security tradeoff.
  - [_Designing APIs for humans: object IDs_][stripe_ids_url]: the Stripe id
    design many of UXID's choices follow.

## Development

After checking out the repository, run `mise install` for the tools
`mise.toml` pins. Then `mise run gate:loop` runs the inner loop (configure,
build and the CTest tests), and `mise run gate` runs the full quality gate:
the compiler's version, the build with warnings as errors and the tests, the
sanitizer builds and their tests, the format check, clang-tidy, 90 percent
line coverage and the conformance corpus check. The tests include the
conformance corpus, every case with no skip list.

A release is published by CI, never from a checkout: pushing a tag `vX.Y.Z`
that names the version in `include/uxid.h` runs
`.github/workflows/release.yml`, which creates the GitHub release.

## Contributing

Bug reports and pull requests are welcome on GitHub at
https://github.com/riddler/uxid-c.

## License

The library is available as open source under the terms of the
[MIT License](https://opensource.org/licenses/MIT).

<!-- LINKS -->
[guide_wire_format_url]: https://github.com/riddler/uxid-ex/blob/v2.10.0/guides/wire-format.md
[guide_why_url]: https://github.com/riddler/uxid-ex/blob/v2.10.0/guides/why-prefix-time-randomness.md
[guide_monotonic_url]: https://github.com/riddler/uxid-ex/blob/v2.10.0/guides/monotonic.md
[stripe_ids_url]: https://dev.to/stripe/designing-apis-for-humans-object-ids-3o5a

<!-- BADGES -->
[badge_license_url]: https://img.shields.io/badge/license-MIT-brightgreen.svg
[badge_ci_url]: https://github.com/riddler/uxid-c/actions/workflows/ci.yml/badge.svg
