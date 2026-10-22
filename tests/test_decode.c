/*
 * uxid_decode, uxid_decode_options_init, uxid_is_valid and
 * uxid_is_deterministic. The decode and refusal vectors are cases of the uxid
 * conformance corpus (decode.json and the decode cases of invalid.json at the
 * reference's v2.10.0 tag), each named by its case id. Every input is copied
 * into a heap block of exactly its length with no terminating NUL, so a read
 * past the length is caught by the sanitizer build.
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

/* 2^40 ms, one compact time window. */
#define WINDOW (INT64_C(1) << 40)

/* One day in milliseconds. */
#define ONE_DAY INT64_C(86400000)

/* The time of most corpus decode cases. */
#define CASE_TIME INT64_C(1700000000000)

/* The largest compact time field the generator emits: 31 x 2^35 - 1. */
#define GUARD_LAST_VALID INT64_C(1065151889407)

/*
 * The decoder clock window every compact corpus case holds for, computed as
 * the corpus README computes it: from one day before the 1700000000000 ms
 * cases, inclusive, to one day before 2^40 + 1065151889407, exclusive.
 */
#define WINDOW_START (CASE_TIME - ONE_DAY)
#define WINDOW_END (WINDOW + GUARD_LAST_VALID - ONE_DAY)

_Static_assert(WINDOW_START == INT64_C(1699913600000), "the corpus README's window start");
_Static_assert(WINDOW_END == INT64_C(2164577117183), "the corpus README's window end");

/* A copy of s, without its NUL, in a block of exactly its length. */
static char *exact_copy(const char *s, size_t len)
{
    char *copy = malloc(len == 0 ? 1U : len);
    assert(copy != NULL);
    memcpy(copy, s, len);
    return copy;
}

/* A fixed clock that counts its calls. */
struct fixed_clock {
    uint64_t now;
    int calls;
    int fail;
};

static int fixed_clock_read(void *ctx, uint64_t *now_ms)
{
    struct fixed_clock *clock = ctx;
    clock->calls++;
    *now_ms = clock->now;
    return clock->fail;
}

/* Decodes the NUL-terminated s from an exact copy, under "_" and clock. */
static uxid_decode_status decode_at(const char *s, struct fixed_clock *clock, uxid_decoded *out)
{
    size_t len = strlen(s);
    char *copy = exact_copy(s, len);
    uxid_decode_options opts;
    uxid_decode_options_init(&opts);
    opts.clock = fixed_clock_read;
    opts.clock_ctx = clock;
    uxid_decode_status status = uxid_decode(copy, len, &opts, out);
    if (status == UXID_DECODE_OK && out->prefix != NULL) {
        assert(out->prefix == copy);
    }
    free(copy);
    return status;
}

/* The time a compact corpus case decodes to at a clock reading. */
static int64_t compact_time_at(const char *s, int64_t now)
{
    struct fixed_clock clock = {(uint64_t)now, 0, 0};
    uxid_decoded out;
    assert(decode_at(s, &clock, &out) == UXID_DECODE_OK);
    assert(out.compact && out.has_time);
    assert(clock.calls == 1);
    return out.time;
}

struct decode_case {
    const char *id;
    const char *input;
    const char *prefix;
    uxid_size size;
    bool compact;
    bool deterministic;
    int64_t time;
};

/* Each row is the corpus case decode/<id>; a deterministic row's time is unused. */
static const struct decode_case decode_cases[] = {
    {"xl-standard", "usr_01hf7yat0004hmasw9nf6yy093", "usr", UXID_SIZE_XL, false, false, CASE_TIME},
    {"xl-compact", "usr_hf7yat0004hmasw9nf6yy093", "usr", UXID_SIZE_XL, true, false, CASE_TIME},
    {"l-standard", "usr_01hf7yat0004hmasw9nf61", "usr", UXID_SIZE_L, false, false, CASE_TIME},
    {"l-compact", "usr_hf7yat0004hmasw9nf6yf", "usr", UXID_SIZE_L, true, false, CASE_TIME},
    {"m-standard", "usr_01hf7yat0004hmasw9", "usr", UXID_SIZE_M, false, false, CASE_TIME},
    {
        "m-compact-reads-as-standard",
        "usr_hf7yat0004hmasw9n3",
        "usr",
        UXID_SIZE_M,
        false,
        false,
        INT64_C(51950139736068),
    },
    {"s-standard", "usr_01hf7yat0004h1", "usr", UXID_SIZE_S, false, false, CASE_TIME},
    {"s-compact", "usr_hf7yat0004hm5", "usr", UXID_SIZE_S, true, false, CASE_TIME},
    {"xs-standard", "usr_01hf7yat00", "usr", UXID_SIZE_XS, false, false, CASE_TIME},
    {"xs-compact-reads-as-standard", "usr_hf7yat0001", "usr", UXID_SIZE_XS, false, false, INT64_C(51950139736065)},
    {"xl-standard-upper", "usr_01HF7YAT0004HMASW9NF6YY093", "usr", UXID_SIZE_XL, false, false, CASE_TIME},
    {"xl-standard-no-prefix", "01hf7yat0004hmasw9nf6yy093", NULL, UXID_SIZE_XL, false, false, CASE_TIME},
    {"s-standard-prefix-with-delimiter", "a_b_01hf7yat000001", "a_b", UXID_SIZE_S, false, false, CASE_TIME},
    {"s-compact-guard-last-valid", "usr_yzzzzzzz04hm5", "usr", UXID_SIZE_S, true, false, GUARD_LAST_VALID},
    {"deterministic-usr-xl", "usr_zcvt7epac0t1ebcsjfyf7cwz25", "usr", UXID_SIZE_XL, false, true, 0},
    {"deterministic-cus-s", "cus_zt9vnyfd85bff0", "cus", UXID_SIZE_S, false, true, 0},
    {"deterministic-no-prefix-m", "zqz4cwzgyw8ks97869", NULL, UXID_SIZE_M, false, true, 0},
    {"deterministic-upper-marker", "usr_ZCVT7EPAC0T1EBCSJFYF7CWZ25", "usr", UXID_SIZE_XL, false, true, 0},
};

struct invalid_case {
    const char *id;
    const char *input;
    uxid_decode_status expected;
};

/* Each row is the corpus case invalid/<id>. */
static const struct invalid_case invalid_cases[] = {
    {"decode-empty-string", "", UXID_DECODE_EMPTY},
    {"decode-ends-in-delimiter", "usr_", UXID_DECODE_EMPTY},
    {"decode-prefix-with-delimiter-ends-in-delimiter", "a_b_", UXID_DECODE_EMPTY},
    {"decode-delimiter-only", "_", UXID_DECODE_EMPTY},
    {"decode-length-11", "usr_01hf7yat000", UXID_DECODE_INVALID_LENGTH},
    {"decode-length-25", "usr_01hf7yat00000g40r40m30e20", UXID_DECODE_INVALID_LENGTH},
    {"decode-length-27", "usr_01hf7yat00000g40r40m30e2090", UXID_DECODE_INVALID_LENGTH},
    {"decode-no-delimiter-short", "usr", UXID_DECODE_INVALID_LENGTH},
    {"decode-character-in-time", "usr_01hu7yat00000g40r40m30e209", UXID_DECODE_INVALID_CHARACTER},
    {"decode-character-in-random", "usr_01hf7yat00000g4lr40m30e209", UXID_DECODE_INVALID_CHARACTER},
    {"decode-upper-o-in-random", "usr_01hf7yat00000g40r40mO0e209", UXID_DECODE_INVALID_CHARACTER},
    {"decode-hyphen-in-body", "usr_01hf7yat00-00g40r40m30e209", UXID_DECODE_INVALID_CHARACTER},
    {"decode-character-in-deterministic", "usr_zcvt7epac0t1ebcsjfyf7cwzi5", UXID_DECODE_INVALID_CHARACTER},
    {"decode-non-ascii-table-length", "01hf7yat00000g40r40m30e2\xc3\xa9", UXID_DECODE_INVALID_CHARACTER},
    {"decode-non-ascii-off-table-length", "01hf7yat00000g40r40m30e20\xc3\xa9", UXID_DECODE_INVALID_LENGTH},
    {"decode-length-and-character", "usr_01hf7yat00u", UXID_DECODE_INVALID_LENGTH},
};

/* True when every field of out is zero, false or NULL. */
static bool decoded_is_clear(const uxid_decoded *out)
{
    for (size_t i = 0; i < sizeof out->reserved / sizeof out->reserved[0]; i++) {
        if (out->reserved[i] != NULL) {
            return false;
        }
    }
    if (out->prefix != NULL || out->prefix_len != 0 || out->size != UXID_SIZE_XS || out->compact ||
        out->deterministic || out->has_time || out->time != 0) {
        return false;
    }
    return true;
}

/* Checks the decoded prefix is absent for NULL, else the length of prefix. */
static void assert_prefix(const uxid_decoded *out, const char *prefix)
{
    if (prefix == NULL) {
        assert(out->prefix == NULL);
        assert(out->prefix_len == 0);
        return;
    }
    assert(out->prefix != NULL);
    assert(out->prefix_len == strlen(prefix));
}
static void test_decode_options_init_defaults(void) /* (sabotage: delimiter "-" -> strcmp(opts.delimiter, "_") == 0) */
{
    uxid_decode_options opts;
    memset(&opts, 0xA5, sizeof opts);
    uxid_decode_options_init(&opts);
    assert(strcmp(opts.delimiter, "_") == 0);
    assert(opts.clock == NULL);
    assert(opts.clock_ctx == NULL);
    for (size_t i = 0; i < sizeof opts.reserved / sizeof opts.reserved[0]; i++) {
        assert(opts.reserved[i] == NULL);
    }
    uxid_decode_options_init(NULL);
}

static void test_decode_corpus_cases(void) /* (sabotage: first group << 44 -> out.time == c->time) */
{
    for (size_t i = 0; i < sizeof decode_cases / sizeof decode_cases[0]; i++) {
        const struct decode_case *c = &decode_cases[i];
        struct fixed_clock clock = {(uint64_t)CASE_TIME, 0, 0};
        uxid_decoded out;
        assert(decode_at(c->input, &clock, &out) == UXID_DECODE_OK);
        assert_prefix(&out, c->prefix);
        assert(out.size == c->size);
        assert(out.compact == c->compact);
        assert(out.deterministic == c->deterministic);
        assert(out.has_time == !c->deterministic);
        assert(out.time == c->time);
        assert(clock.calls == (c->compact ? 1 : 0));
    }
}

static void test_decode_prefix_bytes(void) /* (sabotage: prefix_len + 1 -> out.prefix_len == 3) */
{
    static const char id[] = "a_b_01hf7yat000001";
    uxid_decoded out;
    assert(uxid_decode(id, strlen(id), NULL, &out) == UXID_DECODE_OK);
    assert(out.prefix == id);
    assert(out.prefix_len == 3);
    assert(memcmp(out.prefix, "a_b", 3) == 0);

    static const char leading[] = "_01hf7yat000001";
    assert(uxid_decode(leading, strlen(leading), NULL, &out) == UXID_DECODE_OK);
    assert(out.prefix == leading);
    assert(out.prefix_len == 0);
}

static void test_decode_invalid_cases(void) /* (sabotage: length refusal dropped -> status == c->expected) */
{
    for (size_t i = 0; i < sizeof invalid_cases / sizeof invalid_cases[0]; i++) {
        const struct invalid_case *c = &invalid_cases[i];
        struct fixed_clock clock = {(uint64_t)CASE_TIME, 0, 0};
        uxid_decoded out;
        memset(&out, 0xA5, sizeof out);
        uxid_decode_status status = decode_at(c->input, &clock, &out);
        assert(status == c->expected);
        assert(decoded_is_clear(&out));
        assert(clock.calls == 0);
    }
}

static void test_clock_window_start(void) /* (sabotage: day check >= -> ...(WINDOW_START) == CASE_TIME) */
{
    assert(compact_time_at("usr_hf7yat0004hmasw9nf6yy093", WINDOW_START) == CASE_TIME);
    assert(compact_time_at("usr_hf7yat0004hm5", WINDOW_START) == CASE_TIME);
    assert(compact_time_at("usr_yzzzzzzz04hm5", WINDOW_START) == GUARD_LAST_VALID);
    /* One millisecond earlier, the case's time is more than a day ahead. */
    assert(compact_time_at("usr_hf7yat0004hmasw9nf6yy093", WINDOW_START - 1) == CASE_TIME - WINDOW);
}

static void test_clock_window_end(void) /* (sabotage: day + 1 -> ...(WINDOW_END - 1) == GUARD_LAST_VALID) */
{
    assert(compact_time_at("usr_yzzzzzzz04hm5", WINDOW_END - 1) == GUARD_LAST_VALID);
    assert(compact_time_at("usr_hf7yat0004hmasw9nf6yy093", WINDOW_END - 1) == CASE_TIME);
    /* At the end, the guard case is exactly one day ahead and is not stepped back. */
    assert(compact_time_at("usr_yzzzzzzz04hm5", WINDOW_END) == WINDOW + GUARD_LAST_VALID);
}

static void test_first_window_negative(void) /* (sabotage: clamp at 0 -> ...(..., 0) == low - WINDOW) */
{
    /* The low 40 bits of 1700000000000, more than a day ahead of a clock at 0. */
    int64_t low = CASE_TIME - WINDOW;
    assert(low > ONE_DAY);
    assert(compact_time_at("usr_hf7yat0004hmasw9nf6yy093", 0) == low - WINDOW);
    assert(compact_time_at("usr_hf7yat0004hmasw9nf6yy093", 0) == INT64_C(-499023255552));
    /* A compact time no more than a day ahead stays in the first window. */
    assert(compact_time_at("usr_hf7yat0004hmasw9nf6yy093", low) == low);
}

static void test_clock_not_called(void) /* (sabotage: clock read for every body -> clock.calls == 0) */
{
    static const char *const inputs[] = {
        "usr_01hf7yat0004hmasw9nf6yy093",
        "usr_zcvt7epac0t1ebcsjfyf7cwz25",
        "usr_01hu7yat00000g40r40m30e209",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        struct fixed_clock clock = {(uint64_t)CASE_TIME, 0, 0};
        uxid_decoded out;
        (void)decode_at(inputs[i], &clock, &out);
        assert(clock.calls == 0);
    }
}

static void test_clock_refusals(void) /* (sabotage: clock failure ignored -> decode_at(&failing) == ..._ARGUMENT) */
{
    uxid_decoded out;
    struct fixed_clock failing = {(uint64_t)CASE_TIME, 0, 1};
    assert(decode_at("usr_hf7yat0004hm5", &failing, &out) == UXID_DECODE_INVALID_ARGUMENT);
    assert(failing.calls == 1);
    assert(decoded_is_clear(&out));

    struct fixed_clock too_late = {(UINT64_C(1) << 48U), 0, 0};
    assert(decode_at("usr_hf7yat0004hm5", &too_late, &out) == UXID_DECODE_INVALID_ARGUMENT);
    assert(decoded_is_clear(&out));

    struct fixed_clock latest = {(UINT64_C(1) << 48U) - 1U, 0, 0};
    assert(decode_at("usr_hf7yat0004hm5", &latest, &out) == UXID_DECODE_OK);

    /* The clock fails only a compact time body: a body refusal comes first. */
    assert(decode_at("usr_hf7yat0004h", &failing, &out) == UXID_DECODE_INVALID_LENGTH);
}

static void test_system_clock(void) /* (sabotage: system clock read as 0 -> out.time == CASE_TIME) */
{
    static const char id[] = "usr_hf7yat0004hm5";
    uxid_decoded out;
    assert(uxid_decode(id, strlen(id), NULL, &out) == UXID_DECODE_OK);
    assert(out.compact);
    /* The machine's clock is inside the corpus window, so the case's time comes back. */
    assert(out.time == CASE_TIME);
}

static void test_decode_invalid_arguments(void) /* (sabotage: reserved unread -> uxid_decode(...) == ..._ARGUMENT) */
{
    static const char id[] = "usr_01hf7yat0004h1";
    uxid_decode_options opts;
    uxid_decoded out;

    assert(uxid_decode(id, strlen(id), NULL, NULL) == UXID_DECODE_INVALID_ARGUMENT);
    assert(uxid_decode(NULL, 4, NULL, &out) == UXID_DECODE_INVALID_ARGUMENT);
    assert(decoded_is_clear(&out));
    assert(uxid_decode(NULL, 0, NULL, &out) == UXID_DECODE_EMPTY);

    uxid_decode_options_init(&opts);
    opts.delimiter = NULL;
    assert(uxid_decode(id, strlen(id), &opts, &out) == UXID_DECODE_INVALID_ARGUMENT);
    opts.delimiter = "";
    assert(uxid_decode(id, strlen(id), &opts, &out) == UXID_DECODE_INVALID_ARGUMENT);

    for (size_t i = 0; i < sizeof opts.reserved / sizeof opts.reserved[0]; i++) {
        uxid_decode_options_init(&opts);
        opts.reserved[i] = &opts;
        assert(uxid_decode(id, strlen(id), &opts, &out) == UXID_DECODE_INVALID_ARGUMENT);
    }

    /* An argument refusal comes before a body refusal. */
    uxid_decode_options_init(&opts);
    opts.delimiter = "";
    assert(uxid_decode("", 0, &opts, &out) == UXID_DECODE_INVALID_ARGUMENT);
}

static void test_decode_reads_only_len(void) /* (sabotage: body_len + 1 -> uxid_decode(padded) == UXID_DECODE_OK) */
{
    /* A valid id followed by bytes that would change the answer if read. */
    static const char padded[] = "usr_01hf7yat0004h1zzzz";
    uxid_decoded out;
    assert(uxid_decode(padded, strlen("usr_01hf7yat0004h1"), NULL, &out) == UXID_DECODE_OK);
    assert(out.size == UXID_SIZE_S && out.time == CASE_TIME);

    /* An id with no terminating NUL, in a block of exactly its length. */
    static const char id[] = "cus_zt9vnyfd85bff0";
    char *copy = exact_copy(id, strlen(id));
    assert(uxid_decode(copy, strlen(id), NULL, &out) == UXID_DECODE_OK);
    assert(out.deterministic);
    assert(uxid_is_valid(copy, strlen(id), "_", "cus"));
    assert(uxid_is_deterministic(copy, strlen(id), "_"));
    free(copy);
}

static void test_delimiter_split(void) /* (sabotage: first delimiter -> uxid_decode(multi) == UXID_DECODE_OK) */
{
    uxid_decode_options opts;
    uxid_decode_options_init(&opts);
    uxid_decoded out;

    opts.delimiter = "::";
    static const char multi[] = "org::usr::01hf7yat0004h1";
    assert(uxid_decode(multi, strlen(multi), &opts, &out) == UXID_DECODE_OK);
    assert(out.prefix == multi && out.prefix_len == 8);
    assert(out.size == UXID_SIZE_S && out.time == CASE_TIME);

    /* The scan does not overlap matches: "___" under "__" is one match at 3, then "_" in the body. */
    opts.delimiter = "__";
    static const char overlap[] = "usr___01hf7yat0004h";
    assert(uxid_decode(overlap, strlen(overlap), &opts, &out) == UXID_DECODE_INVALID_CHARACTER);
    static const char clean[] = "usr__01hf7yat0004h1";
    assert(uxid_decode(clean, strlen(clean), &opts, &out) == UXID_DECODE_OK);
    assert(out.prefix_len == 3);

    /* Under another delimiter, "_" is a body byte, outside the alphabet. */
    opts.delimiter = "-";
    static const char underscore[] = "usr_01hf7yat0004h1";
    assert(uxid_decode(underscore, strlen(underscore), &opts, &out) == UXID_DECODE_INVALID_CHARACTER);
    assert(!uxid_is_valid(underscore, strlen(underscore), "-", NULL));
    assert(uxid_is_valid(underscore, strlen(underscore), "_", NULL));
}

static void test_is_valid(void) /* (sabotage: prefix bytes not compared -> !uxid_is_valid(id, len, "_", "cus")) */
{
    static const char id[] = "usr_01hf7yat0004hmasw9nf6yy093";
    size_t len = strlen(id);
    assert(uxid_is_valid(id, len, "_", NULL));
    assert(uxid_is_valid(id, len, "_", ""));
    assert(uxid_is_valid(id, len, "_", "usr"));
    assert(!uxid_is_valid(id, len, "_", "cus"));
    assert(!uxid_is_valid(id, len, "_", "us"));
    assert(!uxid_is_valid(id, len, "_", "usr_"));

    static const char bare[] = "01hf7yat0004hmasw9nf6yy093";
    assert(uxid_is_valid(bare, strlen(bare), "_", NULL));
    assert(!uxid_is_valid(bare, strlen(bare), "_", "usr"));

    static const char nested[] = "a_b_01hf7yat000001";
    assert(uxid_is_valid(nested, strlen(nested), "_", "a_b"));
    assert(!uxid_is_valid(nested, strlen(nested), "_", "a"));

    for (size_t i = 0; i < sizeof invalid_cases / sizeof invalid_cases[0]; i++) {
        assert(!uxid_is_valid(invalid_cases[i].input, strlen(invalid_cases[i].input), "_", NULL));
    }
    for (size_t i = 0; i < sizeof decode_cases / sizeof decode_cases[0]; i++) {
        assert(uxid_is_valid(decode_cases[i].input, strlen(decode_cases[i].input), "_", NULL));
    }

    /* Stricter than the reference: an alphabet body of no table length is not valid. */
    static const char eleven[] = "usr_01hf7yat000";
    assert(!uxid_is_valid(eleven, strlen(eleven), "_", NULL));

    assert(!uxid_is_valid(id, len, NULL, NULL));
    assert(!uxid_is_valid(id, len, "", NULL));
    assert(!uxid_is_valid(NULL, 3, "_", NULL));
    assert(!uxid_is_valid(NULL, 0, "_", NULL));
}

static void test_is_deterministic(void) /* (sabotage: marker check dropped -> !uxid_is_deterministic(standard, ...)) */
{
    static const char det[] = "usr_zcvt7epac0t1ebcsjfyf7cwz25";
    static const char upper[] = "usr_ZCVT7EPAC0T1EBCSJFYF7CWZ25";
    static const char standard[] = "usr_01hf7yat0004hmasw9nf6yy093";
    static const char bad_char[] = "usr_zcvt7epac0t1ebcsjfyf7cwzi5";
    static const char bad_len[] = "usr_zcvt7epac0t1ebcsjfyf7cwz2";
    assert(uxid_is_deterministic(det, strlen(det), "_"));
    assert(uxid_is_deterministic(upper, strlen(upper), "_"));
    assert(!uxid_is_deterministic(standard, strlen(standard), "_"));
    /* Stricter than the reference: a marker body that does not decode is not deterministic. */
    assert(!uxid_is_deterministic(bad_char, strlen(bad_char), "_"));
    assert(!uxid_is_deterministic(bad_len, strlen(bad_len), "_"));
    assert(!uxid_is_deterministic(det, strlen(det), NULL));
    assert(!uxid_is_deterministic(det, strlen(det), ""));
    assert(!uxid_is_deterministic(det, strlen(det), "-"));
    assert(!uxid_is_deterministic(NULL, 5, "_"));
    assert(!uxid_is_deterministic(NULL, 0, "_"));
}

/* Generates a cus id at a size and time form with a fixed time, and decodes it. */
static void round_trip(uxid_size size, bool compact, size_t width)
{
    static const uint8_t rand[10] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x10, 0x32};
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "cus";
    opts.size = size;
    opts.compact_time = compact;
    opts.has_time = true;
    opts.time = UINT64_C(1800000000123);
    opts.rand = rand;
    opts.rand_len = width;
    char id[64];
    assert(uxid_generate(id, sizeof id, &opts) == UXID_OK);
    struct fixed_clock clock = {UINT64_C(1800000000123), 0, 0};
    uxid_decoded out;
    assert(decode_at(id, &clock, &out) == UXID_DECODE_OK);
    assert(out.size == size);
    /* Compact xs and compact m share standard lengths and read as standard. */
    if (compact && (size == UXID_SIZE_XS || size == UXID_SIZE_M)) {
        assert(!out.compact);
        return;
    }
    assert(out.compact == compact);
    assert(out.time == (int64_t)opts.time);
}

static void test_round_trip(void) /* (sabotage: compact read 7 symbols -> out.time == (int64_t)opts.time) */
{
    static const uxid_size sizes[] = {UXID_SIZE_XS, UXID_SIZE_S, UXID_SIZE_M, UXID_SIZE_L, UXID_SIZE_XL};
    /* The random field width of each size, standard then compact. */
    static const size_t standard_widths[] = {0, 2, 5, 7, 10};
    static const size_t compact_widths[] = {1, 3, 6, 8, 10};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        round_trip(sizes[i], false, standard_widths[i]);
        round_trip(sizes[i], true, compact_widths[i]);
    }
}

int main(void)
{
    test_decode_options_init_defaults();
    test_decode_corpus_cases();
    test_decode_prefix_bytes();
    test_decode_invalid_cases();
    test_clock_window_start();
    test_clock_window_end();
    test_first_window_negative();
    test_clock_not_called();
    test_clock_refusals();
    test_system_clock();
    test_decode_invalid_arguments();
    test_decode_reads_only_len();
    test_delimiter_split();
    test_is_valid();
    test_is_deterministic();
    test_round_trip();
    return 0;
}
