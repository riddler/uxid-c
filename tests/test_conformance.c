/*
 * The corpus runner: every case of every case file the vendored conformance
 * corpus's manifest lists (conformance/, from the reference's v2.10.0 tag),
 * run by the corpus README's "Running the cases", with no skip list. A case
 * file whose kind this runner does not know fails the run, and so does a case
 * carrying a key its kind does not define, at any level, so a later corpus
 * cannot be half read. A null prefix in a case is no prefix. The corpus hash
 * and the case counts are checked by scripts/check_corpus.cmake; this runner
 * checks each file's line count too, since it splits the lines itself.
 *
 * JSON is read through the vendored cJSON in tests/vendor/cJSON, which
 * only this test compiles. CMake defines UXID_CORPUS_DIR as the corpus
 * directory.
 */
#include <assert.h>
#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uxid.h>

#ifdef NDEBUG
#error "the tests assert with assert(); the test build never defines NDEBUG"
#endif

#ifndef UXID_CORPUS_DIR
#error "the build defines UXID_CORPUS_DIR as the conformance corpus directory"
#endif

/* A buffer that holds any id the corpus asks for. */
#define ID_CAP 128

/* The largest integer every corpus number is at most: 2^53 - 1. */
#define JSON_INT_MAX UINT64_C(9007199254740991)

/* The largest time a time field holds: 2^48 - 1. */
#define TIME_MAX UINT64_C(281474976710655)

/* The time of most corpus cases. */
#define CASE_TIME UINT64_C(1700000000000)

/*
 * The decoder clocks every decode case runs at: the ends of the window the
 * corpus README gives for its compact cases, from 1699913600000 ms inclusive
 * to 2164577117183 ms exclusive, and the cases' own time.
 */
static const uint64_t decode_clocks[] = {UINT64_C(1699913600000), CASE_TIME, UINT64_C(2164577117182)};

/* Prints why the case id failed and answers false. */
static bool fail(const char *id, const char *why)
{
    (void)fprintf(stderr, "corpus case %s: %s\n", id != NULL ? id : "(no id)", why);
    return false;
}

/*
 * True when obj is an object whose keys are all among the count names at
 * keys, none twice, and the first required of them are all present.
 */
static bool keys_are(const cJSON *obj, const char *const *keys, size_t count, size_t required)
{
    if (cJSON_IsObject(obj) == 0) {
        return false;
    }
    bool seen[16] = {false};
    assert(count <= sizeof seen / sizeof seen[0] && required <= count);
    for (const cJSON *child = obj->child; child != NULL; child = child->next) {
        size_t at = 0;
        while (at < count && strcmp(child->string, keys[at]) != 0) {
            at++;
        }
        if (at == count || seen[at]) {
            return false;
        }
        seen[at] = true;
    }
    for (size_t i = 0; i < required; i++) {
        if (!seen[i]) {
            return false;
        }
    }
    return true;
}

/* The string at key in obj, or NULL when it is absent or not a string. */
static const char *string_at(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) != 0 ? item->valuestring : NULL;
}

/* Reads a JSON integer from 0 to max (at most 2^53 - 1) into *out. */
static bool read_uint(const cJSON *item, uint64_t max, uint64_t *out)
{
    if (cJSON_IsNumber(item) == 0 || !(item->valuedouble >= 0.0) || item->valuedouble > (double)max) {
        return false;
    }
    uint64_t value = (uint64_t)item->valuedouble;
    if ((double)value != item->valuedouble || value > max) {
        return false;
    }
    *out = value;
    return true;
}

/* Reads a JSON boolean into *out. */
static bool read_bool(const cJSON *item, bool *out)
{
    if (cJSON_IsBool(item) == 0) {
        return false;
    }
    *out = cJSON_IsTrue(item) != 0;
    return true;
}

/*
 * Reads a prefix: a string, or null for no prefix, which is NULL. An empty
 * string stays the empty string, which the library also reads as no prefix.
 */
static bool read_prefix(const cJSON *item, const char **out)
{
    if (cJSON_IsNull(item) != 0) {
        *out = NULL;
        return true;
    }
    if (cJSON_IsString(item) != 0) {
        *out = item->valuestring;
        return true;
    }
    return false;
}

/* Reads a size name. */
static bool read_size(const cJSON *item, uxid_size *out)
{
    static const char *const names[] = {"xs", "s", "m", "l", "xl"};
    static const uxid_size sizes[] = {UXID_SIZE_XS, UXID_SIZE_S, UXID_SIZE_M, UXID_SIZE_L, UXID_SIZE_XL};
    if (cJSON_IsString(item) == 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(item->valuestring, names[i]) == 0) {
            *out = sizes[i];
            return true;
        }
    }
    return false;
}

/* Reads a letter case name. */
static bool read_case(const cJSON *item, uxid_case *out)
{
    if (cJSON_IsString(item) == 0) {
        return false;
    }
    if (strcmp(item->valuestring, "lower") == 0) {
        *out = UXID_CASE_LOWER;
        return true;
    }
    if (strcmp(item->valuestring, "upper") == 0) {
        *out = UXID_CASE_UPPER;
        return true;
    }
    return false;
}

/* The value of one lowercase hex digit, or -1. */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* Reads lowercase hex into out, at most cap bytes, setting *len. */
static bool read_hex(const cJSON *item, uint8_t *out, size_t cap, size_t *len)
{
    if (cJSON_IsString(item) == 0) {
        return false;
    }
    const char *hex = item->valuestring;
    size_t chars = strlen(hex);
    if (chars % 2U != 0 || chars / 2U > cap) {
        return false;
    }
    for (size_t i = 0; i < chars / 2U; i++) {
        int high = hex_digit(hex[2U * i]);
        int low = hex_digit(hex[(2U * i) + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = (uint8_t)((unsigned)high << 4U | (unsigned)low);
    }
    *len = chars / 2U;
    return true;
}

/* A byte source that always fails: a case that draws a byte is answered with random_source. */
static int no_bytes(void *ctx, uint8_t *out, size_t len)
{
    (void)ctx;
    memset(out, 0, len);
    return 1;
}

/* Options for a case, with a byte source that fails so that no case draws. */
static uxid_options case_options(void)
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.byte_source = no_bytes;
    return opts;
}

static bool run_encode(const char *id, const cJSON *c)
{
    static const char *const top[] = {"id", "input", "expected"};
    static const char *const keys[] = {"prefix", "size", "compact", "case", "time", "rand"};
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(c, "input");
    const char *expected = string_at(c, "expected");
    if (!keys_are(c, top, 3, 3) || !keys_are(input, keys, 6, 6) || expected == NULL) {
        return fail(id, "a key the encode kind does not define, or one it requires is missing");
    }
    uxid_options opts = case_options();
    uint8_t rand[16];
    if (!read_prefix(cJSON_GetObjectItemCaseSensitive(input, "prefix"), &opts.prefix) ||
        !read_size(cJSON_GetObjectItemCaseSensitive(input, "size"), &opts.size) ||
        !read_bool(cJSON_GetObjectItemCaseSensitive(input, "compact"), &opts.compact_time) ||
        !read_case(cJSON_GetObjectItemCaseSensitive(input, "case"), &opts.letter_case) ||
        !read_uint(cJSON_GetObjectItemCaseSensitive(input, "time"), TIME_MAX, &opts.time) ||
        !read_hex(cJSON_GetObjectItemCaseSensitive(input, "rand"), rand, sizeof rand, &opts.rand_len)) {
        return fail(id, "an input value of the wrong type or out of range");
    }
    opts.has_time = true;
    opts.rand = rand;
    char out[ID_CAP];
    uxid_status status = uxid_generate(out, sizeof out, &opts);
    if (status != UXID_OK) {
        return fail(id, uxid_status_name(status));
    }
    if (strcmp(out, expected) != 0) {
        return fail(id, "the id differs from expected");
    }
    return true;
}

static bool run_deterministic(const char *id, const cJSON *c)
{
    static const char *const top[] = {"id", "input", "expected"};
    static const char *const keys[] = {"prefix", "from", "size", "case"};
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(c, "input");
    const char *expected = string_at(c, "expected");
    if (!keys_are(c, top, 3, 3) || !keys_are(input, keys, 4, 4) || expected == NULL) {
        return fail(id, "a key the deterministic kind does not define, or one it requires is missing");
    }
    uxid_options opts = case_options();
    opts.from = string_at(input, "from");
    if (!read_prefix(cJSON_GetObjectItemCaseSensitive(input, "prefix"), &opts.prefix) || opts.from == NULL ||
        !read_size(cJSON_GetObjectItemCaseSensitive(input, "size"), &opts.size) ||
        !read_case(cJSON_GetObjectItemCaseSensitive(input, "case"), &opts.letter_case)) {
        return fail(id, "an input value of the wrong type");
    }
    opts.from_len = strlen(opts.from);
    char out[ID_CAP];
    uxid_status status = uxid_generate(out, sizeof out, &opts);
    if (status != UXID_OK) {
        return fail(id, uxid_status_name(status));
    }
    if (strcmp(out, expected) != 0) {
        return fail(id, "the id differs from expected");
    }
    return true;
}

/* A copy of s, without its NUL, in a block of exactly its length, so a read past it is caught. */
static char *exact_copy(const char *s, size_t len)
{
    char *copy = malloc(len == 0 ? 1U : len);
    assert(copy != NULL);
    memcpy(copy, s, len);
    return copy;
}

static int fixed_clock(void *ctx, uint64_t *now_ms)
{
    *now_ms = *(const uint64_t *)ctx;
    return 0;
}

/* The name the corpus gives a decode refusal; this library's own status has none. */
static const char *decode_status_name(uxid_decode_status status)
{
    switch (status) {
    case UXID_DECODE_OK:
        return "ok";
    case UXID_DECODE_EMPTY:
        return "empty";
    case UXID_DECODE_INVALID_LENGTH:
        return "invalid_length";
    case UXID_DECODE_INVALID_CHARACTER:
        return "invalid_character";
    case UXID_DECODE_INVALID_ARGUMENT:
        return "invalid_argument";
    }
    return "unknown";
}

/* Decodes len bytes at s with the clock at now. */
static uxid_decode_status decode_at(const char *s, size_t len, uint64_t now, uxid_decoded *out)
{
    uxid_decode_options opts;
    uxid_decode_options_init(&opts);
    opts.clock = fixed_clock;
    opts.clock_ctx = &now;
    return uxid_decode(s, len, &opts, out);
}

/* True when the decoded answer is the expected object's at one clock. */
static bool decoded_matches(const char *id, const char *input, const cJSON *expected, uint64_t now)
{
    const char *prefix = NULL;
    uxid_size size = UXID_SIZE_XL;
    bool compact = false;
    bool deterministic = false;
    uint64_t time = 0;
    const cJSON *time_item = cJSON_GetObjectItemCaseSensitive(expected, "time");
    bool has_time = cJSON_IsNull(time_item) == 0;
    if (!read_prefix(cJSON_GetObjectItemCaseSensitive(expected, "prefix"), &prefix) ||
        !read_size(cJSON_GetObjectItemCaseSensitive(expected, "size"), &size) ||
        !read_bool(cJSON_GetObjectItemCaseSensitive(expected, "compact"), &compact) ||
        !read_bool(cJSON_GetObjectItemCaseSensitive(expected, "deterministic"), &deterministic) ||
        (has_time && !read_uint(time_item, TIME_MAX, &time))) {
        return fail(id, "an expected value of the wrong type or out of range");
    }
    size_t len = strlen(input);
    char *copy = exact_copy(input, len);
    uxid_decoded out;
    uxid_decode_status status = decode_at(copy, len, now, &out);
    bool valid = uxid_is_valid(copy, len, "_", NULL);
    bool marked = uxid_is_deterministic(copy, len, "_");
    /* out.prefix points into copy, so it is compared before copy is freed. */
    bool prefix_ok = false;
    if (prefix == NULL) {
        prefix_ok = out.prefix == NULL;
    } else if (out.prefix != NULL && out.prefix_len == strlen(prefix) &&
               memcmp(out.prefix, prefix, out.prefix_len) == 0) {
        prefix_ok = true;
    }
    free(copy);
    if (status != UXID_DECODE_OK) {
        return fail(id, decode_status_name(status));
    }
    if (!prefix_ok) {
        return fail(id, "the prefix differs from expected");
    }
    if (out.size != size || out.compact != compact || out.deterministic != deterministic || out.has_time != has_time) {
        return fail(id, "the size, compact flag, deterministic flag or presence of a time differs from expected");
    }
    if (has_time && out.time != (int64_t)time) {
        return fail(id, "the time differs from expected");
    }
    if (!valid || marked != deterministic) {
        return fail(id, "uxid_is_valid() or uxid_is_deterministic() disagrees with the decoded answer");
    }
    return true;
}

static bool run_decode(const char *id, const cJSON *c)
{
    static const char *const top[] = {"id", "input", "expected"};
    static const char *const keys[] = {"prefix", "size", "compact", "deterministic", "time"};
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(c, "expected");
    const char *input = string_at(c, "input");
    if (!keys_are(c, top, 3, 3) || !keys_are(expected, keys, 5, 5) || input == NULL) {
        return fail(id, "a key the decode kind does not define, or one it requires is missing");
    }
    bool ok = true;
    for (size_t i = 0; i < sizeof decode_clocks / sizeof decode_clocks[0]; i++) {
        if (!decoded_matches(id, input, expected, decode_clocks[i])) {
            ok = false;
        }
    }
    return ok;
}

static bool refused_decode(const char *id, const char *input, const char *expected_error)
{
    size_t len = strlen(input);
    char *copy = exact_copy(input, len);
    uxid_decoded out;
    uxid_decode_status status = decode_at(copy, len, CASE_TIME, &out);
    bool valid = uxid_is_valid(copy, len, "_", NULL);
    free(copy);
    if (strcmp(decode_status_name(status), expected_error) != 0) {
        return fail(id, decode_status_name(status));
    }
    if (valid) {
        return fail(id, "uxid_is_valid() answers true");
    }
    return true;
}

/* A draw source that fails: a refused monotonic request draws nothing. */
static int no_seed(void *ctx, uint8_t *out, size_t width)
{
    (void)ctx;
    memset(out, 0, width);
    return 1;
}

static int no_step(void *ctx, size_t width, uint64_t *step)
{
    (void)ctx;
    (void)width;
    *step = 0;
    return 1;
}

/* Reads the shape options of a refused generate request; an absent option keeps its default. */
static bool read_generate_shape(const cJSON *generate, uxid_options *opts)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(generate, "prefix");
    if (item != NULL && !read_prefix(item, &opts->prefix)) {
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "size");
    if (item != NULL && !read_size(item, &opts->size)) {
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "compact");
    if (item != NULL && !read_bool(item, &opts->compact_time)) {
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "case");
    if (item != NULL && !read_case(item, &opts->letter_case)) {
        return false;
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "time");
    if (item != NULL) {
        if (!read_uint(item, TIME_MAX, &opts->time)) {
            return false;
        }
        opts->has_time = true;
    }
    return true;
}

/* Reads the random field, from and monotonic options of a refused generate request. */
static bool read_generate_fields(const cJSON *generate, uxid_options *opts, uint8_t *rand, size_t rand_cap,
                                 bool *monotonic)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(generate, "rand");
    if (item != NULL) {
        if (!read_hex(item, rand, rand_cap, &opts->rand_len)) {
            return false;
        }
        opts->rand = rand;
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "rand_size");
    if (item != NULL) {
        uint64_t rand_size = 0;
        if (!read_uint(item, JSON_INT_MAX, &rand_size)) {
            return false;
        }
        opts->has_rand_size = true;
        opts->rand_size = (size_t)rand_size;
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "from");
    if (item != NULL) {
        if (cJSON_IsString(item) == 0) {
            return false;
        }
        opts->from = item->valuestring;
        opts->from_len = strlen(opts->from);
    }
    item = cJSON_GetObjectItemCaseSensitive(generate, "monotonic");
    if (item != NULL && !read_bool(item, monotonic)) {
        return false;
    }
    return true;
}

static bool refused_generate(const char *id, const cJSON *generate, const char *expected_error)
{
    static const char *const keys[] = {
        "prefix", "size", "compact", "case", "time", "rand", "rand_size", "from", "monotonic",
    };
    if (!keys_are(generate, keys, 9, 0)) {
        return fail(id, "a generate key the invalid kind does not define");
    }
    uxid_options opts = case_options();
    uint8_t rand[64];
    bool monotonic = false;
    if (!read_generate_shape(generate, &opts) ||
        !read_generate_fields(generate, &opts, rand, sizeof rand, &monotonic)) {
        return fail(id, "a generate value of the wrong type or out of range");
    }
    char out[ID_CAP];
    uxid_status status = UXID_OK;
    if (monotonic) {
        uxid_draw_source draws = {.seed = no_seed, .step = no_step};
        uxid_monotonic gen;
        assert(uxid_monotonic_init(&gen, &draws) == UXID_OK);
        status = uxid_monotonic_next(&gen, out, sizeof out, &opts);
        uxid_monotonic_destroy(&gen);
    } else {
        status = uxid_generate(out, sizeof out, &opts);
    }
    if (strcmp(uxid_status_name(status), expected_error) != 0) {
        return fail(id, uxid_status_name(status));
    }
    return true;
}

static bool run_invalid(const char *id, const cJSON *c)
{
    static const char *const top[] = {"id", "input", "expected_error"};
    static const char *const one_of[] = {"decode", "generate"};
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(c, "input");
    const char *expected_error = string_at(c, "expected_error");
    if (!keys_are(c, top, 3, 3) || !keys_are(input, one_of, 2, 0) || cJSON_GetArraySize(input) != 1 ||
        expected_error == NULL) {
        return fail(id, "a key the invalid kind does not define, or one it requires is missing");
    }
    const char *decode = string_at(input, "decode");
    if (decode != NULL) {
        return refused_decode(id, decode, expected_error);
    }
    const cJSON *generate = cJSON_GetObjectItemCaseSensitive(input, "generate");
    if (generate == NULL) {
        return fail(id, "decode is not a string");
    }
    return refused_generate(id, generate, expected_error);
}

/* The draws of one monotonic case, replayed in order. */
struct replay {
    uint64_t initial_seed;
    bool has_overflow_seed;
    uint64_t overflow_seed;
    const cJSON *steps;
    int steps_used;
    int seeds_used;
};

static int replay_seed(void *ctx, uint8_t *out, size_t width)
{
    struct replay *r = ctx;
    if (r->seeds_used > 1 || (r->seeds_used == 1 && !r->has_overflow_seed)) {
        return 1;
    }
    uint64_t value = r->seeds_used == 0 ? r->initial_seed : r->overflow_seed;
    r->seeds_used++;
    if (width < 8U && value >> (8U * width) != 0) {
        return 1;
    }
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
    if (r->steps_used >= cJSON_GetArraySize(r->steps) ||
        !read_uint(cJSON_GetArrayItem(r->steps, r->steps_used), JSON_INT_MAX, step)) {
        return 1;
    }
    r->steps_used++;
    return 0;
}

/* Runs each call of a monotonic case against its expected ids, with the generator gen. */
static bool monotonic_calls(const char *id, uxid_monotonic *gen, uxid_options opts, const cJSON *times,
                            const cJSON *expected)
{
    for (int i = 0; i < cJSON_GetArraySize(times); i++) {
        if (!read_uint(cJSON_GetArrayItem(times, i), TIME_MAX, &opts.time)) {
            return fail(id, "a time of the wrong type or out of range");
        }
        opts.has_time = true;
        const cJSON *want = cJSON_GetArrayItem(expected, i);
        char out[ID_CAP];
        uxid_status status = uxid_monotonic_next(gen, out, sizeof out, &opts);
        if (status != UXID_OK) {
            return fail(id, uxid_status_name(status));
        }
        if (cJSON_IsString(want) == 0 || strcmp(out, want->valuestring) != 0) {
            return fail(id, "an id differs from expected");
        }
    }
    return true;
}

static bool run_monotonic(const char *id, const cJSON *c)
{
    static const char *const top[] = {"id", "input", "expected"};
    static const char *const keys[] = {"prefix", "size", "compact", "times", "initial_seed", "steps", "overflow_seed"};
    const cJSON *input = cJSON_GetObjectItemCaseSensitive(c, "input");
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(c, "expected");
    if (!keys_are(c, top, 3, 3) || !keys_are(input, keys, 7, 7)) {
        return fail(id, "a key the monotonic kind does not define, or one it requires is missing");
    }
    const cJSON *times = cJSON_GetObjectItemCaseSensitive(input, "times");
    const cJSON *overflow = cJSON_GetObjectItemCaseSensitive(input, "overflow_seed");
    struct replay r = {.steps = cJSON_GetObjectItemCaseSensitive(input, "steps")};
    uxid_options opts = case_options();
    r.has_overflow_seed = cJSON_IsNull(overflow) == 0;
    if (!read_prefix(cJSON_GetObjectItemCaseSensitive(input, "prefix"), &opts.prefix) ||
        !read_size(cJSON_GetObjectItemCaseSensitive(input, "size"), &opts.size) ||
        !read_bool(cJSON_GetObjectItemCaseSensitive(input, "compact"), &opts.compact_time) ||
        !read_uint(cJSON_GetObjectItemCaseSensitive(input, "initial_seed"), JSON_INT_MAX, &r.initial_seed) ||
        (r.has_overflow_seed && !read_uint(overflow, JSON_INT_MAX, &r.overflow_seed)) || cJSON_IsArray(times) == 0 ||
        cJSON_IsArray(r.steps) == 0 || cJSON_IsArray(expected) == 0 || cJSON_GetArraySize(times) == 0 ||
        cJSON_GetArraySize(times) != cJSON_GetArraySize(expected)) {
        return fail(id, "an input value of the wrong type or out of range");
    }
    uxid_draw_source draws = {.seed = replay_seed, .step = replay_step, .ctx = &r};
    uxid_monotonic gen;
    assert(uxid_monotonic_init(&gen, &draws) == UXID_OK);
    bool ok = monotonic_calls(id, &gen, opts, times, expected);
    uxid_monotonic_destroy(&gen);
    if (ok && (r.steps_used != cJSON_GetArraySize(r.steps) || r.seeds_used != (r.has_overflow_seed ? 2 : 1))) {
        return fail(id, "the generator did not make exactly the case's draws");
    }
    return ok;
}

/*
 * Runs one case line of the given kind: true when it passes. A line that is
 * not one JSON object, whose id does not start with the kind and a slash, or
 * whose kind this runner does not know fails.
 */
static bool run_case(const char *kind, const char *line)
{
    cJSON *c = cJSON_Parse(line);
    if (c == NULL) {
        return fail(NULL, "a line that is not JSON");
    }
    const char *id = string_at(c, "id");
    size_t kind_len = strlen(kind);
    bool ok = false;
    if (id == NULL || strncmp(id, kind, kind_len) != 0 || id[kind_len] != '/') {
        ok = fail(id, "an id that does not start with its kind and a slash");
    } else if (strcmp(kind, "encode") == 0) {
        ok = run_encode(id, c);
    } else if (strcmp(kind, "deterministic") == 0) {
        ok = run_deterministic(id, c);
    } else if (strcmp(kind, "decode") == 0) {
        ok = run_decode(id, c);
    } else if (strcmp(kind, "invalid") == 0) {
        ok = run_invalid(id, c);
    } else if (strcmp(kind, "monotonic") == 0) {
        ok = run_monotonic(id, c);
    } else {
        ok = fail(id, "a kind this runner does not know");
    }
    cJSON_Delete(c);
    return ok;
}

/* The whole file at UXID_CORPUS_DIR/name, NUL-terminated, its length in *len. */
static char *read_corpus_file(const char *name, size_t *len)
{
    char path[1024];
    int n = snprintf(path, sizeof path, "%s/%s", UXID_CORPUS_DIR, name);
    assert(n > 0 && (size_t)n < sizeof path);
    FILE *file = NULL;
#ifdef _MSC_VER
    assert(fopen_s(&file, path, "rb") == 0);
#else
    file = fopen(path, "rb");
#endif
    assert(file != NULL);
    size_t cap = 4096;
    size_t used = 0;
    char *data = malloc(cap);
    assert(data != NULL);
    /* Reads until a short read, which is the end of the file or an error, and never after one. */
    for (;;) {
        if (cap - used == 1U) {
            cap *= 2U;
            char *grown = realloc(data, cap);
            assert(grown != NULL);
            data = grown;
        }
        size_t want = cap - used - 1U;
        size_t got = fread(data + used, 1, want, file);
        used += got;
        if (got < want) {
            break;
        }
    }
    assert(ferror(file) == 0);
    assert(fclose(file) == 0);
    assert(used < cap);
    data[used] = '\0';
    *len = used;
    return data;
}

/*
 * Runs every line of one case file, each ended by a newline, and counts them
 * into *cases; answers the number that failed.
 */
static size_t run_file(const char *kind, const char *name, size_t *cases)
{
    size_t len = 0;
    char *data = read_corpus_file(name, &len);
    assert(len == 0 || data[len - 1U] == '\n');
    assert(strlen(data) == len);
    size_t failures = 0;
    *cases = 0;
    char *line = data;
    for (char *end = strchr(line, '\n'); end != NULL; end = strchr(line, '\n')) {
        *end = '\0';
        if (!run_case(kind, line)) {
            failures++;
        }
        (*cases)++;
        line = end + 1;
    }
    free(data);
    return failures;
}

static void test_every_case(void) /* (sabotage: marker check drops Z -> failures == 0) */
{
    size_t len = 0;
    char *text = read_corpus_file("manifest.json", &len);
    cJSON *manifest = cJSON_Parse(text);
    free(text);
    static const char *const top[] = {"corpus_version", "files", "corpus_hash"};
    static const char *const file_keys[] = {"kind", "file", "case_count"};
    assert(keys_are(manifest, top, 3, 3));
    uint64_t version = 0;
    assert(read_uint(cJSON_GetObjectItemCaseSensitive(manifest, "corpus_version"), JSON_INT_MAX, &version));
    assert(version == 1);
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(manifest, "files");
    assert(cJSON_IsArray(files) != 0 && cJSON_GetArraySize(files) > 0);
    size_t failures = 0;
    size_t total = 0;
    for (const cJSON *entry = files->child; entry != NULL; entry = entry->next) {
        assert(keys_are(entry, file_keys, 3, 3));
        const char *kind = string_at(entry, "kind");
        const char *name = string_at(entry, "file");
        uint64_t case_count = 0;
        assert(kind != NULL && name != NULL);
        assert(read_uint(cJSON_GetObjectItemCaseSensitive(entry, "case_count"), JSON_INT_MAX, &case_count));
        size_t cases = 0;
        failures += run_file(kind, name, &cases);
        assert(cases == case_count);
        total += cases;
    }
    cJSON_Delete(manifest);
    (void)fprintf(stderr, "corpus: %zu cases run, %zu failed\n", total, failures);
    assert(total > 0);
    assert(failures == 0);
}

/* An encode case of the corpus with its prefix and one extra spelt in. */
static bool encode_line(const char *prefix_json, const char *input_extra, const char *top_extra)
{
    char line[512];
    int n = snprintf(line, sizeof line,
                     "{\"expected\":\"01hf7yat0004h1\",\"id\":\"encode/runner-check\",\"input\":{\"case\":\"lower\","
                     "\"compact\":false,\"prefix\":%s,\"rand\":\"0123\",\"size\":\"s\",\"time\":1700000000000%s}%s}",
                     prefix_json, input_extra, top_extra);
    assert(n > 0 && (size_t)n < sizeof line);
    return run_case("encode", line);
}

static void test_unknown_key_fails(void) /* (sabotage: keys_are ignores unknown keys -> !encode_line(...)) */
{
    assert(encode_line("\"\"", "", ""));
    assert(!encode_line("\"\"", ",\"delimiter\":\"-\"", ""));
    assert(!encode_line("\"\"", "", ",\"note\":\"x\""));
    assert(!encode_line("\"\"", ",\"case\":\"upper\"", ""));
    assert(!run_case("decode",
                     "{\"expected\":{\"compact\":false,\"deterministic\":false,\"prefix\":null,"
                     "\"random\":\"00\",\"size\":\"s\",\"time\":1700000000000},\"id\":\"decode/runner-check\","
                     "\"input\":\"01hf7yat0004h1\"}"));
    assert(!run_case("invalid", "{\"expected_error\":\"rand_size_too_large\",\"id\":\"invalid/runner-check\","
                                "\"input\":{\"generate\":{\"rand_size\":11,\"strict\":true}}}"));
    assert(run_case("invalid", "{\"expected_error\":\"rand_size_too_large\",\"id\":\"invalid/runner-check\","
                               "\"input\":{\"generate\":{\"rand_size\":11}}}"));
    assert(!run_case("other", "{\"id\":\"other/runner-check\"}"));
}

static void test_null_prefix_is_none(void) /* (sabotage: read_prefix refuses null -> encode_line("null", "", "")) */
{
    assert(encode_line("null", "", ""));
    assert(!encode_line("7", "", ""));
    assert(!encode_line("\"usr\"", "", ""));
}

int main(void)
{
    /* The runner checks itself first, so a failure in it is not read as a library failure. */
    test_unknown_key_fails();
    test_null_prefix_is_none();
    test_every_case();
    return 0;
}
