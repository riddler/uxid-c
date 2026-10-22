/*
 * uxid: the library source.
 *
 * SPDX-License-Identifier: MIT
 */
#include "uxid.h"

/*
 * ISO C requires a translation unit to hold at least one declaration. This
 * one also checks, at compile time, that the version string is at least as
 * long as the shortest "MAJOR.MINOR.PATCH".
 */
_Static_assert(sizeof UXID_VERSION_STRING >= sizeof "0.0.0", "UXID_VERSION_STRING is MAJOR.MINOR.PATCH");
