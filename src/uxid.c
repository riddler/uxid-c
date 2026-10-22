/*
 * uxid: the library source.
 *
 * SPDX-License-Identifier: MIT
 */

#include "uxid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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
#include <pthread.h>
#include <sys/random.h>
#include <sys/types.h>
/* glibc defines the pthread types in bits/pthreadtypes.h, which pthread.h includes. */
#ifdef __has_include
#if __has_include(<bits/pthreadtypes.h>)
#include <bits/pthreadtypes.h>
#endif
#endif
/*
 * EINTR is POSIX, not C; glibc's errno.h takes it from the kernel's
 * errno-base.h. Naming that header as well lets clang-tidy see where it is
 * defined.
 */
#ifdef __has_include
#if __has_include(<asm-generic/errno-base.h>)
#include <asm-generic/errno-base.h>
#endif
#endif
#else
#include <pthread.h>
/* Recent macOS SDKs define pthread_mutex_t in its own header, which pthread.h includes. */
#if defined(__APPLE__) && defined(__has_include)
#if __has_include(<sys/_pthread/_pthread_mutex_t.h>)
#include <sys/_pthread/_pthread_mutex_t.h>
#endif
#endif
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

/*
 * The layout of uxid_options before from and from_len took its first two
 * reserved slots. A caller allocates the struct at the size its header gives,
 * so the size and every earlier field's offset must not move.
 */
struct uxid_options_layout {
    const char *prefix;
    const char *delimiter;
    uxid_size size;
    bool has_rand_size;
    size_t rand_size;
    bool compact_time;
    uxid_case letter_case;
    bool has_time;
    uint64_t time;
    const uint8_t *rand;
    size_t rand_len;
    uxid_byte_source byte_source;
    void *byte_source_ctx;
    void *reserved[8];
};

#define UXID_SAME_OFFSET(field) (offsetof(uxid_options, field) == offsetof(struct uxid_options_layout, field))

_Static_assert(sizeof(uxid_options) == sizeof(struct uxid_options_layout), "uxid_options keeps its size");
_Static_assert(UXID_SAME_OFFSET(prefix) && UXID_SAME_OFFSET(delimiter) && UXID_SAME_OFFSET(size) &&
                   UXID_SAME_OFFSET(has_rand_size) && UXID_SAME_OFFSET(rand_size) && UXID_SAME_OFFSET(compact_time) &&
                   UXID_SAME_OFFSET(letter_case) && UXID_SAME_OFFSET(has_time) && UXID_SAME_OFFSET(time) &&
                   UXID_SAME_OFFSET(rand) && UXID_SAME_OFFSET(rand_len) && UXID_SAME_OFFSET(byte_source) &&
                   UXID_SAME_OFFSET(byte_source_ctx),
               "every field of uxid_options keeps its offset");
_Static_assert(offsetof(uxid_options, from) == offsetof(struct uxid_options_layout, reserved) &&
                   offsetof(uxid_options, from_len) ==
                       offsetof(struct uxid_options_layout, reserved) + sizeof(void *) &&
                   offsetof(uxid_options, reserved) ==
                       offsetof(struct uxid_options_layout, reserved) + (2 * sizeof(void *)),
               "from and from_len take the first two reserved slots");

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
    opts->from = NULL;
    opts->from_len = 0;
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
    case UXID_ERR_OUT_OF_RESOURCES:
        return "out_of_resources";
    default:
        return "unknown";
    }
}

/* sha256: begin (the SHA-256 of FIPS 180-4; the tests read this region) */

/* SHA-256's round constants, FIPS 180-4 section 4.2.2. */
static const uint32_t uxid_sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

/* The digest length of SHA-256, in bytes. */
#define UXID_SHA256_LENGTH 32U

/* A SHA-256 computation in progress, held by its caller. */
typedef struct uxid_sha256 {
    uint32_t state[8];
    uint8_t block[64];
    size_t used;
    uint64_t length;
} uxid_sha256;

static uint32_t uxid_rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32U - n));
}

/* The initial hash value, FIPS 180-4 section 5.3.3. */
static void uxid_sha256_init(uxid_sha256 *ctx)
{
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    memcpy(ctx->state, initial, sizeof initial);
    ctx->used = 0;
    ctx->length = 0;
}

/* Processes one 64-byte block, FIPS 180-4 section 6.2.2. */
static void uxid_sha256_block(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (size_t t = 0; t < 16U; t++) {
        w[t] = ((uint32_t)block[4U * t] << 24U) | ((uint32_t)block[(4U * t) + 1U] << 16U) |
               ((uint32_t)block[(4U * t) + 2U] << 8U) | (uint32_t)block[(4U * t) + 3U];
    }
    for (unsigned t = 16; t < 64U; t++) {
        uint32_t s0 = uxid_rotr(w[t - 15U], 7U) ^ uxid_rotr(w[t - 15U], 18U) ^ (w[t - 15U] >> 3U);
        uint32_t s1 = uxid_rotr(w[t - 2U], 17U) ^ uxid_rotr(w[t - 2U], 19U) ^ (w[t - 2U] >> 10U);
        w[t] = w[t - 16U] + s0 + w[t - 7U] + s1;
    }
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (unsigned t = 0; t < 64U; t++) {
        uint32_t big_s1 = uxid_rotr(e, 6U) ^ uxid_rotr(e, 11U) ^ uxid_rotr(e, 25U);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + big_s1 + ch + uxid_sha256_k[t] + w[t];
        uint32_t big_s0 = uxid_rotr(a, 2U) ^ uxid_rotr(a, 13U) ^ uxid_rotr(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = big_s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/* Adds len bytes at data to the message. */
static void uxid_sha256_update(uxid_sha256 *ctx, const uint8_t *data, size_t len)
{
    ctx->length += (uint64_t)len;
    while (len > 0) {
        size_t take = sizeof ctx->block - ctx->used;
        if (take > len) {
            take = len;
        }
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take;
        data += take;
        len -= take;
        if (ctx->used == sizeof ctx->block) {
            uxid_sha256_block(ctx->state, ctx->block);
            ctx->used = 0;
        }
    }
}

/* Pads the message (FIPS 180-4 section 5.1.1) and writes the 32-byte digest. */
static void uxid_sha256_final(uxid_sha256 *ctx, uint8_t digest[UXID_SHA256_LENGTH])
{
    uint64_t bits = ctx->length * 8U;
    ctx->block[ctx->used++] = 0x80U;
    if (ctx->used > 56U) {
        memset(ctx->block + ctx->used, 0, sizeof ctx->block - ctx->used);
        uxid_sha256_block(ctx->state, ctx->block);
        ctx->used = 0;
    }
    memset(ctx->block + ctx->used, 0, 56U - ctx->used);
    for (unsigned i = 0; i < 8U; i++) {
        ctx->block[56U + i] = (uint8_t)(bits >> (56U - (8U * i)));
    }
    uxid_sha256_block(ctx->state, ctx->block);
    for (size_t i = 0; i < 8U; i++) {
        digest[4U * i] = (uint8_t)(ctx->state[i] >> 24U);
        digest[(4U * i) + 1U] = (uint8_t)(ctx->state[i] >> 16U);
        digest[(4U * i) + 2U] = (uint8_t)(ctx->state[i] >> 8U);
        digest[(4U * i) + 3U] = (uint8_t)ctx->state[i];
    }
}

/* sha256: end */

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
    if (opts->rand == NULL && opts->rand_len != 0) {
        return false;
    }
    if (opts->from == NULL && opts->from_len != 0) {
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

/* The hash characters of a deterministic body at a size: 9, 13, 17, 21 or 25. */
static unsigned uxid_hash_chars(uxid_size size)
{
    return 9U + (4U * (unsigned)size);
}

/*
 * Writes the deterministic id: the prefix and the delimiter, the marker and
 * the first 5 x count bits of SHA-256(prefix || 0 || from || 0 || count).
 */
static uxid_status uxid_generate_from(char *out, size_t out_size, const uxid_options *opts)
{
    size_t prefix_len = 0;
    size_t head_len = 0;
    unsigned count = uxid_hash_chars(opts->size);
    size_t body_len = 1U + count;
    if (!uxid_head_length(opts, &prefix_len, &head_len) || head_len >= out_size || body_len >= out_size - head_len) {
        return uxid_refuse(out, out_size, UXID_ERR_BUFFER_TOO_SMALL);
    }

    static const uint8_t zero = 0;
    const uint8_t count_byte = (uint8_t)count;
    uxid_sha256 sha;
    uint8_t digest[UXID_SHA256_LENGTH];
    uxid_sha256_init(&sha);
    uxid_sha256_update(&sha, (const uint8_t *)opts->prefix, prefix_len);
    uxid_sha256_update(&sha, &zero, 1U);
    uxid_sha256_update(&sha, (const uint8_t *)opts->from, opts->from_len);
    uxid_sha256_update(&sha, &zero, 1U);
    uxid_sha256_update(&sha, &count_byte, 1U);
    uxid_sha256_final(&sha, digest);

    const char *alphabet = opts->letter_case == UXID_CASE_UPPER ? uxid_upper_alphabet : uxid_lower_alphabet;
    char *at = out;
    if (prefix_len > 0) {
        memcpy(at, opts->prefix, prefix_len);
        at += prefix_len;
        memcpy(at, opts->delimiter, head_len - prefix_len);
        at += head_len - prefix_len;
    }
    *at++ = alphabet[UXID_MARKER];
    for (unsigned i = 0; i < count; i++) {
        unsigned bit = 5U * i;
        unsigned pair = ((unsigned)digest[bit / 8U] << 8U) | digest[(bit / 8U) + 1U];
        *at++ = alphabet[(pair >> (11U - (bit % 8U))) & 31U];
    }
    *at = '\0';
    return UXID_OK;
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
    if (opts->from != NULL) {
        if (opts->rand != NULL) {
            return uxid_refuse(out, out_size, UXID_ERR_RAND_WITH_FROM);
        }
        return uxid_generate_from(out, out_size, opts);
    }

    uint64_t time = opts->time;
    if (opts->has_time ? time > UXID_MAX_TIME : (uxid_now_ms(&time) != 0 || time > UXID_MAX_TIME)) {
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

/* One day in milliseconds: how far ahead of the clock a compact time may be. */
#define UXID_ONE_DAY_MS UINT64_C(86400000)

/* The number of reserved decode option slots. */
#define UXID_DECODE_RESERVED_SLOTS                                                                                     \
    (sizeof((uxid_decode_options *)0)->reserved / sizeof((uxid_decode_options *)0)->reserved[0])

/* The number of reserved decoded slots. */
#define UXID_DECODED_RESERVED_SLOTS (sizeof((uxid_decoded *)0)->reserved / sizeof((uxid_decoded *)0)->reserved[0])

void uxid_decode_options_init(uxid_decode_options *opts)
{
    if (opts == NULL) {
        return;
    }
    memset(opts, 0, sizeof *opts);
    opts->delimiter = "_";
    opts->clock = NULL;
    opts->clock_ctx = NULL;
    for (size_t i = 0; i < UXID_DECODE_RESERVED_SLOTS; i++) {
        opts->reserved[i] = NULL;
    }
}

/* The value of one Crockford Base32 symbol in either case, or -1. */
static int uxid_symbol_value(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'Z') {
        c = (unsigned char)(c - 'A' + 'a');
    }
    if (c < 'a' || c > 'z') {
        return -1;
    }
    for (int i = 10; i < 32; i++) {
        if (uxid_lower_alphabet[i] == (char)c) {
            return i;
        }
    }
    return -1;
}

/*
 * The body of the len bytes at id: everything after the last delimiter a
 * left-to-right scan finds, its matches not overlapping. *prefix_end is the
 * delimiter's offset, or len when there is none (no prefix).
 */
static size_t uxid_split(const char *id, size_t len, const char *delimiter, size_t delimiter_len, size_t *prefix_end)
{
    size_t body_start = 0;
    *prefix_end = len;
    size_t at = 0;
    while (delimiter_len <= len - at) {
        if (memcmp(id + at, delimiter, delimiter_len) == 0) {
            *prefix_end = at;
            at += delimiter_len;
            body_start = at;
        } else {
            at++;
        }
    }
    return body_start;
}

/*
 * The first rule the body of body_len bytes breaks, in the order empty,
 * length, alphabet, or UXID_DECODE_OK with the size and time form its length
 * gives.
 */
static uxid_decode_status uxid_check_body(const char *body, size_t body_len, uxid_size *size, bool *compact)
{
    *compact = false;
    *size = UXID_SIZE_XL;
    if (body_len == 0) {
        return UXID_DECODE_EMPTY;
    }
    switch (body_len) {
    case 10U:
        *size = UXID_SIZE_XS;
        break;
    case 13U:
        *size = UXID_SIZE_S;
        *compact = true;
        break;
    case 14U:
        *size = UXID_SIZE_S;
        break;
    case 18U:
        *size = UXID_SIZE_M;
        break;
    case 21U:
        *size = UXID_SIZE_L;
        *compact = true;
        break;
    case 22U:
        *size = UXID_SIZE_L;
        break;
    case 24U:
        *compact = true;
        break;
    case 26U:
        break;
    default:
        return UXID_DECODE_INVALID_LENGTH;
    }
    for (size_t i = 0; i < body_len; i++) {
        if (uxid_symbol_value((unsigned char)body[i]) < 0) {
            return UXID_DECODE_INVALID_CHARACTER;
        }
    }
    return UXID_DECODE_OK;
}

/* A well-formed body and where it sits in the id. */
struct uxid_parsed {
    size_t prefix_end;
    const char *body;
    size_t body_len;
    uxid_size size;
    bool compact;
};

/*
 * Splits and checks the id, the delimiter already known to be non-empty. A
 * NULL id with len 0 is the empty string.
 */
static uxid_decode_status uxid_parse(const char *id, size_t len, const char *delimiter, struct uxid_parsed *parsed)
{
    if (id == NULL) {
        parsed->prefix_end = 0;
        parsed->body = NULL;
        parsed->body_len = 0;
        return UXID_DECODE_EMPTY;
    }
    size_t body_start = uxid_split(id, len, delimiter, strlen(delimiter), &parsed->prefix_end);
    parsed->body = id + body_start;
    parsed->body_len = len - body_start;
    return uxid_check_body(parsed->body, parsed->body_len, &parsed->size, &parsed->compact);
}

/* Reads count symbols of a checked body as one integer, most significant first. */
static uint64_t uxid_read_integer(const char *body, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; i++) {
        value = (value << 5U) | (uint64_t)uxid_symbol_value((unsigned char)body[i]);
    }
    return value;
}

/* The decoder's clock in milliseconds, 0 on success. */
static int uxid_decode_clock(const uxid_decode_options *opts, uint64_t *now)
{
    int failed = opts->clock != NULL ? opts->clock(opts->clock_ctx, now) : uxid_now_ms(now);
    if (failed != 0 || *now > UXID_MAX_TIME) {
        return -1;
    }
    return 0;
}

/* True when the body starts with the deterministic marker, z or Z. */
static bool uxid_is_marker(const char *body)
{
    if (body[0] == 'z' || body[0] == 'Z') {
        return true;
    }
    return false;
}
static bool uxid_decode_options_valid(const uxid_decode_options *opts)
{
    if (opts->delimiter == NULL || opts->delimiter[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < UXID_DECODE_RESERVED_SLOTS; i++) {
        if (opts->reserved[i] != NULL) {
            return false;
        }
    }
    return true;
}

static void uxid_decoded_clear(uxid_decoded *out)
{
    memset(out, 0, sizeof *out);
    out->prefix = NULL;
    out->prefix_len = 0;
    out->size = UXID_SIZE_XS;
    out->compact = false;
    out->deterministic = false;
    out->has_time = false;
    out->time = 0;
    for (size_t i = 0; i < UXID_DECODED_RESERVED_SLOTS; i++) {
        out->reserved[i] = NULL;
    }
}

uxid_decode_status uxid_decode(const char *id, size_t len, const uxid_decode_options *opts, uxid_decoded *out)
{
    uxid_decode_options defaults;
    if (opts == NULL) {
        uxid_decode_options_init(&defaults);
        opts = &defaults;
    }
    if (out == NULL) {
        return UXID_DECODE_INVALID_ARGUMENT;
    }
    uxid_decoded_clear(out);
    if ((id == NULL && len != 0) || !uxid_decode_options_valid(opts)) {
        return UXID_DECODE_INVALID_ARGUMENT;
    }

    struct uxid_parsed parsed;
    uxid_decode_status status = uxid_parse(id, len, opts->delimiter, &parsed);
    if (status != UXID_DECODE_OK) {
        return status;
    }

    bool deterministic = uxid_is_marker(parsed.body);
    int64_t time = 0;
    if (!deterministic && parsed.compact) {
        uint64_t now = 0;
        if (uxid_decode_clock(opts, &now) != 0) {
            return UXID_DECODE_INVALID_ARGUMENT;
        }
        /* Both are at most 2^48 - 1, so they fit an int64_t. */
        uint64_t full = (now & ~UXID_COMPACT_MASK) | uxid_read_integer(parsed.body, 8U);
        time = (int64_t)full;
        if (full > now && full - now > UXID_ONE_DAY_MS) {
            time -= (int64_t)(UXID_COMPACT_MASK + 1U);
        }
    } else if (!deterministic) {
        uint64_t first = uxid_read_integer(parsed.body, 1U) & 7U;
        time = (int64_t)((first << 45U) | uxid_read_integer(parsed.body + 1, 9U));
    }

    if (parsed.prefix_end < len) {
        out->prefix = id;
        out->prefix_len = parsed.prefix_end;
    }
    out->size = parsed.size;
    out->compact = parsed.compact;
    out->deterministic = deterministic;
    out->has_time = false;
    if (!deterministic) {
        out->has_time = true;
    }
    out->time = time;
    return UXID_DECODE_OK;
}

bool uxid_is_valid(const char *id, size_t len, const char *delimiter, const char *required_prefix)
{
    if ((id == NULL && len != 0) || delimiter == NULL || delimiter[0] == '\0') {
        return false;
    }
    struct uxid_parsed parsed;
    if (uxid_parse(id, len, delimiter, &parsed) != UXID_DECODE_OK) {
        return false;
    }
    if (required_prefix == NULL || required_prefix[0] == '\0') {
        return true;
    }
    size_t required_len = strlen(required_prefix);
    if (parsed.prefix_end == len || parsed.prefix_end != required_len) {
        return false;
    }
    if (memcmp(id, required_prefix, required_len) != 0) {
        return false;
    }
    return true;
}

bool uxid_is_deterministic(const char *id, size_t len, const char *delimiter)
{
    if ((id == NULL && len != 0) || delimiter == NULL || delimiter[0] == '\0') {
        return false;
    }
    struct uxid_parsed parsed;
    if (uxid_parse(id, len, delimiter, &parsed) != UXID_DECODE_OK) {
        return false;
    }
    return uxid_is_marker(parsed.body);
}

/* One prefix and random width's state: the last id's time and random value. */
struct uxid_monotonic_entry {
    /* A copy of the prefix's bytes, or NULL for no prefix. */
    char *prefix;
    size_t prefix_len;
    size_t width;
    uint64_t time;
    /* The random value, big-endian, in its first width bytes. */
    uint8_t value[UXID_MAX_RAND_BYTES];
};

/* What a uxid_monotonic's storage holds. */
struct uxid_monotonic_state {
#ifdef _WIN32
    SRWLOCK lock;
#else
    pthread_mutex_t lock;
#endif
    bool has_draws;
    uxid_draw_source draws;
    struct uxid_monotonic_entry *entries;
    size_t count;
    size_t capacity;
};

_Static_assert(sizeof(struct uxid_monotonic_state) <= sizeof(((uxid_monotonic *)0)->opaque),
               "the generator's state fits the storage uxid_monotonic gives it");
_Static_assert(_Alignof(struct uxid_monotonic_state) <= _Alignof(uxid_monotonic),
               "uxid_monotonic is aligned for the generator's state");

/* The number of reserved draw source slots. */
#define UXID_DRAW_RESERVED_SLOTS (sizeof((uxid_draw_source *)0)->reserved / sizeof((uxid_draw_source *)0)->reserved[0])

static struct uxid_monotonic_state *uxid_monotonic_state_of(uxid_monotonic *gen)
{
    void *storage = &gen->opaque;
    return storage;
}

static int uxid_monotonic_lock_init(struct uxid_monotonic_state *state)
{
#ifdef _WIN32
    InitializeSRWLock(&state->lock);
    return 0;
#else
    return pthread_mutex_init(&state->lock, NULL);
#endif
}

static void uxid_monotonic_lock(struct uxid_monotonic_state *state)
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&state->lock);
#else
    (void)pthread_mutex_lock(&state->lock);
#endif
}

static void uxid_monotonic_unlock(struct uxid_monotonic_state *state)
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&state->lock);
#else
    (void)pthread_mutex_unlock(&state->lock);
#endif
}

uxid_status uxid_monotonic_init(uxid_monotonic *gen, const uxid_draw_source *draws)
{
    if (gen == NULL) {
        return UXID_ERR_INVALID_ARGUMENT;
    }
    if (draws != NULL) {
        if (draws->seed == NULL || draws->step == NULL) {
            return UXID_ERR_INVALID_ARGUMENT;
        }
        for (size_t i = 0; i < UXID_DRAW_RESERVED_SLOTS; i++) {
            if (draws->reserved[i] != NULL) {
                return UXID_ERR_INVALID_ARGUMENT;
            }
        }
    }
    memset(gen, 0, sizeof *gen);
    struct uxid_monotonic_state *state = uxid_monotonic_state_of(gen);
    if (uxid_monotonic_lock_init(state) != 0) {
        return UXID_ERR_OUT_OF_RESOURCES;
    }
    state->has_draws = draws != NULL;
    if (draws != NULL) {
        state->draws = *draws;
    }
    state->entries = NULL;
    state->count = 0;
    state->capacity = 0;
    return UXID_OK;
}

void uxid_monotonic_destroy(uxid_monotonic *gen)
{
    if (gen == NULL) {
        return;
    }
    struct uxid_monotonic_state *state = uxid_monotonic_state_of(gen);
    for (size_t i = 0; i < state->count; i++) {
        free(state->entries[i].prefix);
    }
    free(state->entries);
    state->entries = NULL;
    state->count = 0;
    state->capacity = 0;
#ifndef _WIN32
    (void)pthread_mutex_destroy(&state->lock);
#endif
}

/* The entry for a prefix and width, or NULL when the generator has none. */
static struct uxid_monotonic_entry *uxid_monotonic_find(struct uxid_monotonic_state *state, const char *prefix,
                                                        size_t prefix_len, size_t width)
{
    for (size_t i = 0; i < state->count; i++) {
        struct uxid_monotonic_entry *entry = &state->entries[i];
        if (entry->width == width && entry->prefix_len == prefix_len &&
            (prefix_len == 0 || memcmp(entry->prefix, prefix, prefix_len) == 0)) {
            return entry;
        }
    }
    return NULL;
}

/*
 * Makes room for one more entry and copies the prefix for it into *copy
 * (NULL for no prefix), without adding the entry; false when memory runs out.
 */
static bool uxid_monotonic_reserve(struct uxid_monotonic_state *state, const char *prefix, size_t prefix_len,
                                   char **copy)
{
    *copy = NULL;
    if (state->count == state->capacity) {
        size_t capacity = state->capacity == 0 ? 4U : state->capacity * 2U;
        if (capacity > SIZE_MAX / sizeof *state->entries) {
            return false;
        }
        struct uxid_monotonic_entry *grown = realloc(state->entries, capacity * sizeof *grown);
        if (grown == NULL) {
            return false;
        }
        state->entries = grown;
        state->capacity = capacity;
    }
    if (prefix_len > 0) {
        if (prefix_len == SIZE_MAX) {
            return false;
        }
        *copy = malloc(prefix_len + 1U);
        if (*copy == NULL) {
            return false;
        }
        memcpy(*copy, prefix, prefix_len);
        (*copy)[prefix_len] = '\0';
    }
    return true;
}

/* A fresh random value for a field of width bytes, 0 on success. */
static int uxid_monotonic_seed(const struct uxid_monotonic_state *state, const uxid_options *opts, uint8_t *value,
                               size_t width)
{
    if (state->has_draws) {
        return state->draws.seed(state->draws.ctx, value, width);
    }
    return uxid_draw(opts, value, width);
}

/* A step from 1 to 2^(4 x width), uniform over it, 0 on success. */
static int uxid_monotonic_step(const struct uxid_monotonic_state *state, const uxid_options *opts, size_t width,
                               uint64_t *step)
{
    uint64_t max = UINT64_C(1) << (4U * width);
    if (state->has_draws) {
        *step = 0;
        if (state->draws.step(state->draws.ctx, width, step) != 0 || *step < 1U || *step > max) {
            return -1;
        }
        return 0;
    }
    uint8_t bytes[(UXID_MAX_RAND_BYTES + 1U) / 2U];
    size_t count = (width + 1U) / 2U;
    if (uxid_draw(opts, bytes, count) != 0) {
        return -1;
    }
    uint64_t drawn = 0;
    for (size_t i = 0; i < count; i++) {
        drawn = (drawn << 8U) | bytes[i];
    }
    *step = (drawn & (max - 1U)) + 1U;
    return 0;
}

/* Adds step to the big-endian value of width bytes; false when it overflows. */
static bool uxid_add_step(uint8_t *value, size_t width, uint64_t step)
{
    uint64_t carry = step;
    for (size_t i = width; i > 0 && carry != 0; i--) {
        uint64_t sum = (uint64_t)value[i - 1U] + (carry & 0xFFU);
        value[i - 1U] = (uint8_t)sum;
        carry = (carry >> 8U) + (sum >> 8U);
    }
    return carry == 0;
}

/* The checks of uxid_monotonic_next() that need no lock; width is set on UXID_OK. */
static uxid_status uxid_monotonic_check(const uxid_options *opts, size_t out_size, bool *compact, size_t *width)
{
    if (!uxid_options_valid(opts)) {
        return UXID_ERR_INVALID_ARGUMENT;
    }
    if (opts->from != NULL) {
        return UXID_ERR_FROM_WITH_MONOTONIC;
    }
    if (opts->rand != NULL) {
        return UXID_ERR_RAND_WITH_MONOTONIC;
    }
    /* A standard xs has no random field to step; a compact xs has a 1-byte one. */
    *compact = opts->compact_time;
    if (opts->size == UXID_SIZE_XS) {
        *compact = true;
    }
    *width = uxid_rand_bytes(opts->size, *compact);
    if (opts->has_rand_size) {
        if (opts->rand_size > UXID_MAX_RAND_BYTES) {
            return UXID_ERR_RAND_SIZE_TOO_LARGE;
        }
        *width = opts->rand_size;
    }
    if (*width == 0 || (opts->has_time && opts->time > UXID_MAX_TIME)) {
        return UXID_ERR_INVALID_ARGUMENT;
    }
    size_t prefix_len = 0;
    size_t head_len = 0;
    size_t body_len = (*compact ? 8U : 10U) + uxid_rand_chars(*width);
    if (!uxid_head_length(opts, &prefix_len, &head_len) || head_len >= out_size || body_len >= out_size - head_len) {
        return UXID_ERR_BUFFER_TOO_SMALL;
    }
    return UXID_OK;
}

/* Writes the id for a time and random value; the buffer is known to fit it. */
static void uxid_monotonic_write(char *out, const uxid_options *opts, bool compact, uint64_t time, const uint8_t *value,
                                 size_t width)
{
    const char *alphabet = opts->letter_case == UXID_CASE_UPPER ? uxid_upper_alphabet : uxid_lower_alphabet;
    size_t prefix_len = opts->prefix == NULL ? 0U : strlen(opts->prefix);
    char *at = out;
    if (prefix_len > 0) {
        size_t delimiter_len = strlen(opts->delimiter);
        memcpy(at, opts->prefix, prefix_len);
        at += prefix_len;
        memcpy(at, opts->delimiter, delimiter_len);
        at += delimiter_len;
    }
    if (compact) {
        at = uxid_put_integer(at, time & UXID_COMPACT_MASK, 8U, alphabet);
    } else {
        at = uxid_put_integer(at, time, 10U, alphabet);
    }
    at = uxid_put_bytes(at, value, width, alphabet);
    *at = '\0';
}

/* The time and random value of the next id, with the lock held. */
static uxid_status uxid_monotonic_advance(const struct uxid_monotonic_state *state, const uxid_options *opts,
                                          const struct uxid_monotonic_entry *last, uint64_t now, size_t width,
                                          uint64_t *time, uint8_t *value)
{
    if (last != NULL && now <= last->time) {
        uint64_t step = 0;
        if (uxid_monotonic_step(state, opts, width, &step) != 0) {
            return UXID_ERR_RANDOM_SOURCE;
        }
        memcpy(value, last->value, width);
        if (uxid_add_step(value, width, step)) {
            *time = last->time;
            return UXID_OK;
        }
        if (last->time == UXID_MAX_TIME) {
            return UXID_ERR_INVALID_ARGUMENT;
        }
        *time = last->time + 1U;
    } else {
        *time = now;
    }
    if (uxid_monotonic_seed(state, opts, value, width) != 0) {
        return UXID_ERR_RANDOM_SOURCE;
    }
    return UXID_OK;
}

/* uxid_monotonic_next() once the lock is held. */
static uxid_status uxid_monotonic_next_locked(struct uxid_monotonic_state *state, char *out, const uxid_options *opts,
                                              bool compact, size_t width)
{
    uint64_t now = opts->time;
    if (!opts->has_time && (uxid_now_ms(&now) != 0 || now > UXID_MAX_TIME)) {
        return UXID_ERR_INVALID_ARGUMENT;
    }
    const char *prefix = opts->prefix == NULL ? "" : opts->prefix;
    size_t prefix_len = strlen(prefix);
    struct uxid_monotonic_entry *last = uxid_monotonic_find(state, prefix, prefix_len, width);
    char *copy = NULL;
    if (last == NULL && !uxid_monotonic_reserve(state, prefix, prefix_len, &copy)) {
        return UXID_ERR_OUT_OF_RESOURCES;
    }

    uint64_t time = 0;
    uint8_t value[UXID_MAX_RAND_BYTES] = {0};
    uxid_status status = uxid_monotonic_advance(state, opts, last, now, width, &time, value);
    if (status == UXID_OK && compact && ((time & UXID_COMPACT_MASK) >> 35U) == UXID_MARKER) {
        status = UXID_ERR_COMPACT_TIME_OUT_OF_RANGE;
    }
    if (status != UXID_OK) {
        free(copy);
        return status;
    }

    uxid_monotonic_write(out, opts, compact, time, value, width);
    if (last == NULL) {
        last = &state->entries[state->count++];
        last->prefix = copy;
        last->prefix_len = prefix_len;
        last->width = width;
    }
    last->time = time;
    memcpy(last->value, value, width);
    return UXID_OK;
}

uxid_status uxid_monotonic_next(uxid_monotonic *gen, char *out, size_t out_size, const uxid_options *opts)
{
    uxid_options defaults;
    if (opts == NULL) {
        uxid_options_init(&defaults);
        opts = &defaults;
    }
    if (gen == NULL || out == NULL) {
        return uxid_refuse(out, out_size, UXID_ERR_INVALID_ARGUMENT);
    }
    bool compact = false;
    size_t width = 0;
    uxid_status status = uxid_monotonic_check(opts, out_size, &compact, &width);
    if (status != UXID_OK) {
        return uxid_refuse(out, out_size, status);
    }
    struct uxid_monotonic_state *state = uxid_monotonic_state_of(gen);
    uxid_monotonic_lock(state);
    status = uxid_monotonic_next_locked(state, out, opts, compact, width);
    uxid_monotonic_unlock(state);
    if (status != UXID_OK) {
        return uxid_refuse(out, out_size, status);
    }
    return UXID_OK;
}
