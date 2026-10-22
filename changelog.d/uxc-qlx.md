### Added

- `uxid_monotonic`, a monotonic generator the caller owns: `uxid_monotonic_init`, `uxid_monotonic_next` and `uxid_monotonic_destroy`. For one prefix and random width, each id it makes sorts after the last, also within one millisecond and when the clock moves back, following the wire format's step and overflow rules; it keeps that state per prefix and random width, one heap entry for each distinct pair, freed by `uxid_monotonic_destroy`.
- One generator may be shared between threads: a platform mutex inside the struct (a pthread mutex on POSIX, an SRWLOCK on Windows) orders the calls, so the ids it makes for one prefix and random width increase strictly whichever thread asks.
- `uxid_monotonic_next` takes the same `uxid_options` as `uxid_generate`, always uses compact time at size xs, and refuses `from` as `UXID_ERR_FROM_WITH_MONOTONIC` and fixed `rand` bytes as `UXID_ERR_RAND_WITH_MONOTONIC`. A refused call leaves the generator's state as it was.
- `uxid_draw_source` replaces a generator's seeds and steps with values of the caller's, to replay known draws in tests; by default the draws come from each call's byte source, the step masked to its range with no modulo bias.
- `UXID_ERR_OUT_OF_RESOURCES` (`out_of_resources`) answers a generator whose lock or state entry the system cannot provide.
- On POSIX the library links the platform thread library (`-pthread`), in the CMake target and in `uxid.pc`'s `Libs.private`.
