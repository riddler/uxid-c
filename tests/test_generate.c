/*
 * uxid_generate, uxid_options_init and uxid_status_name. The encode and
 * refusal vectors are cases of the uxid conformance corpus (encode.json and
 * the generate cases of invalid.json at the reference's v2.10.0 tag), each
 * named by its case id.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uxid.h>

#ifdef NDEBUG
#error "the tests assert with assert(); the test build never defines NDEBUG"
#endif

/* Reads two lowercase hex digits per byte into out; returns the byte count. */
static size_t hex_bytes(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = strlen(hex) / 2;
    assert(n <= cap);
    for (size_t i = 0; i < n; i++) {
        char pair[3];
        pair[0] = hex[2 * i];
        pair[1] = hex[(2 * i) + 1];
        pair[2] = '\0';
        char *end = NULL;
        unsigned long value = strtoul(pair, &end, 16);
        assert(end == pair + 2 && value <= 0xFFU);
        out[i] = (uint8_t)value;
    }
    return n;
}

/* The value of one Crockford Base32 symbol of either case, or -1. */
static int symbol_value(char c)
{
    static const char lower[] = "0123456789abcdefghjkmnpqrstvwxyz";
    static const char upper[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (int i = 0; i < 32; i++) {
        if (lower[i] == c || upper[i] == c) {
            return i;
        }
    }
    return -1;
}

/* Reads a standard time field, the first 10 symbols of a body. */
static uint64_t standard_time(const char *body)
{
    uint64_t t = 0;
    for (int i = 0; i < 10; i++) {
        int v = symbol_value(body[i]);
        assert(v >= 0);
        t = (t << 5U) | (uint64_t)v;
    }
    return t;
}

struct encode_case {
    const char *id;
    const char *prefix;
    uxid_size size;
    bool compact;
    uxid_case letter_case;
    uint64_t time;
    const char *rand;
    const char *expected;
};

/* Short names for the table below: a size, the time form, a case and the usual time. */
#define XS UXID_SIZE_XS
#define S UXID_SIZE_S
#define M UXID_SIZE_M
#define L UXID_SIZE_L
#define XL UXID_SIZE_XL
#define STD false
#define CPT true
#define LO UXID_CASE_LOWER
#define UP UXID_CASE_UPPER
#define T0 UINT64_C(1700000000000)

/* Each row is the corpus case encode/<id>. */
static const struct encode_case encode_cases[] = {
    {"xs-standard-lower", "usr", XS, STD, LO, T0, "", "usr_01hf7yat00"},
    {"xs-compact-upper", "usr", XS, CPT, UP, T0, "01", "usr_HF7YAT0001"},
    {"s-standard-lower", "usr", S, STD, LO, T0, "0123", "usr_01hf7yat0004h1"},
    {"s-compact-lower", "usr", S, CPT, LO, T0, "012345", "usr_hf7yat0004hm5"},
    {"m-standard-upper", "usr", M, STD, UP, T0, "0123456789", "usr_01HF7YAT0004HMASW9"},
    {"m-compact-lower", "usr", M, CPT, LO, T0, "0123456789ab", "usr_hf7yat0004hmasw9n3"},
    {"l-standard-lower", "usr", L, STD, LO, T0, "0123456789abcd", "usr_01hf7yat0004hmasw9nf61"},
    {"l-compact-upper", "usr", L, CPT, UP, T0, "0123456789abcdef", "usr_HF7YAT0004HMASW9NF6YF"},
    {"xl-standard-lower", "usr", XL, STD, LO, T0, "0123456789abcdef0123", "usr_01hf7yat0004hmasw9nf6yy093"},
    {"xl-compact-upper", "usr", XL, CPT, UP, T0, "0123456789abcdef0123", "usr_HF7YAT0004HMASW9NF6YY093"},
    {"xs-compact-rand-all-ones", "cus", XS, CPT, LO, T0, "ff", "cus_hf7yat00z7"},
    {"s-standard-rand-all-ones", "cus", S, STD, LO, T0, "ffff", "cus_01hf7yat00zzz1"},
    {"l-compact-rand-all-ones", "cus", L, CPT, LO, T0, "ffffffffffffffff", "cus_hf7yat00zzzzzzzzzzzzf"},
    {"xs-standard-time-zero", NULL, XS, STD, LO, 0, "", "0000000000"},
    {"xs-standard-time-max", NULL, XS, STD, LO, UINT64_C(281474976710655), "", "7zzzzzzzzz"},
    {"s-compact-guard-last-valid", "usr", S, CPT, LO, UINT64_C(1065151889407), "012345", "usr_yzzzzzzz04hm5"},
    {"xl-standard-no-prefix", NULL, XL, STD, LO, T0, "0123456789abcdef0123", "01hf7yat0004hmasw9nf6yy093"},
    {"xl-standard-empty-prefix", "", XL, STD, LO, T0, "0123456789abcdef0123", "01hf7yat0004hmasw9nf6yy093"},
    {"s-standard-prefix-with-delimiter", "a_b", S, STD, LO, T0, "0123", "a_b_01hf7yat0004h1"},
};

#undef XS
#undef S
#undef M
#undef L
#undef XL
#undef STD
#undef CPT
#undef LO
#undef UP
#undef T0

/*
 * A value read at run time from digits, so a test can put a number outside an
 * enum's members into an enum field without a cast the analyzer can follow.
 */
static int runtime_int(const char *digits)
{
    char *end = NULL;
    long value = strtol(digits, &end, 10);
    assert(end != digits && *end == '\0');
    return (int)value;
}

/* Milliseconds since the Unix epoch from the C11 clock. */
static uint64_t now_ms(void)
{
    struct timespec ts;
    assert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
    return ((uint64_t)ts.tv_sec * 1000U) + ((uint64_t)ts.tv_nsec / 1000000U);
}

/* Options for a case: its prefix, size, time form, case, time and fixed bytes. */
static uxid_options case_options(const struct encode_case *c, uint8_t *rand, size_t cap)
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = c->prefix;
    opts.size = c->size;
    opts.compact_time = c->compact;
    opts.letter_case = c->letter_case;
    opts.has_time = true;
    opts.time = c->time;
    opts.rand_len = hex_bytes(c->rand, rand, cap);
    opts.rand = rand;
    return opts;
}

/* A byte source that counts its calls and records what it was asked for. */
struct recording_source {
    int calls;
    size_t last_len;
    int fail;
    uint8_t fill;
};

static int recording_draw(void *ctx, uint8_t *out, size_t len)
{
    struct recording_source *src = ctx;
    src->calls++;
    src->last_len = len;
    if (src->fail) {
        return src->fail;
    }
    memset(out, src->fill, len);
    return 0;
}

static void test_options_init_defaults(void) /* (sabotage: init size L -> opts.size == UXID_SIZE_XL) */
{
    uxid_options opts;
    memset(&opts, 0xA5, sizeof opts);
    uxid_options_init(&opts);
    assert(opts.prefix == NULL);
    assert(opts.delimiter != NULL && strcmp(opts.delimiter, "_") == 0);
    assert(opts.size == UXID_SIZE_XL);
    assert(!opts.has_rand_size);
    assert(!opts.compact_time);
    assert(opts.letter_case == UXID_CASE_LOWER);
    assert(!opts.has_time);
    assert(opts.rand == NULL && opts.rand_len == 0);
    assert(opts.byte_source == NULL && opts.byte_source_ctx == NULL);
    for (size_t i = 0; i < sizeof opts.reserved / sizeof opts.reserved[0]; i++) {
        assert(opts.reserved[i] == NULL);
    }
}

static void test_encode_corpus_cases(void) /* (sabotage: upper H as I -> strcmp(out, c->expected) == 0) */
{
    for (size_t i = 0; i < sizeof encode_cases / sizeof encode_cases[0]; i++) {
        const struct encode_case *c = &encode_cases[i];
        uint8_t rand[10];
        uxid_options opts = case_options(c, rand, sizeof rand);
        char out[64];
        uxid_status status = uxid_generate(out, sizeof out, &opts);
        if (status != UXID_OK || strcmp(out, c->expected) != 0) {
            (void)fprintf(stderr, "encode/%s: status %s, got \"%s\", expected \"%s\"\n", c->id,
                          uxid_status_name(status), out, c->expected);
        }
        assert(status == UXID_OK);
        assert(strcmp(out, c->expected) == 0);
    }
}

static void test_null_options_default_xl(void) /* (sabotage: NULL opts size m -> strlen(out) == 26) */
{
    char out[UXID_BODY_MAX_LENGTH + 1];
    assert(uxid_generate(out, sizeof out, NULL) == UXID_OK);
    assert(strlen(out) == 26);
    for (size_t i = 0; i < 26; i++) {
        assert(symbol_value(out[i]) >= 0);
        assert(out[i] < 'A' || out[i] > 'Z');
    }
    assert(out[0] >= '0' && out[0] <= '7');
}

static void test_delimiter_joins_prefix(void) /* (sabotage: delimiter len 1 -> strcmp(out, "usr--01hf7yat00") == 0) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.delimiter = "--";
    opts.size = UXID_SIZE_XS;
    opts.has_time = true;
    opts.time = 1700000000000;
    char out[32];
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr--01hf7yat00") == 0);

    opts.prefix = "";
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "01hf7yat00") == 0);
}

static void test_rand_size_sets_width(void) /* (sabotage: rand_size ignored -> uxid_generate() == UXID_OK) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.size = UXID_SIZE_S;
    opts.has_time = true;
    opts.time = 1700000000000;
    opts.has_rand_size = true;
    opts.rand_size = 3;
    const uint8_t rand[] = {0x01, 0x23, 0x45};
    opts.rand = rand;
    opts.rand_len = sizeof rand;
    char out[32];
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_01hf7yat0004hm5") == 0);

    struct recording_source src = {0, 0, 0, 0xFF};
    opts.rand = NULL;
    opts.rand_len = 0;
    opts.rand_size = 10;
    opts.byte_source = recording_draw;
    opts.byte_source_ctx = &src;
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(src.calls == 1 && src.last_len == 10);
    assert(strcmp(out, "usr_01hf7yat00zzzzzzzzzzzzzzzz") == 0);
}

static void test_rand_size_too_large(void) /* (sabotage: wrong refusal -> status == UXID_ERR_RAND_SIZE_TOO_LARGE) */
{
    /* invalid/generate-rand-size-11 */
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.size = UXID_SIZE_XL;
    opts.has_time = true;
    opts.time = 1700000000000;
    opts.has_rand_size = true;
    opts.rand_size = 11;
    char out[64];
    memset(out, 'x', sizeof out);
    uxid_status status = uxid_generate(out, sizeof out, &opts);
    assert(status == UXID_ERR_RAND_SIZE_TOO_LARGE);
    assert(out[0] == '\0');
    assert(strcmp(uxid_status_name(status), "rand_size_too_large") == 0);
}

static void test_rand_width_mismatch(void) /* (sabotage: no check -> uxid_generate() == UXID_ERR_RAND_WIDTH_MISMATCH) */
{
    /* invalid/generate-rand-too-wide, -too-narrow and -on-xs-standard */
    static const struct {
        uxid_size size;
        const char *rand;
    } cases[] = {
        {UXID_SIZE_S, "010203"},
        {UXID_SIZE_XL, "010203040506070809"},
        {UXID_SIZE_XS, "01"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uxid_options opts;
        uxid_options_init(&opts);
        opts.prefix = "usr";
        opts.size = cases[i].size;
        opts.has_time = true;
        opts.time = 1700000000000;
        uint8_t rand[10];
        opts.rand_len = hex_bytes(cases[i].rand, rand, sizeof rand);
        opts.rand = rand;
        char out[64];
        memset(out, 'x', sizeof out);
        assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_RAND_WIDTH_MISMATCH);
        assert(out[0] == '\0');
    }
}

static void test_compact_time_guard(void) /* (sabotage: guard >> 36 -> status == UXID_ERR_COMPACT_TIME_OUT_OF_RANGE) */
{
    /* invalid/generate-compact-time-at-guard and -at-guard-previous-window */
    static const uint64_t times[] = {2164663517184, 1065151889408};
    for (size_t i = 0; i < sizeof times / sizeof times[0]; i++) {
        uxid_options opts;
        uxid_options_init(&opts);
        opts.prefix = "usr";
        opts.size = UXID_SIZE_S;
        opts.compact_time = true;
        opts.has_time = true;
        opts.time = times[i];
        const uint8_t rand[] = {0x01, 0x02, 0x03};
        opts.rand = rand;
        opts.rand_len = sizeof rand;
        char out[64];
        memset(out, 'x', sizeof out);
        uxid_status status = uxid_generate(out, sizeof out, &opts);
        assert(status == UXID_ERR_COMPACT_TIME_OUT_OF_RANGE);
        assert(out[0] == '\0');

        /* Standard time is never refused for this. */
        opts.compact_time = false;
        opts.rand_len = 2;
        assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    }
}

static void test_buffer_exact_fit(void) /* (sabotage: fit needs a spare byte -> uxid_generate() == UXID_OK) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.size = UXID_SIZE_XS;
    opts.has_time = true;
    opts.time = 1700000000000;
    char out[sizeof "usr_01hf7yat00" + 1];
    memset(out, 'x', sizeof out);
    assert(uxid_generate(out, sizeof "usr_01hf7yat00", &opts) == UXID_OK);
    assert(strcmp(out, "usr_01hf7yat00") == 0);
    assert(out[sizeof "usr_01hf7yat00"] == 'x');
}

static void test_buffer_too_small(void) /* (sabotage: > for >= -> uxid_generate() == UXID_ERR_BUFFER_TOO_SMALL) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.size = UXID_SIZE_XS;
    opts.has_time = true;
    opts.time = 1700000000000;
    struct recording_source src = {0, 0, 0, 0};
    opts.byte_source = recording_draw;
    opts.byte_source_ctx = &src;
    opts.compact_time = true;
    char out[32];
    const size_t needed = sizeof "usr_hf7yat0000";
    for (size_t size = 1; size < needed; size++) {
        memset(out, 'x', sizeof out);
        assert(uxid_generate(out, size, &opts) == UXID_ERR_BUFFER_TOO_SMALL);
        assert(out[0] == '\0');
        for (size_t i = 1; i < sizeof out; i++) {
            assert(out[i] == 'x');
        }
    }
    memset(out, 'x', sizeof out);
    assert(uxid_generate(out, 0, &opts) == UXID_ERR_BUFFER_TOO_SMALL);
    assert(out[0] == 'x');
    assert(src.calls == 0);
    assert(strcmp(uxid_status_name(UXID_ERR_BUFFER_TOO_SMALL), "buffer_too_small") == 0);
}

static void test_byte_source_fills_field(void) /* (sabotage: injected source ignored -> src.calls == 1) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "cus";
    opts.size = UXID_SIZE_S;
    opts.has_time = true;
    opts.time = 1700000000000;
    struct recording_source src = {0, 0, 0, 0xFF};
    opts.byte_source = recording_draw;
    opts.byte_source_ctx = &src;
    char out[32];
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(src.calls == 1);
    assert(src.last_len == 2);
    assert(strcmp(out, "cus_01hf7yat00zzz1") == 0);
}

static void test_byte_source_not_called(void) /* (sabotage: draw for an empty field -> src.calls == 0) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.size = UXID_SIZE_XS;
    opts.has_time = true;
    opts.time = 1700000000000;
    struct recording_source src = {0, 0, 0, 0};
    opts.byte_source = recording_draw;
    opts.byte_source_ctx = &src;
    char out[32];
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    const uint8_t rand[] = {0x01, 0x23};
    opts.size = UXID_SIZE_S;
    opts.rand = rand;
    opts.rand_len = sizeof rand;
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "01hf7yat0004h1") == 0);
    assert(src.calls == 0);
}

static void test_byte_source_failure(void) /* (sabotage: only > 0 is failure -> status == UXID_ERR_RANDOM_SOURCE) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "usr";
    struct recording_source src = {0, 0, -5, 0};
    opts.byte_source = recording_draw;
    opts.byte_source_ctx = &src;
    char out[64];
    memset(out, 'x', sizeof out);
    uxid_status status = uxid_generate(out, sizeof out, &opts);
    assert(status == UXID_ERR_RANDOM_SOURCE);
    assert(src.calls == 1);
    assert(out[0] == '\0');
    assert(strcmp(uxid_status_name(status), "random_source") == 0);
}

static void test_platform_source_draws(void) /* (sabotage: platform source zeros -> strcmp(first, second) != 0) */
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.has_time = true;
    opts.time = 1700000000000;
    char first[UXID_BODY_MAX_LENGTH + 1];
    char second[UXID_BODY_MAX_LENGTH + 1];
    assert(uxid_generate(first, sizeof first, &opts) == UXID_OK);
    assert(uxid_generate(second, sizeof second, &opts) == UXID_OK);
    assert(strncmp(first, "01hf7yat00", 10) == 0);
    assert(strncmp(second, "01hf7yat00", 10) == 0);
    /* Two 80-bit draws are equal with probability 2^-80. */
    assert(strcmp(first, second) != 0);
}

static void test_system_clock(void) /* (sabotage: clock in seconds -> time >= before) */
{
    uint64_t before = now_ms();
    char out[UXID_BODY_MAX_LENGTH + 1];
    assert(uxid_generate(out, sizeof out, NULL) == UXID_OK);
    uint64_t after = now_ms();
    uint64_t time = standard_time(out);
    assert(time >= before);
    assert(time <= after);
}

static void test_invalid_arguments(void) /* (sabotage: "" taken -> uxid_generate() == UXID_ERR_INVALID_ARGUMENT) */
{
    char out[64];
    uxid_options opts;

    assert(uxid_generate(NULL, 64, NULL) == UXID_ERR_INVALID_ARGUMENT);

    uxid_options_init(&opts);
    opts.prefix = "usr";
    opts.delimiter = NULL;
    memset(out, 'x', sizeof out);
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    assert(out[0] == '\0');

    opts.delimiter = "";
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    uxid_options_init(&opts);
    opts.size = (uxid_size)runtime_int("5");
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    opts.size = (uxid_size)runtime_int("-1");
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    uxid_options_init(&opts);
    opts.letter_case = (uxid_case)runtime_int("2");
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    uxid_options_init(&opts);
    opts.has_time = true;
    opts.time = UINT64_C(281474976710656);
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    opts.time = UINT64_C(281474976710655);
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);

    uxid_options_init(&opts);
    opts.rand_len = 10;
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    uxid_options_init(&opts);
    int slot = 0;
    opts.reserved[7] = &slot;
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    assert(strcmp(uxid_status_name(UXID_ERR_INVALID_ARGUMENT), "invalid_argument") == 0);
}

static void test_size_aliases(void) /* (sabotage: LARGE aliases M -> strcmp(by_alias, by_name) == 0) */
{
    static const struct {
        uxid_size alias;
        uxid_size size;
    } pairs[] = {
        {UXID_SIZE_XSMALL, UXID_SIZE_XS}, {UXID_SIZE_SMALL, UXID_SIZE_S},   {UXID_SIZE_MEDIUM, UXID_SIZE_M},
        {UXID_SIZE_LARGE, UXID_SIZE_L},   {UXID_SIZE_XLARGE, UXID_SIZE_XL},
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        uxid_options opts;
        uxid_options_init(&opts);
        opts.has_time = true;
        opts.time = 1700000000000;
        opts.byte_source = recording_draw;
        struct recording_source src = {0, 0, 0, 0x5A};
        opts.byte_source_ctx = &src;
        char by_alias[UXID_BODY_MAX_LENGTH + 1];
        char by_name[UXID_BODY_MAX_LENGTH + 1];
        opts.size = pairs[i].alias;
        assert(uxid_generate(by_alias, sizeof by_alias, &opts) == UXID_OK);
        opts.size = pairs[i].size;
        assert(uxid_generate(by_name, sizeof by_name, &opts) == UXID_OK);
        assert(strcmp(by_alias, by_name) == 0);
    }
}

static void test_status_names(void) /* (sabotage: rand-with-from -> strcmp(got, name) == 0) */
{
    static const struct {
        uxid_status status;
        int value;
        const char *name;
    } names[] = {
        {UXID_OK, 0, "ok"},
        {UXID_ERR_COMPACT_TIME_OUT_OF_RANGE, 1, "compact_time_out_of_range"},
        {UXID_ERR_RAND_WIDTH_MISMATCH, 2, "rand_width_mismatch"},
        {UXID_ERR_RAND_SIZE_TOO_LARGE, 3, "rand_size_too_large"},
        {UXID_ERR_FROM_WITH_MONOTONIC, 4, "from_with_monotonic"},
        {UXID_ERR_RAND_WITH_FROM, 5, "rand_with_from"},
        {UXID_ERR_RAND_WITH_MONOTONIC, 6, "rand_with_monotonic"},
        {UXID_ERR_BUFFER_TOO_SMALL, 7, "buffer_too_small"},
        {UXID_ERR_RANDOM_SOURCE, 8, "random_source"},
        {UXID_ERR_INVALID_ARGUMENT, 9, "invalid_argument"},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        uxid_status status = names[i].status;
        const char *name = names[i].name;
        assert((int)status == names[i].value);
        const char *got = uxid_status_name(status);
        assert(strcmp(got, name) == 0);
    }
    assert(strcmp(uxid_status_name((uxid_status)runtime_int("10")), "unknown") == 0);
}

int main(void)
{
    test_options_init_defaults();
    test_buffer_exact_fit();
    test_buffer_too_small();
    test_byte_source_fills_field();
    test_byte_source_not_called();
    test_byte_source_failure();
    test_encode_corpus_cases();
    test_null_options_default_xl();
    test_delimiter_joins_prefix();
    test_rand_size_sets_width();
    test_rand_size_too_large();
    test_rand_width_mismatch();
    test_compact_time_guard();
    test_platform_source_draws();
    test_system_clock();
    test_invalid_arguments();
    test_size_aliases();
    test_status_names();
    return 0;
}
