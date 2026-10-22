### Added

- `uxid_options.from` and `uxid_options.from_len` ask `uxid_generate` for a deterministic id: the same prefix, input string and size always give the same id, a `z` (or `Z`) marker and the leading bits of a SHA-256 digest, at the size's standard body length. The SHA-256 is the library's own, so it still needs nothing beyond the platform C library.
- With `from` set, `uxid_generate` reads neither the time options, compact time nor the random width, and does not read the system clock or call the byte source.
- `uxid_generate` refuses fixed random bytes together with `from` as `UXID_ERR_RAND_WITH_FROM`.

### Changed

- `uxid_options.reserved` has 6 slots, down from 8: `from` and `from_len` take the first two, so the struct's size and the offset of every other field are unchanged.
