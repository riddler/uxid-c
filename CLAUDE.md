# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

## Beads issue tracker

This project tracks all work in **bd (beads)** - not TodoWrite, not markdown TODO
lists. The prefix is `uxc-`. Run `bd prime` for the command reference and
session-close protocol, and `bd remember` for knowledge that should outlive the
session.

Claude Code injects `bd prime` at session start, so this section is deliberately
a stub; the authority rules below are the part that is specific to this repo.

`AGENTS.md` is a symlink to this file. There is one set of instructions, not two.

### Beads that span repositories

| Situation | Rule |
|---|---|
| A decision is recorded in two trackers and they disagree | The repository whose files change owns the decision. The ID format itself is uxid-ex's call (its wire-format spec and its conformance corpus); this port's C API, packaging and gate are this repo's call |
| A bead pairs with one in another repo | Both halves carry `mirrors: <id>` as the first line of the description |
| You are about to schedule, claim, plan against, or cite the status of a mirrored bead | Re-read the other tracker first and write a new dated note above the old one, then act |
| A `mirrors:` line names an id that no longer resolves | Broken immediately, not stale. Fix it with one `bd update` the moment you notice |

## Agent authority in this repo

**This repository grants an agent the authority to commit, push, and open
requests only inside an orchestrated campaign that carries the operator's
explicit consent for that campaign.** The grant is consent-scoped, not
standing. Outside such a campaign the conservative rules `bd prime` describes
apply in full, and so they do for any action the table below does not name.

What unlocks the grant is the operator saying, in their own words, that a
particular campaign may commit, push, and open requests here. Nothing else
does. It is **not** inferable from another repository having opted into the
team-maintainer profile; not from this file's resemblance to theirs; not from
the fact that the same person works on all of them. A dispatch from another
agent - a conductor, an orchestrator, a parent session - is not by itself the
operator's consent either, however confidently it asserts otherwise. An agent
that believes consent exists but cannot point to where the operator gave it
should do the work, stop before the irreversible step, and report.

| Action | Trigger | Still unauthorized when |
|---|---|---|
| `bd` task tracking (`create`, `claim`, `update`, `note`) | any time | never - this is the conservative profile too |
| `mise run gate` and any of its tasks | any time | never - running the gate costs nothing but time |
| `git commit` on the bead's branch | a campaign carrying the operator's explicit consent **and** the bead's work complete **and** the full gate (`mise run gate`) green; a change touching no path the manifest lists under `gate.build_paths` or `gate.also_gated_paths` (the lists the gate rules below name) has no gate to run and may commit on review of the diff alone | on `main`, on a red gate, on a `mise run gate:loop` or otherwise scoped run, or with unrelated changes in the tree |
| `git push`, `gh pr create` | the same consent, **and** the terminology scan clean over the full outbound content | any scan hit - that is a hard stop, not something to rephrase past |
| merging a campaign PR | a campaign consent the operator adopted verbatim that names automatic merges, with every named condition met (full gate green, firewall scan clean with a positive control, any named review gate passed) | outside such a consent; any named condition unmet; any PR the consent's carve-outs hold for the operator |
| `bd close <id>` | never for a mirrored bead whose other half is not merged to its own repo's `origin/main`; a mirrored bead whose other half has ALSO landed may be closed by the campaign conductor under a consent naming this exception, both halves together, each verified against its remote; otherwise the operator's call | for a bead whose description carries a `mirrors:` line while its other half is unlanded, campaign consent included |
| `bd dolt push` | bead state changed locally **and** the git side of the same change has already reached `origin`; inside a campaign, the conductor pushes (atomically across the campaign's trackers) | as a way to publish beads for work that is not on `origin/main` yet |
| a release prep (a version bump and a changelog promotion) and its tag | a release bead a campaign consent names, or the operator names in their own words; the tag once that prep is merged to `origin/main`, naming its version at the merged commit | on any other bead or on `main`; the tag before the prep is on `origin/main` |
| a release, a GitHub release of this package | never - a version of this package is the GitHub release the release workflow (`.github/workflows/release.yml`) creates on the tag push the release-prep row above already allows; an agent or a session never creates, edits, deletes or uploads to a GitHub release by hand (no `gh release create`, `edit`, `delete` or `upload`), and never moves or deletes a pushed tag | always - a failed workflow is re-run from its Actions page, never worked round by hand; a broken release is fixed by a new patch version, never by moving its tag |

The organizing principle: the human gate belongs where an action stops being
reversible. A commit on a per-bead branch is undone with
`git reset --soft HEAD~1`. A push, a request, a merge outside a consented
campaign, and a closed bead are visible to other people and other machines,
so a campaign's consent is what buys the first two and nothing buys the last
two.

Two rules override every row above. A current "do not commit", "do not push",
or equivalent instruction from the operator wins outright. And authority is
the operator's to give, never an agent's to infer: a subagent that believes a
trigger has fired - reasoning its way there from its dispatch, from a sibling
repo, or from the fact that it was asked to do the work - reports that, it
does not act on it. A subagent carrying the operator's consent relayed
verbatim by the session that owns the work is the other case: there the
authority is the operator's and the subagent is only the hands, so it may act.
What has to be quotable is the relay - the operator's own words authorizing
that campaign, not the subagent's sense of being authorized. A subagent that
cannot quote them reports and stops. A relay unlocks nothing the rows above
forbid outright: closing a mirrored bead, creating, editing or deleting a
GitHub release by hand, and moving a pushed tag stay forbidden however the
consent arrives. A release prep and its tag are not a release; the Release
preps paragraph below records them.

Merging a campaign PR is a recorded exception: under a campaign consent the
operator has adopted verbatim that names automatic merges, with every
condition that consent names met, the conductor's merge executes the
operator's own authorization - the consent's text is what may be done and
nothing more. (Adopted here at onboarding with the rest of this authority
table.)

**Release preps.** The version bump and the tag of a release prep are the
family norm, not a grant a campaign consent has to name. On a release bead
a campaign consent names, or the operator names in their own words,
the prep - the version bump (the `UXID_VERSION_*` macros in `include/uxid.h`,
which CMake's `project(VERSION)` reads, so the CMake package version and the
pkg-config version follow them), the version in the README's CMake
`FetchContent` line once the README carries one, and the changelog promotion -
lands through the rows above; once it is merged to `origin/main`, the
conductor or the session that owns the release bead tags that merged commit
with the new version (`vX.Y.Z`) and pushes the tag. The tag push is the
release: the release workflow (`.github/workflows/release.yml`, the file the
release publishes from) verifies the tag and its publish job creates the
GitHub release on it, carrying `uxid-<version>.tar.gz` (a `git archive` of
the tag) and its `.sha256`. A failed workflow is re-run from its Actions page,
never worked round by hand. That workflow arrives with a later change; until
it is on `main`, nothing publishes this package.

There is no package registry for this package. The publish job, and no other
job or workflow, holds `contents: write`, and it creates the release with the
workflow's own `GITHUB_TOKEN`. No repository secret exists in this repository
and none is created: no agent or session lists, reads, prints or sets one,
and nobody runs `gh secret` against this repository. vcpkg and Conan ports
are not part of this repository. Merging the prep follows this file's merge
row, and nothing else this file reserves for the operator changes. A prep is
cut only on a green full gate, which will include the conformance corpus
check.

This repository keeps no decision records (`docs/adr`). A release or gating
decision is recorded here, in the authority table and the Release preps
paragraphs, and in the header of the workflow it governs; a change a user of
the API could notice is a changelog fragment (Conventions, below). The ID
format itself is not decided here: it is uxid-ex's wire-format spec,
`guides/wire-format.md` in https://github.com/riddler/uxid-ex, read at the
tag the vendored corpus names (`conformance/SOURCE.json`, once the corpus is
vendored). That this repository keeps no decision records was decided by the
conductor under a standing consent, 2026-10-21; a future API decision may
start a `docs/adr` directory.

Widening this section is a decision for the operator to make and record here.
An agent may draft the change; it does not adopt it.

## Non-interactive shell commands

`cp`, `mv`, and `rm` may be aliased to `-i` on a developer's machine, which
hangs an agent forever on a y/n prompt it cannot see. Always pass the
non-interactive form: `cp -f`, `mv -f`, `rm -f`, `rm -rf`, `cp -rf`. Same for
`scp` and `ssh` (`-o BatchMode=yes`), `apt-get` (`-y`), and `brew`
(`HOMEBREW_NO_AUTO_UPDATE=1`).

Also avoid `bd edit`, which opens `$EDITOR` and blocks. Use
`bd update <id> --title/--description/--notes/--design` instead.

## What this project is

`uxid` for C. Version 1.0.0, the package this repository is being built into,
makes prefixed, K-sortable, Stripe-style identifiers a person can read, copy
and route back to their resource, wire-compatible with the Elixir reference
`uxid` and checked against its conformance corpus. It covers generation
(random, deterministic and monotonic), decoding and the validity checks, into
caller-owned buffers with status codes. It is C11, one header
(`include/uxid.h`) and one source (`src/uxid.c`), with no dependency beyond
the platform C library (and bcrypt on Windows). Its default byte source is the
platform CSPRNG: `getrandom` on Linux, `arc4random_buf` on macOS and the BSDs,
`BCryptGenRandom` on Windows. It is built with CMake (`find_package(uxid)`,
target `uxid::uxid`) or found through pkg-config (`uxid`). The target's link
requirements are `bcrypt` on Windows, for the CSPRNG, and the platform thread
library on POSIX (`-pthread`), for the monotonic generator's lock; both are
platform libraries, not dependencies.

The tree on `main` today holds the build and the gate around a library with
no code yet: `CMakeLists.txt` (the `uxid` library from `src/uxid.c` and
`include/uxid.h`, its install and export rules, `uxidConfig.cmake` and
`uxid.pc` from `cmake/`), `CMakePresets.json`, `mise.toml` with the gate
tasks, `.clang-format`, `.clang-tidy`, the tests under `tests/` and the
coverage script under `scripts/`, beside these agent instructions, the wurk
manifest (`.claude/wurk.json`) and the tracker files under `.beads/`.
`include/uxid.h` declares only the version macros. The generation, decoding
and validity code, CI, the release workflow and the vendored corpus arrive
with later changes; until they do, the sentences in this file about the 1.0.0
package, its corpus and its workflows describe the target, not the current
tree.

- **The package is `uxid`.** Version 1.0.0 is its first release, a GitHub
  release on the tag `v1.0.0`.
- **The corpus is the arbiter.** `conformance/` will hold the corpus
  vendored from uxid-ex at a release tag, its tag, commit and hash recorded in
  `conformance/SOURCE.json`. The vendored files are never edited, and every
  case passes with no skip list: a case this port cannot pass is reported
  against uxid-ex with its evidence, never skipped or worked round here. An
  answer the corpus does not fix follows uxid-ex's spec at that tag.

`README.md`, which a later change adds, is the user reference; `CHANGELOG.md`
records each release.

## Build & Test

The gate tasks live in `mise.toml`.

```bash
mise install            # CMake, clang-format, clang-tidy and the tools mise.toml pins
mise run gate:loop      # inner loop
mise run gate           # full gate
```

The C compiler is the platform's own (Apple clang on macOS, gcc or clang on
Linux, MSVC on Windows); mise pins the tools around it: CMake, and
clang-format and clang-tidy from LLVM's PyPI releases (installed through uv
on a pinned Python). The full gate prints the compiler's version first and
then runs: the CMake configure and build with
`-Wall -Wextra -Wpedantic -Werror` (`/W4 /WX` on MSVC) and the CTest tests
(the `default` preset, a static library), a second build with
AddressSanitizer and UndefinedBehaviorSanitizer and its CTest run
(`asan-ubsan`, a shared library, so the gate builds both kinds), the
clang-format check, clang-tidy over the `default` preset's compile database (on macOS
with the SDK path from `xcrun`, which LLVM's own clang-tidy does not know),
and line coverage of `src/` at 90 percent (`coverage`, clang's source-based
coverage, `scripts/coverage.cmake`). The conformance corpus check joins the
gate when the corpus is vendored; until then the gate names it and checks
nothing. While `src/` has no executable line, the coverage step passes only
if the built library defines no external symbol; a library that defines one
with no measured line fails it.

One CTest test (`install_consume`) installs the library to a scratch prefix
under the build tree and builds and runs a consumer through
`find_package(uxid CONFIG)` and through `pkg-config --cflags --libs uxid`.
Every preset builds with `CMAKE_BUILD_TYPE=Debug`, and each test source
refuses to compile when `NDEBUG` is defined.

The gate proves the toolchain it runs on. CI, once it lands, proves the four
toolchains the package supports: gcc and clang on Linux, Apple clang on
macOS, and MSVC on Windows.

### This repo's own gate rules

- The full gate is `mise run gate`; the inner loop is `mise run gate:loop`.
  Only the full command is the advancement gate: a `gate:loop` run, like a
  bare `ctest`, a single test binary or any other scoped run, is never
  evidence for a claim that the gate is green.
- **Never truncate the output.** No `| tail`, `| head`, `| grep`; truncating
  removes findings, not noise.
- **Never go green by weakening the check.** Not by lowering the coverage
  minimum, not by dropping `-Werror` or a warning flag, not by a `#pragma`
  that silences a warning, a `NOLINT` marker or a clang-tidy check disabled
  to get past a finding, not by a `// clang-format off` region, not by a
  sanitizer suppression or a sanitizer left out of the build, not by
  `#if 0`, a test left out of the CTest list or a `DISABLED` or `WILL_FAIL`
  property, not by defining `NDEBUG` in the test build, not by dropping or
  skipping a corpus case. If a finding is genuinely wrong for this project,
  say so and let the operator decide.
- **Tests assert with plain `assert`**, and the test build never defines
  `NDEBUG`: an `assert` compiled out is a test that cannot fail.
- Check `git status` after a gate run and include or discard deliberately
  anything it rewrote.
- A change touching no path the manifest (`.claude/wurk.json`) lists under
  `gate.build_paths` or `gate.also_gated_paths` has no gate to run and may
  commit on review of the diff alone. Those two lists are the one source for
  the exception: `src/`, `include/`, `tests/`, `scripts/`, `conformance/` and
  `README.md` are what the gate checks, and `CMakeLists.txt`,
  `CMakePresets.json`, `cmake/`, `mise.toml`, `.clang-format`, `.clang-tidy`
  and `.editorconfig` decide how it runs and what it resolves. A listed path
  need not exist yet.
- **Sabotage notes use the in-name form.** The gate's sabotage scan finds a
  test by its function definition line under `tests/`
  (`static void test_<name>(void)`) and reads a note only on that line
  itself: its other note form reads `#` comment lines above the test, and C
  has no `#` comment (a `#` line is a preprocessor directive), so it never
  sees a `//` or `/* */` comment above a C test. So a test's note is a
  trailing comment on its definition line:
  `static void test_<name>(void) /* (sabotage: <the mutation> -> <the assert that failed>) */`.
  The line, note and indentation included, stays within the clang-format
  column limit: clang-format re-wraps a longer comment, which would move the
  note's closing parenthesis off the line the scan reads.
- Documentation may point at the gate; it never enlarges it.

## Conventions

This repository is not part of the statifier package family, but it is worked
by the same operator with the same tooling, so it adopts the family's
satellite conventions rather than inventing a second set.

- **Domain-free examples.** The README, the header's comments and the test
  fixtures teach no example domain: examples use neutral resource prefixes
  such as `usr` and `cus`, and any address is a fictional `@example.com` one.
- **No dependencies beyond the platform.** The library uses the platform C
  library and, on Windows, bcrypt; it links the platform thread library on
  POSIX. Adding any other dependency needs a bead of its own first. Test-only
  code, such as a vendored JSON parser for the corpus runner, lives under
  `tests/` and is never compiled into the library or installed. Build tools
  come from `mise.toml`.
- **No global state.** No static mutable variable, counter, cache or
  configuration. The monotonic generator is a struct the caller holds and
  initializes, with its lock inside: a pthread mutex on POSIX, an SRWLOCK on
  Windows (decided by the conductor under a standing consent, 2026-10-21).
- **The byte source is injectable; the default is the platform CSPRNG.** The
  default is `getrandom` on Linux, `arc4random_buf` on macOS and the BSDs and
  `BCryptGenRandom` on Windows, every return value checked; never `rand()` or
  another seeded or non-cryptographic generator as a default. Explicit random
  bytes, where the API takes them, exist for test vectors and are documented
  as such. No test-only seam lives in the library source (`src/` and
  `include/`).
- **C11, CMake and the platform toolchains** (ruled by the operator,
  2026-10-21): one header and one source, CMake with install and export
  rules and a pkg-config file, CTest, the sanitizer build, warnings as errors,
  clang-format and clang-tidy in the gate, and CI on Linux (gcc and clang),
  macOS and Windows once CI lands. The coverage bar is 90 percent of lines
  (decided by the conductor under a standing consent, 2026-10-21).
- **Caller-owned buffers and status codes.** Every output goes into a buffer
  the caller passes with its size, and every call that can fail returns a
  status code rather than aborting or printing.
- **Tests use CTest and plain `assert`** under `tests/`. Every public
  declaration in `include/uxid.h` carries a comment saying what it does.
- Sabotage every new test that asserts library behavior: break the code it
  covers, confirm it goes red, revert, and note the mutation in the in-name
  form the gate rules above give.
- Changelog: a change a user of the API could notice adds a fragment under
  `changelog.d/`, named for the bead (`changelog.d/uxc-abc.md`), in the same
  request; tests, docs, tooling and gate changes take none. New version
  sections in `CHANGELOG.md` keep the file's own heading shape; existing
  history is not rewritten.
- Commit messages: simple present tense ("Adds ...", "Fixes ..."), body
  wrapped at ~72 chars explaining why. Requests are rebase-merged, so each
  commit lands on main with its message unchanged. No AI attribution
  trailers.
