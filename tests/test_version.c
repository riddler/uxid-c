/*
 * The version macros in uxid.h.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <uxid.h>

#ifdef NDEBUG
#error "the tests assert with assert(); the test build never defines NDEBUG"
#endif

static void test_version_string_matches_macros(void) /* (sabotage: STRING is 0.1.1 -> strcmp with built failed) */
{
    char built[32];
    int written = snprintf(built, sizeof built, "%d.%d.%d", UXID_VERSION_MAJOR, UXID_VERSION_MINOR, UXID_VERSION_PATCH);
    assert(written > 0 && (size_t)written < sizeof built);
    assert(strcmp(UXID_VERSION_STRING, built) == 0);
}

static void test_version_matches_cmake_project(void) /* (sabotage: project VERSION 0.1.1 -> strcmp failed) */
{
    assert(strcmp(UXID_VERSION_STRING, UXID_TEST_PROJECT_VERSION) == 0);
}

int main(void)
{
    test_version_string_matches_macros();
    test_version_matches_cmake_project();
    return 0;
}
