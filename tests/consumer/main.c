/*
 * A program built against an installed uxid. It prints the version the
 * installed header declares, which the install test compares with the
 * version it built.
 */
#include <stdio.h>
#include <uxid.h>

int main(void)
{
    return printf("%s\n", UXID_VERSION_STRING) < 0 ? 1 : 0;
}
