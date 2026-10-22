/*
 * Deterministic ids: uxid_generate with the from option. The corpus vectors
 * are the cases of deterministic.json and the rand-with-from case of
 * invalid.json in the uxid conformance corpus at the reference's v2.10.0 tag,
 * each named by its case id. The other known answers were computed with an
 * independent SHA-256 over the hash input the wire format defines.
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

struct deterministic_case {
    const char *id;
    const char *prefix;
    const char *from;
    uxid_size size;
    uxid_case letter_case;
    const char *expected;
};

/* Each row is the corpus case deterministic/<id>. */
static const struct deterministic_case deterministic_cases[] = {
    {"golden-usr-xl", "usr", "alice@example.com", UXID_SIZE_XL, UXID_CASE_LOWER, "usr_zcvt7epac0t1ebcsjfyf7cwz25"},
    {"golden-cus-s", "cus", "acme-corp", UXID_SIZE_S, UXID_CASE_LOWER, "cus_zt9vnyfd85bff0"},
    {"golden-no-prefix-m", NULL, "alice@example.com", UXID_SIZE_M, UXID_CASE_LOWER, "zqz4cwzgyw8ks97869"},
    {"usr-xs", "usr", "alice@example.com", UXID_SIZE_XS, UXID_CASE_LOWER, "usr_z1yhw20vag"},
    {"usr-s", "usr", "alice@example.com", UXID_SIZE_S, UXID_CASE_LOWER, "usr_z5rtsrkhx2h4dr"},
    {"usr-m", "usr", "alice@example.com", UXID_SIZE_M, UXID_CASE_LOWER, "usr_zt8naccefxhwnrmscd"},
    {"usr-l", "usr", "alice@example.com", UXID_SIZE_L, UXID_CASE_LOWER, "usr_zwbz6myvgz7ehe3nceap5n"},
    {"usr-xl", "usr", "alice@example.com", UXID_SIZE_XL, UXID_CASE_LOWER, "usr_zcvt7epac0t1ebcsjfyf7cwz25"},
    {"empty-prefix-xl", "", "alice@example.com", UXID_SIZE_XL, UXID_CASE_LOWER, "z4krgegf6drn563pvqhya4tsx9"},
    {"no-prefix-xl", NULL, "alice@example.com", UXID_SIZE_XL, UXID_CASE_LOWER, "z4krgegf6drn563pvqhya4tsx9"},
    {"usr-xl-upper", "usr", "alice@example.com", UXID_SIZE_XL, UXID_CASE_UPPER, "usr_ZCVT7EPAC0T1EBCSJFYF7CWZ25"},
    {"non-ascii-from-m", "cus", "caf\xc3\xa9", UXID_SIZE_M, UXID_CASE_LOWER, "cus_zfsx9se75671f9kdjv"},
};

/* Options for a deterministic id of from at a size, with no prefix. */
static uxid_options from_options(const char *from, size_t from_len, uxid_size size)
{
    uxid_options opts;
    uxid_options_init(&opts);
    opts.from = from;
    opts.from_len = from_len;
    opts.size = size;
    return opts;
}

/* A byte source that counts its calls and always fails. */
static int failing_draw(void *ctx, uint8_t *out, size_t len)
{
    int *calls = ctx;
    (*calls)++;
    if (len > 0) {
        out[0] = 0;
    }
    return -1;
}

static void test_deterministic_corpus_cases(void) /* (sabotage: count byte 0 -> strcmp(out, c->expected) == 0) */
{
    for (size_t i = 0; i < sizeof deterministic_cases / sizeof deterministic_cases[0]; i++) {
        const struct deterministic_case *c = &deterministic_cases[i];
        uxid_options opts = from_options(c->from, strlen(c->from), c->size);
        opts.prefix = c->prefix;
        opts.letter_case = c->letter_case;
        char out[64];
        memset(out, 'x', sizeof out);
        assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
        assert(strcmp(out, c->expected) == 0);
    }
}

static void test_rand_with_from(void) /* (sabotage: rand check dropped -> status == UXID_ERR_RAND_WITH_FROM) */
{
    /* invalid/generate-rand-with-from */
    const uint8_t rand[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    uxid_options opts = from_options("alice@example.com", strlen("alice@example.com"), UXID_SIZE_XL);
    opts.prefix = "usr";
    opts.rand = rand;
    opts.rand_len = sizeof rand;
    char out[64];
    memset(out, 'x', sizeof out);
    uxid_status status = uxid_generate(out, sizeof out, &opts);
    assert(status == UXID_ERR_RAND_WITH_FROM);
    assert(out[0] == '\0');
    assert(strcmp(uxid_status_name(status), "rand_with_from") == 0);

    /* Fixed bytes of length 0 are still fixed bytes. */
    opts.rand_len = 0;
    assert(uxid_generate(out, sizeof out, &opts) == UXID_ERR_RAND_WITH_FROM);
}

static void test_from_reads_no_time_options(void) /* (sabotage: time checked for from -> uxid_generate() == UXID_OK) */
{
    int calls = 0;
    uxid_options opts = from_options("alice@example.com", strlen("alice@example.com"), UXID_SIZE_XL);
    opts.prefix = "usr";
    opts.has_time = true;
    opts.time = UINT64_C(1) << 48U;
    opts.compact_time = true;
    opts.has_rand_size = true;
    opts.rand_size = 11;
    opts.byte_source = failing_draw;
    opts.byte_source_ctx = &calls;
    char out[64];
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_zcvt7epac0t1ebcsjfyf7cwz25") == 0);
    assert(calls == 0);
}

static void test_from_delimiter(void) /* (sabotage: delimiter written as _ -> strcmp(out, "usr--zcvt...") == 0) */
{
    uxid_options opts = from_options("alice@example.com", strlen("alice@example.com"), UXID_SIZE_XL);
    opts.prefix = "usr";
    opts.delimiter = "--";
    char out[64];
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    /* The delimiter is not part of the hash input. */
    assert(strcmp(out, "usr--zcvt7epac0t1ebcsjfyf7cwz25") == 0);
}

static void test_from_buffer(void) /* (sabotage: no marker room -> uxid_generate() == UXID_ERR_BUFFER_TOO_SMALL) */
{
    uxid_options opts = from_options("acme-corp", strlen("acme-corp"), UXID_SIZE_S);
    opts.prefix = "cus";
    const size_t needed = sizeof "cus_zt9vnyfd85bff0";
    char out[sizeof "cus_zt9vnyfd85bff0" + 1];
    memset(out, 'x', sizeof out);
    assert(uxid_generate(out, needed, &opts) == UXID_OK);
    assert(strcmp(out, "cus_zt9vnyfd85bff0") == 0);
    assert(out[needed] == 'x');
    for (size_t size = 1; size < needed; size++) {
        memset(out, 'x', sizeof out);
        assert(uxid_generate(out, size, &opts) == UXID_ERR_BUFFER_TOO_SMALL);
        assert(out[0] == '\0');
        assert(out[1] == 'x');
    }
}

static void test_from_bytes_as_given(void) /* (sabotage: from_len as strlen -> strcmp(out, "usr_zkcw...") == 0) */
{
    char out[64];

    /* The empty string is an input, not an absent one. */
    uxid_options opts = from_options("", 0, UXID_SIZE_XL);
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "za5v3ysnz0vxz4xcs0m1k7f0s7") == 0);

    /* A zero byte inside from is hashed, so "a", 0, "b" is not "a". */
    opts = from_options("a\0b", 3, UXID_SIZE_M);
    opts.prefix = "usr";
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_zkcwth8y2n0ad446gh") == 0);
    opts.from_len = 1;
    assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
    assert(strcmp(out, "usr_z5h2whgdhmyk5a5b4r") == 0);
}

struct block_case {
    size_t input_len;
    const char *expected;
};

/*
 * With no prefix the hash input is a zero byte, from, a zero byte and the
 * count, so from_len + 3 bytes: these inputs sit at either side of SHA-256's
 * padding boundaries (55 and 56 bytes, 63 and 64, 119 and 120, 128) and at a
 * million bytes, the length of the longest FIPS 180-4 example.
 */
static const struct block_case block_cases[] = {
    {55, "zz9keat35ghe55z9pkk6dvn6nz"},  {56, "zqvnrkp0bqjqx3mpf2z91sny8f"},      {63, "zmpcmvzfjgcjdpfgqbksheckc6"},
    {64, "zjfk0dac5nnm6d6dwm5zq6sx5t"},  {119, "zaebp8tda918mn0s78pg89r2qa"},     {120, "zfd4j318xqmrvhzj1t3fcx7gcw"},
    {128, "z3n8501k3mrva6t1fxrb43zj6e"}, {1000000, "z2npqsye967nmnz1jtyyj5ep7v"},
};

static void test_from_block_boundaries(void) /* (sabotage: pad block at >= 56 -> strcmp(out, c->expected) == 0) */
{
    char *from = malloc(1000000);
    assert(from != NULL);
    memset(from, 'a', 1000000);
    for (size_t i = 0; i < sizeof block_cases / sizeof block_cases[0]; i++) {
        const struct block_case *c = &block_cases[i];
        uxid_options opts = from_options(from, c->input_len - 3U, UXID_SIZE_XL);
        char out[64];
        assert(uxid_generate(out, sizeof out, &opts) == UXID_OK);
        assert(strcmp(out, c->expected) == 0);
    }
    free(from);
}

int main(void)
{
    test_deterministic_corpus_cases();
    test_rand_with_from();
    test_from_reads_no_time_options();
    test_from_delimiter();
    test_from_buffer();
    test_from_bytes_as_given();
    test_from_block_boundaries();
    return 0;
}
