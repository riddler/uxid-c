/*
 * uxid: the library source.
 *
 * SPDX-License-Identifier: MIT
 */

#include "uxid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
/* windows.h comes first: bcrypt.h uses its types. */
#include <bcrypt.h>
#elif defined(__linux__)
#include <errno.h>
#include <sys/random.h>
#include <sys/types.h>
/*
 * EINTR is POSIX, not C; glibc's errno.h takes it from the kernel's
 * errno-base.h. Naming that header as well lets clang-tidy see where it is
 * defined.
 */
#if defined(__has_include)
#if __has_include(<asm-generic/errno-base.h>)
#include <asm-generic/errno-base.h>
#endif
#endif
#else
#include <stdlib.h>
/*
 * Recent macOS SDKs declare arc4random_buf in _stdlib.h, which stdlib.h
 * includes; naming it as well lets clang-tidy see the declaring header.
 */
#if defined(__APPLE__) && defined(__has_include)
#if __has_include(<_stdlib.h>)
#include <_stdlib.h>
#endif
#endif
#endif

_Static_assert(sizeof UXID_VERSION_STRING >= sizeof "0.0.0", "UXID_VERSION_STRING is MAJOR.MINOR.PATCH");

/* The latest time a standard time field holds: 2^48 - 1 milliseconds. */
#define UXID_MAX_TIME ((UINT64_C(1) << 48U) - 1U)

/* The low 40 bits a compact time field holds. */
#define UXID_COMPACT_MASK ((UINT64_C(1) << 40U) - 1U)

/* The widest random field at any size, in bytes. */
#define UXID_MAX_RAND_BYTES 10U

/* The deterministic marker's value, which a compact time may not start with. */
#define UXID_MARKER 31U

/* The number of reserved option slots. */
#define UXID_RESERVED_SLOTS (sizeof((uxid_options *)0)->reserved / sizeof((uxid_options *)0)->reserved[0])

/* Crockford Base32, value to symbol, in each case. */
static const char uxid_lower_alphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
static const char uxid_upper_alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

void uxid_options_init(uxid_options *opts)
{
    if (opts == NULL) {
        return;
    }
    memset(opts, 0, sizeof *opts);
    opts->prefix = NULL;
    opts->delimiter = "_";
    opts->size = UXID_SIZE_XL;
    opts->has_rand_size = false;
    opts->rand_size = 0;
    opts->compact_time = false;
    opts->letter_case = UXID_CASE_LOWER;
    opts->has_time = false;
    opts->time = 0;
    opts->rand = NULL;
    opts->rand_len = 0;
    opts->byte_source = NULL;
    opts->byte_source_ctx = NULL;
    for (size_t i = 0; i < UXID_RESERVED_SLOTS; i++) {
        opts->reserved[i] = NULL;
    }
}

const char *uxid_status_name(uxid_status status)
{
    switch (status) {
    case UXID_OK:
        return "ok";
    case UXID_ERR_COMPACT_TIME_OUT_OF_RANGE:
        return "compact_time_out_of_range";
    case UXID_ERR_RAND_WIDTH_MISMATCH:
        return "rand_width_mismatch";
    case UXID_ERR_RAND_SIZE_TOO_LARGE:
        return "rand_size_too_large";
    case UXID_ERR_FROM_WITH_MONOTONIC:
        return "from_with_monotonic";
    case UXID_ERR_RAND_WITH_FROM:
        return "rand_with_from";
    case UXID_ERR_RAND_WITH_MONOTONIC:
        return "rand_with_monotonic";
    case UXID_ERR_BUFFER_TOO_SMALL:
        return "buffer_too_small";
    case UXID_ERR_RANDOM_SOURCE:
        return "random_source";
    case UXID_ERR_INVALID_ARGUMENT:
        return "invalid_argument";
    default:
        return "unknown";
    }
}

/* The random field's width in bytes for a size and time form. */
static size_t uxid_rand_bytes(uxid_size size, bool compact)
{
    switch (size) {
    case UXID_SIZE_XS:
        return compact ? 1U : 0U;
    case UXID_SIZE_S:
        return compact ? 3U : 2U;
    case UXID_SIZE_M:
        return compact ? 6U : 5U;
    case UXID_SIZE_L:
        return compact ? 8U : 7U;
    default:
        return UXID_MAX_RAND_BYTES;
    }
}

/* The platform's secure generator: fills out with len bytes, 0 on success. */
static int uxid_platform_random(uint8_t *out, size_t len)
{
#ifdef _WIN32
    while (len > 0) {
        ULONG chunk = len > (size_t)0xFFFFFFFFU ? 0xFFFFFFFFU : (ULONG)len;
        NTSTATUS status = BCryptGenRandom(NULL, out, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!BCRYPT_SUCCESS(status)) {
            return -1;
        }
        out += chunk;
        len -= chunk;
    }
    return 0;
#elif defined(__linux__)
    while (len > 0) {
        ssize_t got = getrandom(out, len, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (got == 0) {
            return -1;
        }
        out += (size_t)got;
        len -= (size_t)got;
    }
    return 0;
#else
    arc4random_buf(out, len);
    return 0;
#endif
}

/*
 * The system clock in milliseconds since the Unix epoch, 0 on success. C11's
 * timespec_get reads the realtime clock on every platform this library
 * supports (on Windows, the Universal CRT's, from the system time).
 */
static int uxid_now_ms(uint64_t *now)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC || ts.tv_sec < 0) {
        return -1;
    }
    *now = ((uint64_t)ts.tv_sec * UINT64_C(1000)) + ((uint64_t)ts.tv_nsec / UINT64_C(1000000));
    return 0;
}

/* Writes the low 5 x count bits of value as count symbols, most significant first. */
static char *uxid_put_integer(char *at, uint64_t value, unsigned count, const char *alphabet)
{
    for (unsigned i = 0; i < count; i++) {
        unsigned shift = 5U * (count - 1U - i);
        *at++ = alphabet[(value >> shift) & 31U];
    }
    return at;
}

/*
 * Writes bytes as ceil(8 x len / 5) symbols, most significant bit first; the
 * last group, when shorter than 5 bits, is written as its raw value.
 */
static char *uxid_put_bytes(char *at, const uint8_t *bytes, size_t len, const char *alphabet)
{
    uint32_t acc = 0;
    unsigned bits = 0;
    for (size_t i = 0; i < len; i++) {
        acc = (acc << 8U) | bytes[i];
        bits += 8U;
        while (bits >= 5U) {
            bits -= 5U;
            *at++ = alphabet[(acc >> bits) & 31U];
        }
        acc &= (UINT32_C(1) << bits) - 1U;
    }
    if (bits > 0U) {
        *at++ = alphabet[acc];
    }
    return at;
}

/* The number of symbols len bytes take: ceil(8 x len / 5). */
static size_t uxid_rand_chars(size_t len)
{
    return ((8U * len) + 4U) / 5U;
}

/* Answers status, leaving out holding the empty string when it can. */
static uxid_status uxid_refuse(char *out, size_t out_size, uxid_status status)
{
    if (out != NULL && out_size > 0) {
        out[0] = '\0';
    }
    return status;
}

static bool uxid_reserved_zero(const uxid_options *opts)
{
    for (size_t i = 0; i < UXID_RESERVED_SLOTS; i++) {
        if (opts->reserved[i] != NULL) {
            return false;
        }
    }
    return true;
}

/* The checks behind UXID_ERR_INVALID_ARGUMENT that read only the options. */
static bool uxid_options_valid(const uxid_options *opts)
{
    if (opts->delimiter == NULL || opts->delimiter[0] == '\0') {
        return false;
    }
    if ((unsigned)opts->size > (unsigned)UXID_SIZE_XL) {
        return false;
    }
    if (opts->letter_case != UXID_CASE_LOWER && opts->letter_case != UXID_CASE_UPPER) {
        return false;
    }
    if (opts->has_time && opts->time > UXID_MAX_TIME) {
        return false;
    }
    if (opts->rand == NULL && opts->rand_len != 0) {
        return false;
    }
    return uxid_reserved_zero(opts);
}

/*
 * The random field's width for the request, or a refusal: an explicit width
 * above 10 bytes, or fixed bytes of another count.
 */
static uxid_status uxid_request_width(const uxid_options *opts, size_t *width)
{
    *width = uxid_rand_bytes(opts->size, opts->compact_time);
    if (opts->has_rand_size) {
        if (opts->rand_size > UXID_MAX_RAND_BYTES) {
            return UXID_ERR_RAND_SIZE_TOO_LARGE;
        }
        *width = opts->rand_size;
    }
    if (opts->rand != NULL && opts->rand_len != *width) {
        return UXID_ERR_RAND_WIDTH_MISMATCH;
    }
    return UXID_OK;
}

/*
 * The bytes the prefix and the delimiter take (none for no prefix), or false
 * when the sum does not fit in a size_t.
 */
static bool uxid_head_length(const uxid_options *opts, size_t *prefix_len, size_t *head_len)
{
    *prefix_len = opts->prefix == NULL ? 0U : strlen(opts->prefix);
    *head_len = 0;
    if (*prefix_len == 0) {
        return true;
    }
    size_t delimiter_len = strlen(opts->delimiter);
    if (*prefix_len > SIZE_MAX - delimiter_len) {
        return false;
    }
    *head_len = *prefix_len + delimiter_len;
    return true;
}

/* Fills drawn with width bytes from the byte source, 0 on success. */
static int uxid_draw(const uxid_options *opts, uint8_t *drawn, size_t width)
{
    if (opts->byte_source != NULL) {
        return opts->byte_source(opts->byte_source_ctx, drawn, width);
    }
    return uxid_platform_random(drawn, width);
}

uxid_status uxid_generate(char *out, size_t out_size, const uxid_options *opts)
{
    uxid_options defaults;
    if (opts == NULL) {
        uxid_options_init(&defaults);
        opts = &defaults;
    }
    if (out == NULL || !uxid_options_valid(opts)) {
        return uxid_refuse(out, out_size, UXID_ERR_INVALID_ARGUMENT);
    }

    uint64_t time = opts->time;
    if (!opts->has_time && (uxid_now_ms(&time) != 0 || time > UXID_MAX_TIME)) {
        return uxid_refuse(out, out_size, UXID_ERR_INVALID_ARGUMENT);
    }

    size_t width = 0;
    uxid_status status = uxid_request_width(opts, &width);
    if (status != UXID_OK) {
        return uxid_refuse(out, out_size, status);
    }

    uint64_t compact_time = time & UXID_COMPACT_MASK;
    if (opts->compact_time && (compact_time >> 35U) == UXID_MARKER) {
        return uxid_refuse(out, out_size, UXID_ERR_COMPACT_TIME_OUT_OF_RANGE);
    }

    size_t prefix_len = 0;
    size_t head_len = 0;
    size_t body_len = (opts->compact_time ? 8U : 10U) + uxid_rand_chars(width);
    /* The id and its NUL fit when head_len + body_len < out_size. */
    if (!uxid_head_length(opts, &prefix_len, &head_len) || head_len >= out_size || body_len >= out_size - head_len) {
        return uxid_refuse(out, out_size, UXID_ERR_BUFFER_TOO_SMALL);
    }

    uint8_t drawn[UXID_MAX_RAND_BYTES];
    const uint8_t *rand = opts->rand;
    if (rand == NULL && width > 0) {
        if (uxid_draw(opts, drawn, width) != 0) {
            return uxid_refuse(out, out_size, UXID_ERR_RANDOM_SOURCE);
        }
        rand = drawn;
    }

    const char *alphabet = opts->letter_case == UXID_CASE_UPPER ? uxid_upper_alphabet : uxid_lower_alphabet;
    char *at = out;
    if (prefix_len > 0) {
        memcpy(at, opts->prefix, prefix_len);
        at += prefix_len;
        size_t delimiter_len = head_len - prefix_len;
        memcpy(at, opts->delimiter, delimiter_len);
        at += delimiter_len;
    }
    if (opts->compact_time) {
        at = uxid_put_integer(at, compact_time, 8U, alphabet);
    } else {
        at = uxid_put_integer(at, time, 10U, alphabet);
    }
    if (width > 0) {
        at = uxid_put_bytes(at, rand, width, alphabet);
    }
    *at = '\0';
    return UXID_OK;
}
