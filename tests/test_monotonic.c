/*
 * uxid_monotonic_init, uxid_monotonic_next and uxid_monotonic_destroy. The
 * replayed vectors are cases of the uxid conformance corpus (monotonic.json at
 * the reference's v2.10.0 tag), each named by its case id. One test shares a
 * generator between threads.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uxid.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
/* The pthread types' own headers, which pthread.h includes, on macOS and glibc. */
#ifdef __has_include
#if __has_include(<sys/_pthread/_pthread_t.h>)
#include <sys/_pthread/_pthread_t.h>
#endif
#if __has_include(<bits/pthreadtypes.h>)
#include <bits/pthreadtypes.h>
#endif
#endif
#endif

#ifdef NDEBUG
#error "the tests assert with assert(); the test build never defines NDEBUG"
#endif

/* A buffer that holds any id these tests make. */
#define ID_CAP 64

/* A draw source that replays listed seeds and steps and counts its calls. */
struct replay {
    const uint64_t *seeds;
    size_t seed_count;
    size_t seeds_used;
    const uint64_t *steps;
    size_t step_count;
    size_t steps_used;
    int fail_seed;
    int fail_step;
};

static int replay_seed(void *ctx, uint8_t *out, size_t width)
{
    struct replay *r = ctx;
    if (r->fail_seed) {
        return r->fail_seed;
    }
    assert(r->seeds_used < r->seed_count);
    uint64_t value = r->seeds[r->seeds_used++];
    for (size_t i = width; i > 0; i--) {
        out[i - 1] = (uint8_t)(value & 0xFFU);
        value >>= 8U;
    }
    return 0;
}

static int replay_step(void *ctx, size_t width, uint64_t *step)
{
    struct replay *r = ctx;
    (void)width;
    if (r->fail_step) {
        return r->fail_step;
    }
    assert(r->steps_used < r->step_count);
    *step = r->steps[r->steps_used++];
    return 0;
}

/* A generator that replays r. */
static void init_replay(uxid_monotonic *gen, struct replay *r)
{
    uxid_draw_source draws = {.seed = replay_seed, .step = replay_step, .ctx = r};
    assert(uxid_monotonic_init(gen, &draws) == UXID_OK);
}

/* Options for a prefix, size and time form at a fixed time. */
static uxid_options at_time(const char *prefix, uxid_size size, bool compact, uint64_t time)
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = prefix;
    opts.size = size;
    opts.compact_time = compact;
    opts.has_time = true;
    opts.time = time;
    return opts;
}

/* The id uxid_generate() makes for opts with these fixed random bytes. */
static void fixed_id(char *out, uxid_options opts, uint64_t time, const uint8_t *rand, size_t len)
{
    opts.has_time = true;
    opts.time = time;
    opts.rand = rand;
    opts.rand_len = len;
    assert(uxid_generate(out, ID_CAP, &opts) == UXID_OK);
}

struct monotonic_case {
    const char *id;
    const char *prefix;
    uxid_size size;
    bool compact;
    uint64_t times[6];
    size_t count;
    uint64_t seeds[2];
    size_t seed_count;
    uint64_t steps[5];
    size_t step_count;
    const char *expected[6];
};

#define T0 UINT64_C(1700000000000)

/* Each row is the corpus case monotonic/<id>; seeds are the initial and any overflow seed. */
static const struct monotonic_case monotonic_cases[] = {
    {
        "xl-same-millisecond",
        "usr",
        UXID_SIZE_XL,
        false,
        {T0, T0, T0, T0},
        4,
        {123456789},
        1,
        {1, UINT64_C(1099511627776), 42},
        3,
        {
            "usr_01hf7yat0000000000003nqk8n",
            "usr_01hf7yat0000000000003nqk8p",
            "usr_01hf7yat0000000001003nqk8p",
            "usr_01hf7yat0000000001003nqka0",
        },
    },
    {
        "m-clock-moves-back",
        "usr",
        UXID_SIZE_M,
        false,
        {T0, T0 - 1, T0 - 10, T0},
        4,
        {1000},
        1,
        {5, 1048576, 7},
        3,
        {
            "usr_01hf7yat00000000z8",
            "usr_01hf7yat00000000zd",
            "usr_01hf7yat00000100zd",
            "usr_01hf7yat00000100zm",
        },
    },
    {
        "s-overflow-bumps-time",
        "usr",
        UXID_SIZE_S,
        false,
        {T0, T0, T0, T0, T0, T0 + 1},
        6,
        {65000, 1234},
        2,
        {200, 250, 100, 7, 3},
        5,
        {
            "usr_01hf7yat00zqm0",
            "usr_01hf7yat00ztr0",
            "usr_01hf7yat00zyn0",
            "usr_01hf7yat010k90",
            "usr_01hf7yat010kc1",
            "usr_01hf7yat010ke0",
        },
    },
    {
        "xs-compact-overflow-clock-back",
        "usr",
        UXID_SIZE_XS,
        true,
        {T0, T0, T0 - 1},
        3,
        {250, 9},
        2,
        {5, 16},
        2,
        {
            "usr_hf7yat00z2",
            "usr_hf7yat00z7",
            "usr_hf7yat0111",
        },
    },
    {
        "s-compact-no-prefix-largest-step",
        NULL,
        UXID_SIZE_S,
        true,
        {T0, T0},
        2,
        {0},
        1,
        {4096},
        1,
        {
            "hf7yat0000000",
            "hf7yat0000800",
        },
    },
};

static void test_corpus_cases(void) /* (sabotage: add_step never overflows -> strcmp(out, c->expected[j]) == 0) */
{
    for (size_t i = 0; i < sizeof monotonic_cases / sizeof monotonic_cases[0]; i++) {
        const struct monotonic_case *c = &monotonic_cases[i];
        struct replay r = {
            .seeds = c->seeds,
            .seed_count = c->seed_count,
            .steps = c->steps,
            .step_count = c->step_count,
        };
        uxid_monotonic gen;
        init_replay(&gen, &r);
        for (size_t j = 0; j < c->count; j++) {
            uxid_options opts = at_time(c->prefix, c->size, c->compact, c->times[j]);
            char out[ID_CAP];
            assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
            assert(strcmp(out, c->expected[j]) == 0);
        }
        assert(r.seeds_used == c->seed_count);
        assert(r.steps_used == c->step_count);
        uxid_monotonic_destroy(&gen);
    }
}

static void test_xs_always_compact(void) /* (sabotage: xs reads compact_time -> uxid_monotonic_next() == UXID_OK) */
{
    static const uint64_t seeds[] = {250};
    struct replay r = {.seeds = seeds, .seed_count = 1};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    uxid_options opts = at_time("usr", UXID_SIZE_XS, false, T0);
    char out[ID_CAP];
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_hf7yat00z2") == 0);
    uxid_monotonic_destroy(&gen);
}

static void test_state_per_key(void) /* (sabotage: find skips prefix bytes -> strcmp(out, expected) == 0) */
{
    static const uint64_t seeds[] = {1, 2, 3};
    static const uint64_t steps[] = {7};
    struct replay r = {.seeds = seeds, .seed_count = 3, .steps = steps, .step_count = 1};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    char out[ID_CAP];
    char expected[ID_CAP];

    uxid_options usr = at_time("usr", UXID_SIZE_S, false, T0);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &usr) == UXID_OK);
    fixed_id(expected, usr, T0, (const uint8_t[]){0, 1}, 2);
    assert(strcmp(out, expected) == 0);

    /* Another prefix in the same millisecond has no state: a fresh seed. */
    uxid_options cus = at_time("cus", UXID_SIZE_S, false, T0);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &cus) == UXID_OK);
    fixed_id(expected, cus, T0, (const uint8_t[]){0, 2}, 2);
    assert(strcmp(out, expected) == 0);

    /* Another random width under the same prefix has no state either. */
    uxid_options wide = at_time("usr", UXID_SIZE_S, false, T0);
    wide.has_rand_size = true;
    wide.rand_size = 3;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &wide) == UXID_OK);
    fixed_id(expected, wide, T0, (const uint8_t[]){0, 0, 3}, 3);
    assert(strcmp(out, expected) == 0);
    assert(r.seeds_used == 3);

    /* The first pair steps on from its own value, 1 + 7. */
    assert(uxid_monotonic_next(&gen, out, sizeof out, &usr) == UXID_OK);
    fixed_id(expected, usr, T0, (const uint8_t[]){0, 8}, 2);
    assert(strcmp(out, expected) == 0);
    assert(r.steps_used == 1);
    uxid_monotonic_destroy(&gen);
}

/* A byte source that hands out bytes from a script and counts what it was asked for. */
struct script {
    const uint8_t *bytes;
    size_t len;
    size_t used;
    int calls;
    size_t asked[8];
};

static int script_draw(void *ctx, uint8_t *out, size_t len)
{
    struct script *s = ctx;
    assert(s->calls < 8 && s->used + len <= s->len);
    s->asked[s->calls++] = len;
    memcpy(out, s->bytes + s->used, len);
    s->used += len;
    return 0;
}

static void test_default_draws_mask_step(void) /* (sabotage: step % (max - 1) -> strcmp(out, expected) == 0) */
{
    /* s compact: w = 3; a seed of 3 bytes, then steps of 2 bytes masked to 12 bits. */
    static const uint8_t bytes[] = {0x00, 0x00, 0x05, 0xFF, 0xFF, 0x10, 0x02};
    struct script s = {.bytes = bytes, .len = sizeof bytes};
    uxid_monotonic gen;
    assert(uxid_monotonic_init(&gen, NULL) == UXID_OK);
    uxid_options opts = at_time("usr", UXID_SIZE_S, true, T0);
    opts.byte_source = script_draw;
    opts.byte_source_ctx = &s;
    char out[ID_CAP];
    char expected[ID_CAP];

    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    fixed_id(expected, opts, T0, (const uint8_t[]){0x00, 0x00, 0x05}, 3);
    assert(strcmp(out, expected) == 0);

    /* 0xFFFF masked to 12 bits is 4095, plus one: the largest step, 4096. */
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    fixed_id(expected, opts, T0, (const uint8_t[]){0x00, 0x10, 0x05}, 3);
    assert(strcmp(out, expected) == 0);

    /* 0x1002 masked to 12 bits is 2, plus one. */
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    fixed_id(expected, opts, T0, (const uint8_t[]){0x00, 0x10, 0x08}, 3);
    assert(strcmp(out, expected) == 0);

    assert(s.calls == 3 && s.asked[0] == 3 && s.asked[1] == 2 && s.asked[2] == 2);
    uxid_monotonic_destroy(&gen);
}

static void test_refusals(void) /* (sabotage: rand first -> uxid_monotonic_next() == UXID_ERR_FROM_WITH_MONOTONIC) */
{
    static const uint64_t seeds[] = {1};
    static const uint64_t steps[] = {2};
    struct replay r = {.seeds = seeds, .seed_count = 1, .steps = steps, .step_count = 1};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    char out[ID_CAP];
    char expected[ID_CAP];
    uxid_options base = at_time("usr", UXID_SIZE_S, false, T0);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &base) == UXID_OK);

    static const uint8_t fixed[2] = {0, 0};
    uxid_options opts = base;
    opts.from = "alice@example.com";
    opts.from_len = strlen(opts.from);
    opts.rand = fixed;
    opts.rand_len = sizeof fixed;
    memcpy(out, "x", 2);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_FROM_WITH_MONOTONIC);
    assert(out[0] == '\0');

    opts = base;
    opts.rand = fixed;
    opts.rand_len = sizeof fixed;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_RAND_WITH_MONOTONIC);

    opts = base;
    opts.has_rand_size = true;
    opts.rand_size = 11;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_RAND_SIZE_TOO_LARGE);

    opts.rand_size = 0;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    opts = base;
    opts.time = UINT64_C(1) << 48U;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    opts = base;
    opts.delimiter = "";
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);

    /* "usr_" and 14 body characters need 19 bytes with the NUL. */
    memcpy(out, "x", 2);
    assert(uxid_monotonic_next(&gen, out, 18, &base) == UXID_ERR_BUFFER_TOO_SMALL);
    assert(out[0] == '\0');
    assert(uxid_monotonic_next(NULL, out, sizeof out, &base) == UXID_ERR_INVALID_ARGUMENT);
    assert(uxid_monotonic_next(&gen, NULL, sizeof out, &base) == UXID_ERR_INVALID_ARGUMENT);
    assert(r.seeds_used == 1 && r.steps_used == 0);

    /* Every refusal left the state as it was: the next id steps on from the seed, 1 + 2. */
    assert(uxid_monotonic_next(&gen, out, 19, &base) == UXID_OK);
    fixed_id(expected, base, T0, (const uint8_t[]){0, 3}, 2);
    assert(strcmp(out, expected) == 0);
    uxid_monotonic_destroy(&gen);
}

static void test_argument_before_from(void) /* (sabotage: from read first -> next() == UXID_ERR_INVALID_ARGUMENT) */
{
    /* An option uxid_generate() refuses as an argument is refused as one before from is read. */
    static const uint64_t seeds[] = {1};
    struct replay r = {.seeds = seeds, .seed_count = 1};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    char out[ID_CAP];
    uxid_options opts = at_time("usr", UXID_SIZE_S, false, T0);
    opts.from = "alice@example.com";
    opts.from_len = strlen(opts.from);
    opts.delimiter = "";
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    opts.delimiter = NULL;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    opts.delimiter = "_";
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_FROM_WITH_MONOTONIC);
    assert(r.seeds_used == 0);
    uxid_monotonic_destroy(&gen);
}

/* The last millisecond whose compact time is not refused: the low 40 bits at 31 x 2^35 - 1. */
#define LAST_COMPACT UINT64_C(1065151889407)

static void test_refusal_keeps_state(void) /* (sabotage: state saved first -> uxid_monotonic_next() == UXID_OK) */
{
    static const uint64_t seeds[] = {250, 9};
    static const uint64_t steps[] = {16, 5};
    struct replay r = {.seeds = seeds, .seed_count = 2, .steps = steps, .step_count = 2};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    uxid_options opts = at_time("usr", UXID_SIZE_XS, true, LAST_COMPACT);
    char out[ID_CAP];
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_yzzzzzzzz2") == 0);

    /* 250 + 16 overflows the byte, so the id would be at T + 1, a compact time the format refuses. */
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_COMPACT_TIME_OUT_OF_RANGE);
    assert(out[0] == '\0');
    assert(r.seeds_used == 2 && r.steps_used == 1);

    /* The state is still T and 250: a step of 5 gives 255 at T. */
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_yzzzzzzzz7") == 0);
    uxid_monotonic_destroy(&gen);
}

static void test_draw_errors(void) /* (sabotage: no step range -> uxid_monotonic_next() == UXID_ERR_RANDOM_SOURCE) */
{
    static const uint64_t seeds[] = {7};
    static const uint64_t steps[] = {0, 257, 256};
    struct replay r = {.seeds = seeds, .seed_count = 1, .steps = steps, .step_count = 3};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    uxid_options opts = at_time("usr", UXID_SIZE_S, false, T0);
    char out[ID_CAP];
    char expected[ID_CAP];

    r.fail_seed = 3;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_RANDOM_SOURCE);
    r.fail_seed = 0;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);

    /* A step of 0, or above 2^8 for a 2-byte field, is refused; 2^8 is the largest. */
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_RANDOM_SOURCE);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_RANDOM_SOURCE);
    r.fail_step = -1;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_RANDOM_SOURCE);
    r.fail_step = 0;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    fixed_id(expected, opts, T0, (const uint8_t[]){1, 7}, 2);
    assert(strcmp(out, expected) == 0);
    uxid_monotonic_destroy(&gen);
}

static void test_max_ms(void) /* (sabotage: max time unchecked -> uxid_monotonic_next() == UXID_ERR_INVALID_ARGUMENT) */
{
    static const uint64_t seeds[] = {0xFFFF, 0};
    static const uint64_t steps[] = {1};
    struct replay r = {.seeds = seeds, .seed_count = 2, .steps = steps, .step_count = 1};
    uxid_monotonic gen;
    init_replay(&gen, &r);
    uxid_options opts = at_time(NULL, UXID_SIZE_S, false, (UINT64_C(1) << 48U) - 1U);
    char out[ID_CAP];
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "7zzzzzzzzzzzz1") == 0);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_ERR_INVALID_ARGUMENT);
    assert(r.seeds_used == 1);
    uxid_monotonic_destroy(&gen);
}

static void test_init_args(void) /* (sabotage: reserved unread -> uxid_monotonic_init() == UXID_ERR_INVALID_ARGUMENT) */
{
    uxid_monotonic gen;
    assert(uxid_monotonic_init(NULL, NULL) == UXID_ERR_INVALID_ARGUMENT);
    uxid_draw_source draws = {.seed = replay_seed, .step = NULL};
    assert(uxid_monotonic_init(&gen, &draws) == UXID_ERR_INVALID_ARGUMENT);
    draws = (uxid_draw_source){.seed = NULL, .step = replay_step};
    assert(uxid_monotonic_init(&gen, &draws) == UXID_ERR_INVALID_ARGUMENT);
    int slot = 0;
    draws = (uxid_draw_source){.seed = replay_seed, .step = replay_step};
    draws.reserved[1] = &slot;
    assert(uxid_monotonic_init(&gen, &draws) == UXID_ERR_INVALID_ARGUMENT);
    uxid_monotonic_destroy(NULL);
}

/* A draw source whose n-th seed is n, from 1, and whose steps are all 1. */
static int counting_seed(void *ctx, uint8_t *out, size_t width)
{
    size_t *seeds = ctx;
    memset(out, 0, width);
    out[width - 1] = (uint8_t)++*seeds;
    return 0;
}

static int counting_step(void *ctx, size_t width, uint64_t *step)
{
    (void)ctx;
    (void)width;
    *step = 1;
    return 0;
}

static void test_many_prefixes(void) /* (sabotage: find compares 1 prefix byte -> seeds == 40) */
{
    size_t seeds = 0;
    uxid_draw_source draws = {.seed = counting_seed, .step = counting_step, .ctx = &seeds};
    uxid_monotonic gen;
    assert(uxid_monotonic_init(&gen, &draws) == UXID_OK);
    char prefix[40][4];
    for (size_t i = 0; i < 40; i++) {
        prefix[i][0] = 'p';
        prefix[i][1] = (char)('a' + (i / 26));
        prefix[i][2] = (char)('a' + (i % 26));
        prefix[i][3] = '\0';
    }
    char out[ID_CAP];
    char expected[ID_CAP];
    /* Forty prefixes, each its own state: forty seeds, then each steps on from its own. */
    for (int round = 0; round < 2; round++) {
        for (size_t i = 0; i < 40; i++) {
            uxid_options opts = at_time(prefix[i], UXID_SIZE_M, false, T0);
            assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
            uint8_t value[5] = {0, 0, 0, 0, (uint8_t)(i + 1 + (size_t)round)};
            fixed_id(expected, opts, T0, value, sizeof value);
            assert(strcmp(out, expected) == 0);
        }
        assert(seeds == 40);
    }
    uxid_monotonic_destroy(&gen);
    /* A destroyed generator is initialized again and starts with no state. */
    assert(uxid_monotonic_init(&gen, &draws) == UXID_OK);
    uxid_options opts = at_time(prefix[0], UXID_SIZE_M, false, T0);
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    assert(seeds == 41);
    uxid_monotonic_destroy(&gen);
}

static void test_defaults_and_system_clock(void) /* (sabotage: clock read as 0 -> decoded.time >= (int64_t)before) */
{
    uxid_monotonic gen;
    assert(uxid_monotonic_init(&gen, NULL) == UXID_OK);
    struct timespec ts;
    assert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
    uint64_t before = ((uint64_t)ts.tv_sec * 1000U) + ((uint64_t)ts.tv_nsec / 1000000U);
    char out[ID_CAP];
    char last[ID_CAP] = "";
    for (int i = 0; i < 100; i++) {
        assert(uxid_monotonic_next(&gen, out, sizeof out, NULL) == UXID_OK);
        assert(strlen(out) == 26);
        assert(strcmp(out, last) > 0);
        memcpy(last, out, strlen(out) + 1);
    }
    uxid_decoded decoded;
    assert(uxid_decode(out, strlen(out), NULL, &decoded) == UXID_DECODE_OK);
    assert(decoded.size == UXID_SIZE_XL && !decoded.compact && decoded.has_time);
    assert(decoded.time >= (int64_t)before);

    /* Upper case and another delimiter are written as uxid_generate() writes them. */
    uxid_options opts;
    uxid_options_init(&opts);
    opts.prefix = "cus";
    opts.delimiter = "--";
    opts.letter_case = UXID_CASE_UPPER;
    assert(uxid_monotonic_next(&gen, out, sizeof out, &opts) == UXID_OK);
    assert(strncmp(out, "cus--", 5) == 0 && strlen(out) == 31);
    for (size_t i = 5; i < 31; i++) {
        assert(out[i] < 'a' || out[i] > 'z');
    }
    uxid_monotonic_destroy(&gen);
}

/* The threaded test: each thread makes ids from one shared generator at one fixed time. */
#define THREADS 8
#define PER_THREAD 2000

struct worker {
    uxid_monotonic *gen;
    int index;
    char (*ids)[ID_CAP];
};

static void work(struct worker *w)
{
    char own[4] = {'q', (char)('a' + w->index), '\0', '\0'};
    char last_own[ID_CAP] = "";
    for (int i = 0; i < PER_THREAD; i++) {
        uxid_options opts = at_time("usr", UXID_SIZE_XL, false, T0);
        assert(uxid_monotonic_next(w->gen, w->ids[i], ID_CAP, &opts) == UXID_OK);
        /* A prefix of this thread's own, so the entries grow while other threads use them. */
        uxid_options mine = at_time(own, UXID_SIZE_XL, false, T0);
        char out[ID_CAP];
        assert(uxid_monotonic_next(w->gen, out, sizeof out, &mine) == UXID_OK);
        assert(strcmp(out, last_own) > 0);
        memcpy(last_own, out, strlen(out) + 1);
    }
}

#ifdef _WIN32
static DWORD WINAPI work_thread(LPVOID arg)
{
    work(arg);
    return 0;
}
#else
static void *work_thread(void *arg)
{
    work(arg);
    return NULL;
}
#endif

static int compare_ids(const void *a, const void *b)
{
    return strcmp(a, b);
}

static void test_threads_strictly_increase(void) /* (sabotage: lock body removed -> strcmp(out, last_own) > 0) */
{
    uxid_monotonic gen;
    assert(uxid_monotonic_init(&gen, NULL) == UXID_OK);
    char (*all)[ID_CAP] = calloc((size_t)THREADS * PER_THREAD, ID_CAP);
    assert(all != NULL);
    struct worker workers[THREADS];
#ifdef _WIN32
    HANDLE threads[THREADS];
#else
    pthread_t threads[THREADS];
#endif
    for (int t = 0; t < THREADS; t++) {
        workers[t] = (struct worker){.gen = &gen, .index = t, .ids = all + ((size_t)t * PER_THREAD)};
#ifdef _WIN32
        threads[t] = CreateThread(NULL, 0, work_thread, &workers[t], 0, NULL);
        assert(threads[t] != NULL);
#else
        assert(pthread_create(&threads[t], NULL, work_thread, &workers[t]) == 0);
#endif
    }
    for (int t = 0; t < THREADS; t++) {
#ifdef _WIN32
        assert(WaitForSingleObject(threads[t], INFINITE) == WAIT_OBJECT_0);
        assert(CloseHandle(threads[t]));
#else
        assert(pthread_join(threads[t], NULL) == 0);
#endif
    }

    /* Each thread's ids increase in its own call order. */
    for (int t = 0; t < THREADS; t++) {
        char (*ids)[ID_CAP] = workers[t].ids;
        for (int i = 1; i < PER_THREAD; i++) {
            assert(strcmp(ids[i - 1], ids[i]) < 0);
        }
    }
    /* And no two calls from any threads gave the same id: the lock ordered them all. */
    qsort(all, (size_t)THREADS * PER_THREAD, ID_CAP, compare_ids);
    for (size_t i = 1; i < (size_t)THREADS * PER_THREAD; i++) {
        assert(strcmp(all[i - 1], all[i]) < 0);
    }
    free(all);
    uxid_monotonic_destroy(&gen);
}

int main(void)
{
    test_corpus_cases();
    test_xs_always_compact();
    test_state_per_key();
    test_default_draws_mask_step();
    test_refusals();
    test_argument_before_from();
    test_refusal_keeps_state();
    test_draw_errors();
    test_max_ms();
    test_init_args();
    test_many_prefixes();
    test_defaults_and_system_clock();
    test_threads_strictly_increase();
    return 0;
}
