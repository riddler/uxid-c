/*
 * uxid: prefixed, K-sortable identifiers a person can read, wire-compatible
 * with the Elixir reference uxid package.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UXID_H
#define UXID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The major version of this library, as an integer constant. */
#define UXID_VERSION_MAJOR 0

/* The minor version of this library, as an integer constant. */
#define UXID_VERSION_MINOR 1

/* The patch version of this library, as an integer constant. */
#define UXID_VERSION_PATCH 0

/*
 * The version of this library as a string literal, "MAJOR.MINOR.PATCH",
 * always equal to the three macros above. The CMake package version and the
 * pkg-config version are read from these macros.
 */
#define UXID_VERSION_STRING "0.1.0"

/*
 * Marks a function this library exports. A shared build on Windows exports it
 * from the DLL (the build defines UXID_BUILDING_SHARED); a consumer needs no
 * definition of its own, since a function is reached through the import
 * library either way. With gcc and clang the library is built with hidden
 * visibility and this macro gives each public function default visibility.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(UXID_BUILDING_SHARED)
#define UXID_API __declspec(dllexport)
#else
#define UXID_API
#endif
#elif defined(__GNUC__)
#define UXID_API __attribute__((visibility("default")))
#else
#define UXID_API
#endif

/*
 * The longest body an id can have, in bytes: 26, a standard xl body. Every
 * body character is one ASCII byte. A buffer for an id needs the prefix's
 * length, plus the delimiter's length when the prefix is not empty, plus this,
 * plus one byte for the terminating NUL.
 */
#define UXID_BODY_MAX_LENGTH 26

/*
 * What a call answers. UXID_OK is success; every other value is a refusal.
 * The first six refusals are the wire format's generation errors, and
 * uxid_status_name() gives each one the name the conformance corpus uses.
 * The last three are this library's own and have no corpus name. Values are
 * only ever added, after the last one, so a value never changes meaning.
 */
typedef enum uxid_status {
    /* Success: the output holds the answer. */
    UXID_OK = 0,
    /*
     * compact_time_out_of_range: a compact time whose first character would
     * be the deterministic marker (the low 40 bits of the time at or above
     * 31 x 2^35, from mid-2038 until the 40-bit window rolls over in 2039).
     */
    UXID_ERR_COMPACT_TIME_OUT_OF_RANGE = 1,
    /*
     * rand_width_mismatch: fixed random bytes whose count is not the
     * request's random field width.
     */
    UXID_ERR_RAND_WIDTH_MISMATCH = 2,
    /* rand_size_too_large: an explicit random field width above 10 bytes. */
    UXID_ERR_RAND_SIZE_TOO_LARGE = 3,
    /*
     * from_with_monotonic: a deterministic id asked for with monotonic
     * generation. No call in this version answers it.
     */
    UXID_ERR_FROM_WITH_MONOTONIC = 4,
    /* rand_with_from: fixed random bytes asked for with a deterministic id. */
    UXID_ERR_RAND_WITH_FROM = 5,
    /*
     * rand_with_monotonic: fixed random bytes asked for with monotonic
     * generation. No call in this version answers it.
     */
    UXID_ERR_RAND_WITH_MONOTONIC = 6,
    /* buffer_too_small: the output buffer cannot hold the answer and its NUL. */
    UXID_ERR_BUFFER_TOO_SMALL = 7,
    /* random_source: the byte source, the default or an injected one, failed. */
    UXID_ERR_RANDOM_SOURCE = 8,
    /*
     * invalid_argument: an argument or an option is outside what the call
     * takes (a null output buffer, a null or empty delimiter, a size or case
     * that is not one of its enum's values, a time above 2^48 - 1 for a
     * time-based id, a null fixed random pointer or a null from pointer with
     * a non-zero length, a non-zero reserved option), or, for a time-based
     * id, the system clock cannot be read or reads outside 0 to 2^48 - 1
     * milliseconds.
     */
    UXID_ERR_INVALID_ARGUMENT = 9,
} uxid_status;

/*
 * The size of an id, which fixes the random field's width and so the body
 * length. Each short name has a long alias that means the same value.
 */
typedef enum uxid_size {
    /* xs: a 10-byte body, no random field in standard time, 1 byte in compact. */
    UXID_SIZE_XS = 0,
    /* s: 14 bytes in standard time (2 random bytes), 13 in compact (3). */
    UXID_SIZE_S = 1,
    /* m: 18 bytes in standard time (5 random bytes), 18 in compact (6). */
    UXID_SIZE_M = 2,
    /* l: 22 bytes in standard time (7 random bytes), 21 in compact (8). */
    UXID_SIZE_L = 3,
    /* xl, the default: 26 bytes in standard time, 24 in compact (10 random bytes). */
    UXID_SIZE_XL = 4,
    /* The long spelling of xs. */
    UXID_SIZE_XSMALL = UXID_SIZE_XS,
    /* The long spelling of s. */
    UXID_SIZE_SMALL = UXID_SIZE_S,
    /* The long spelling of m. */
    UXID_SIZE_MEDIUM = UXID_SIZE_M,
    /* The long spelling of l. */
    UXID_SIZE_LARGE = UXID_SIZE_L,
    /* The long spelling of xl. */
    UXID_SIZE_XLARGE = UXID_SIZE_XL,
} uxid_size;

/* The letter case of an id's body. Case never applies to the prefix. */
typedef enum uxid_case {
    /* Lower case, the default. */
    UXID_CASE_LOWER = 0,
    /* Upper case. An upper-case body and its lower-case spelling are the same id. */
    UXID_CASE_UPPER = 1,
} uxid_case;

/*
 * A source of random bytes: fills out[0] to out[len - 1] and returns 0, or
 * returns any other value when it cannot, which the call answers with
 * UXID_ERR_RANDOM_SOURCE. ctx is the context pointer given beside it,
 * passed through unchanged. The bytes are an id's only unpredictable part,
 * so a source given here must be cryptographically secure for any id that
 * must not be guessed.
 */
typedef int (*uxid_byte_source)(void *ctx, uint8_t *out, size_t len);

/*
 * The options uxid_generate() builds an id from. Initialize every instance
 * with uxid_options_init(), then set the fields to change; a field left as
 * uxid_options_init() set it takes the default it names. The struct's size
 * and the offset of every field are fixed from one version to the next: a
 * later version places a new field in reserved slots.
 */
typedef struct uxid_options {
    /*
     * The resource prefix, a NUL-terminated string written before the
     * delimiter exactly as given; it may contain the delimiter. NULL, the
     * default, and the empty string are no prefix: the id is the bare body,
     * with no delimiter.
     */
    const char *prefix;
    /*
     * The delimiter between the prefix and the body, a non-empty
     * NUL-terminated string; the default is "_". It is not recorded in the
     * id, so a reader must be told the same one. NULL or the empty string is
     * refused with UXID_ERR_INVALID_ARGUMENT.
     */
    const char *delimiter;
    /* The size; the default is UXID_SIZE_XL. */
    uxid_size size;
    /*
     * True to use rand_size in place of the size's random field width; the
     * default is false.
     */
    bool has_rand_size;
    /*
     * The random field width in bytes when has_rand_size is true, from 0 to
     * 10; above 10 is refused with UXID_ERR_RAND_SIZE_TOO_LARGE. A body of a
     * width the size table does not list has the same layout, but its length
     * is one a decoder refuses.
     */
    size_t rand_size;
    /*
     * True for compact time: the low 40 bits of the time in 8 characters,
     * in place of the 48-bit standard time in 10, the freed 8 bits going to
     * the random field at every size but xl. The default is false. A compact
     * time whose first character would be the deterministic marker is
     * refused with UXID_ERR_COMPACT_TIME_OUT_OF_RANGE.
     */
    bool compact_time;
    /* The letter case of the body; the default is UXID_CASE_LOWER. */
    uxid_case letter_case;
    /*
     * True to use time in place of the system clock; the default is false,
     * which reads the system clock (UTC) at the call.
     */
    bool has_time;
    /*
     * The time the id carries when has_time is true, in milliseconds since
     * the Unix epoch, from 0 to 2^48 - 1; above that is refused with
     * UXID_ERR_INVALID_ARGUMENT.
     */
    uint64_t time;
    /*
     * Fixed bytes for the random field, so that a fixed time and fixed bytes
     * always build the same id. They exist for reproducible vectors, such as
     * the conformance corpus and tests, and are never a source of
     * randomness: the id is exactly as guessable as the bytes passed. NULL,
     * the default, draws the bytes from the byte source. When not NULL,
     * rand_len must equal the random field width (the size's, or
     * rand_size), or the request is refused with
     * UXID_ERR_RAND_WIDTH_MISMATCH; xs in standard time has no random field,
     * so only a length of 0 fits it.
     */
    const uint8_t *rand;
    /* The number of bytes at rand. Non-zero with a NULL rand is refused. */
    size_t rand_len;
    /*
     * The source of the random field's bytes. NULL, the default, is the
     * platform's cryptographically secure generator: getrandom on Linux,
     * arc4random_buf on macOS and the BSDs, BCryptGenRandom on Windows. It
     * is called at most once per id, and not at all when the random field is
     * empty or rand is set.
     */
    uxid_byte_source byte_source;
    /* The context passed to byte_source; the default is NULL. */
    void *byte_source_ctx;
    /*
     * The input of a deterministic id: from_len bytes at from, the caller's
     * string as UTF-8, hashed as given (a zero byte among them is part of the
     * input). NULL, the default, builds a time-based id. When not NULL, the
     * body is the marker (z, or Z in upper case) followed by the first
     * 5 x count bits of the SHA-256 digest of the prefix's bytes (none for no
     * prefix), a zero byte, the from bytes, a zero byte and one byte holding
     * count, written as count characters; count is 9, 13, 17, 21 or 25 for
     * xs, s, m, l and xl, so the body has the size's standard length. The
     * same prefix, from and size always give the same id, and anyone who can
     * guess from can compute it: it is a name, not a secret. With from set,
     * has_time, time, compact_time, has_rand_size and rand_size are not read,
     * the system clock is not read and the byte source is not called; fixed
     * rand bytes are refused with UXID_ERR_RAND_WITH_FROM. A non-NULL from
     * with from_len 0 is the empty string.
     */
    const char *from;
    /* The number of bytes at from. Non-zero with a NULL from is refused. */
    size_t from_len;
    /*
     * Reserved for options a later version adds, each of which reads zero
     * as its default. uxid_options_init() zeroes them; a non-zero slot is
     * refused with UXID_ERR_INVALID_ARGUMENT, so a caller built against a
     * later version that sets such an option is refused by an earlier
     * library rather than given an id without it.
     */
    void *reserved[6];
} uxid_options;

/*
 * Fills *opts with the defaults: no prefix, the delimiter "_", size xl, the
 * size's random field width, standard time, lower case, the system clock,
 * no fixed random bytes, the platform's secure generator, and no from (a
 * time-based id). Every reserved slot is zeroed. A NULL opts is ignored: the
 * call writes nothing.
 */
UXID_API void uxid_options_init(uxid_options *opts);

/*
 * Generates an id into out, a buffer of out_size bytes: the prefix and the
 * delimiter (none for no prefix), then the body, then a terminating NUL. The
 * body is a time field and a random field, or, when opts->from is set, the
 * deterministic body that field describes. opts NULL takes every default.
 * Lengths are counted in bytes, and nothing is written at or past
 * out[out_size].
 *
 * Returns UXID_OK when out holds the id. Otherwise it returns the first
 * refusal that applies, checked in this order: UXID_ERR_INVALID_ARGUMENT,
 * UXID_ERR_RAND_WITH_FROM, UXID_ERR_RAND_SIZE_TOO_LARGE,
 * UXID_ERR_RAND_WIDTH_MISMATCH, UXID_ERR_COMPACT_TIME_OUT_OF_RANGE,
 * UXID_ERR_BUFFER_TOO_SMALL (out_size is shorter than the id and its NUL;
 * UXID_BODY_MAX_LENGTH says how to size a buffer), then
 * UXID_ERR_RANDOM_SOURCE; no byte source is called before every earlier
 * check has passed. A deterministic id reads no time and no random field, so
 * after UXID_ERR_RAND_WITH_FROM the one refusal it can meet is
 * UXID_ERR_BUFFER_TOO_SMALL. On any status other than UXID_OK, out holds the
 * empty string when out is not NULL and out_size is at least 1.
 */
UXID_API uxid_status uxid_generate(char *out, size_t out_size, const uxid_options *opts);

/*
 * The name of a status, as a static NUL-terminated string: "ok" for UXID_OK;
 * for the six generation errors the conformance corpus's name
 * ("compact_time_out_of_range", "rand_width_mismatch",
 * "rand_size_too_large", "from_with_monotonic", "rand_with_from",
 * "rand_with_monotonic"); "buffer_too_small", "random_source" and
 * "invalid_argument" for this library's own three; "unknown" for a value
 * that is none of these. Never NULL.
 */
UXID_API const char *uxid_status_name(uxid_status status);

#ifdef __cplusplus
}
#endif

#endif /* UXID_H */
