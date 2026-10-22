/*
 * Malformed input for uxid_decode, uxid_is_valid and uxid_is_deterministic:
 * every truncation and extension of well-formed ids, every byte value at
 * every body position, embedded zero bytes, empty inputs and a fixed stream
 * of arbitrary bytes. Each input sits in a heap block of exactly its length
 * with no terminating NUL, so the sanitizer build (the asan-ubsan preset)
 * reports any read past the length and any undefined behavior. Every answer
 * is also checked against an independent reading of the wire format's
 * refusal rules.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <uxid.h>

#ifdef NDEBUG
#error "the tests assert with assert(); the test build never defines NDEBUG"
#endif

/* Well-formed ids at every length the length table lists. */
static const char *const seeds[] = {
    "usr_01hf7yat0004hmasw9nf6yy093",
    "usr_hf7yat0004hmasw9nf6yy093",
    "usr_01hf7yat0004hmasw9nf61",
    "usr_hf7yat0004hmasw9nf6yf",
    "usr_01hf7yat0004hmasw9",
    "usr_01hf7yat0004h1",
    "usr_hf7yat0004hm5",
    "usr_01hf7yat00",
    "a_b_01hf7yat000001",
    "zqz4cwzgyw8ks97869",
    "USR_ZCVT7EPAC0T1EBCSJFYF7CWZ25",
};

/* True when c is a Crockford Base32 symbol in either case. */
static bool in_alphabet(unsigned char c)
{
    static const char symbols[] = "0123456789abcdefghjkmnpqrstvwxyzABCDEFGHJKMNPQRSTVWXYZ";
    if (c == '\0' || memchr(symbols, c, sizeof symbols - 1U) == NULL) {
        return false;
    }
    return true;
}

/* The refusal the wire format gives the bytes, read independently of the library. */
static uxid_decode_status expected_status(const unsigned char *bytes, size_t len)
{
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] == '_') {
            start = i + 1U;
        }
    }
    size_t body_len = len - start;
    if (body_len == 0) {
        return UXID_DECODE_EMPTY;
    }
    switch (body_len) {
    case 10U:
    case 13U:
    case 14U:
    case 18U:
    case 21U:
    case 22U:
    case 24U:
    case 26U:
        break;
    default:
        return UXID_DECODE_INVALID_LENGTH;
    }
    for (size_t i = start; i < len; i++) {
        if (!in_alphabet(bytes[i])) {
            return UXID_DECODE_INVALID_CHARACTER;
        }
    }
    return UXID_DECODE_OK;
}

/* A clock that always reads the same time. */
static int steady_clock(void *ctx, uint64_t *now_ms)
{
    (void)ctx;
    *now_ms = UINT64_C(1700000000000);
    return 0;
}

/* The number of inputs checked, so a test can see it ran. */
struct tally {
    size_t inputs;
    size_t decoded;
};

/* Runs all three calls on an exact copy of the bytes and checks each answer. */
static void check(const unsigned char *bytes, size_t len, struct tally *tally)
{
    unsigned char *copy = malloc(len == 0 ? 1U : len);
    assert(copy != NULL);
    if (len > 0) {
        memcpy(copy, bytes, len);
    }
    const char *id = (const char *)copy;

    uxid_decode_options opts;
    uxid_decode_options_init(&opts);
    opts.clock = steady_clock;
    uxid_decoded out;
    uxid_decode_status status = uxid_decode(id, len, &opts, &out);
    uxid_decode_status expected = expected_status(bytes, len);
    assert(status == expected);
    bool valid = uxid_is_valid(id, len, "_", NULL);
    assert(valid == (status == UXID_DECODE_OK));
    bool deterministic = uxid_is_deterministic(id, len, "_");
    assert(deterministic == (status == UXID_DECODE_OK && out.deterministic));
    if (status == UXID_DECODE_OK) {
        tally->decoded++;
        assert(out.prefix == NULL || (out.prefix == id && out.prefix_len < len));
        assert(out.has_time == !out.deterministic);
        assert(out.time >= -(INT64_C(1) << 40) && out.time < (INT64_C(1) << 48));
    } else {
        assert(out.prefix == NULL && !out.has_time && out.time == 0);
    }
    tally->inputs++;
    free(copy);
}

static void test_truncated_and_overlong(void) /* (sabotage: length 13 read as 12 -> status == expected) */
{
    struct tally tally = {0, 0};
    unsigned char buf[64];
    for (size_t s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
        size_t len = strlen(seeds[s]);
        memcpy(buf, seeds[s], len);
        for (size_t cut = 0; cut <= len; cut++) {
            check(buf, cut, &tally);
        }
        for (size_t extra = 1; len + extra <= sizeof buf; extra++) {
            buf[len + extra - 1U] = (unsigned char)"0z_"[extra % 3U];
            check(buf, len + extra, &tally);
        }
    }
    assert(tally.inputs > 0 && tally.decoded >= sizeof seeds / sizeof seeds[0]);
}

static void test_wrong_alphabet(void) /* (sabotage: bytes above 127 read as 0 -> status == expected) */
{
    struct tally tally = {0, 0};
    unsigned char buf[64];
    for (size_t s = 0; s < sizeof seeds / sizeof seeds[0]; s++) {
        size_t len = strlen(seeds[s]);
        for (size_t pos = 0; pos < len; pos++) {
            for (unsigned value = 0; value < 256U; value++) {
                memcpy(buf, seeds[s], len);
                buf[pos] = (unsigned char)value;
                check(buf, len, &tally);
            }
        }
    }
    assert(tally.inputs > 0 && tally.decoded > 0);
}

static void test_embedded_nul_and_empty(void) /* (sabotage: zero byte read as 0 -> status == expected) */
{
    struct tally tally = {0, 0};
    static const unsigned char nul_in_body[] = "usr_01hf7y\0t0004h1";
    check(nul_in_body, sizeof nul_in_body - 1U, &tally);
    static const unsigned char nul_in_prefix[] = "u\0r_01hf7yat0004h1";
    check(nul_in_prefix, sizeof nul_in_prefix - 1U, &tally);
    static const unsigned char all_nul[26] = {0};
    check(all_nul, sizeof all_nul, &tally);
    static const unsigned char empty[] = "";
    check(empty, 0, &tally);
    static const unsigned char delimiters[] = "____";
    for (size_t len = 0; len <= 4; len++) {
        check(delimiters, len, &tally);
    }
    assert(tally.inputs == 9);
    assert(tally.decoded == 1);

    uxid_decoded out;
    assert(uxid_decode(NULL, 0, NULL, &out) == UXID_DECODE_EMPTY);
    assert(!uxid_is_valid(NULL, 0, "_", NULL));
    assert(!uxid_is_deterministic(NULL, 0, "_"));
}

/* A fixed xorshift stream: arbitrary bytes for the test, not a random source. */
static uint32_t next_value(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    *state = x;
    return x;
}

static void test_arbitrary_bytes(void) /* (sabotage: scan skips the byte after a delimiter -> status == expected) */
{
    struct tally tally = {0, 0};
    static const char alphabet[] = "0123456789abcdefghjkmnpqrstvwxyzZ_-ilou\x80\xff";
    uint32_t state = 0x9e3779b9U;
    unsigned char buf[40];
    for (int round = 0; round < 20000; round++) {
        size_t len = next_value(&state) % (sizeof buf + 1U);
        bool any_byte = (next_value(&state) & 1U) != 0;
        for (size_t i = 0; i < len; i++) {
            uint32_t v = next_value(&state);
            buf[i] = any_byte ? (unsigned char)v : (unsigned char)alphabet[v % (sizeof alphabet - 1U)];
        }
        check(buf, len, &tally);
    }
    assert(tally.inputs == 20000);
}

int main(void)
{
    test_truncated_and_overlong();
    test_wrong_alphabet();
    test_embedded_nul_and_empty();
    test_arbitrary_bytes();
    return 0;
}
