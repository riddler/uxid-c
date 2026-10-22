/*
 * The library's SHA-256 against the example vectors of FIPS 180-4 (the empty
 * message, "abc", the 448-bit and 896-bit messages and one million 'a'
 * bytes), and two messages of 'a' bytes whose padding ends a block exactly.
 * The functions are static in src/uxid.c, so CMake copies that source's
 * marked SHA-256 region into a header in the build tree and this test
 * includes it; the test links nothing from the library, and the library
 * exports nothing for it.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uxid_sha256_region.h"

#ifdef NDEBUG
#error "the tests assert with assert(); the test build never defines NDEBUG"
#endif

/* The digest of len bytes at message, fed in pieces of at most piece bytes, as lowercase hex. */
static void digest_hex(const char *message, size_t len, size_t piece, char hex[(2 * UXID_SHA256_LENGTH) + 1])
{
    uxid_sha256 sha;
    uint8_t digest[UXID_SHA256_LENGTH];
    uxid_sha256_init(&sha);
    size_t at = 0;
    do {
        size_t take = len - at < piece ? len - at : piece;
        uxid_sha256_update(&sha, (const uint8_t *)message + at, take);
        at += take;
    } while (at < len);
    uxid_sha256_final(&sha, digest);
    for (size_t i = 0; i < UXID_SHA256_LENGTH; i++) {
        int n = snprintf(hex + (2 * i), 3, "%02x", (unsigned)digest[i]);
        assert(n == 2);
    }
}

static const char msg_448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const char msg_896[] =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopq"
    "rsmnopqrstnopqrstu";

static void test_fips_short_messages(void) /* (sabotage: ch ~e & g as f & g -> strcmp(hex, "e3b0...") == 0) */
{
    char hex[(2 * UXID_SHA256_LENGTH) + 1];
    digest_hex("", 0, 64, hex);
    assert(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    digest_hex("abc", 3, 64, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static void test_fips_two_block_messages(void) /* (sabotage: pad block unhashed -> strcmp(hex, "248d6a61...") == 0) */
{
    char hex[(2 * UXID_SHA256_LENGTH) + 1];
    assert(strlen(msg_448) == 56 && strlen(msg_896) == 112);
    static const size_t pieces[] = {1, 7, 64, 200};
    for (size_t i = 0; i < sizeof pieces / sizeof pieces[0]; i++) {
        digest_hex(msg_448, strlen(msg_448), pieces[i], hex);
        assert(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
        digest_hex(msg_896, strlen(msg_896), pieces[i], hex);
        assert(strcmp(hex, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1") == 0);
    }
}

static void test_fips_million_a(void) /* (sabotage: length added as uint16_t -> strcmp(hex, "cdc76e5c...") == 0) */
{
    const size_t len = 1000000;
    char *million = malloc(len);
    assert(million != NULL);
    memset(million, 'a', len);
    char hex[(2 * UXID_SHA256_LENGTH) + 1];
    digest_hex(million, len, 1000, hex);
    assert(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    digest_hex(million, len, len, hex);
    assert(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    free(million);
}

static void test_padding_fills_one_block(void) /* (sabotage: pad when used >= 56 -> strcmp(hex, "9f4390f8...") == 0) */
{
    /*
     * 55 bytes, and 55 more than one block: the 0x80 byte leaves exactly the 8 length bytes free in
     * the last block, so the length goes into that block and no extra block of padding is hashed.
     */
    char message[119];
    memset(message, 'a', sizeof message);
    char hex[(2 * UXID_SHA256_LENGTH) + 1];
    digest_hex(message, 55, 64, hex);
    assert(strcmp(hex, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318") == 0);
    digest_hex(message, 119, 7, hex);
    assert(strcmp(hex, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb") == 0);
}

int main(void)
{
    test_fips_short_messages();
    test_fips_two_block_messages();
    test_fips_million_a();
    test_padding_fills_one_block();
    return 0;
}
