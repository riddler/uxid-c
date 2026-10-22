/*
 * uxid: prefixed, K-sortable identifiers a person can read, wire-compatible
 * with the Elixir reference uxid package.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UXID_H
#define UXID_H

#ifdef __cplusplus
extern "C" {
#endif

/* The major version of this library, as an integer constant. */
#define UXID_VERSION_MAJOR 0

/* The minor version of this library, as an integer constant. */
#define UXID_VERSION_MINOR 1

/* The patch version of this library, as an integer constant. */
#define UXID_VERSION_PATCH 0

/*
 * The version of this library as a string literal, "MAJOR.MINOR.PATCH",
 * always equal to the three macros above. The CMake package version and the
 * pkg-config version are read from these macros.
 */
#define UXID_VERSION_STRING "0.1.0"

#ifdef __cplusplus
}
#endif

#endif /* UXID_H */
