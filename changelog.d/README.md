# Changelog fragments

Changelog entries for unreleased work live here, one file per issue, not as
edits to `CHANGELOG.md`. At release the fragments are assembled into one
version section of `CHANGELOG.md` and deleted.

## Why fragments

Work happens in one worktree per issue, so several branches are usually open
at once. If each appended to the top of `CHANGELOG.md`, every branch would
touch the same lines of the same file and nearly every pull request would
conflict with every other. A fragment is named for its issue, so no two
branches write the same file.

## When a change needs a fragment

The changelog serves people who use the library. Write a fragment for a
change a user of the API could notice:

- a public function, type, enum value, macro or status code in
  `include/uxid.h` added, changed or removed
- a change in observable behavior, including what an id looks like and what
  generation, decoding or the validity checks accept or refuse
- a change in how the library is built, found or linked: the CMake package
  or target name, the pkg-config module, the C standard, the supported
  platforms or compilers, or the libraries the target links
- a bug fix a user could have noticed
- anything breaking

Write none for tests, documentation, tooling, CI, the release workflow or
gate changes. If you are unsure, ask whether someone who only calls the
public API, or only builds and links the library, could tell the
difference; if not, skip it.

## Format

One file per issue, named for its beads issue id, in the same pull request as
the change:

    changelog.d/uxc-abc.md

The file holds a Keep a Changelog kind heading and one bullet per change; an
illustration of the shape, not a change this library has made:

```markdown
### Fixed

- The validity check answers false for a `usr` id when the caller requires the `cus` prefix.
```

- The kind headings are `### Added`, `### Changed`, `### Deprecated`,
  `### Removed`, `### Fixed` and `### Security`. A file may carry more than
  one when the issue genuinely spans them.
- Bullets are `- `, one per change, in the present tense, saying what the
  change means to a user. No nested bullets: detail belongs in the pull
  request and the commit body.
- A breaking change opens its bullet with `Breaking:` and says what a caller
  does about it, not only what broke.
- A change in the vendored conformance corpus that changes an id this library
  produces or accepts is visible to every consumer that stores ids: say what
  changes on the consumer's side.
- Examples stay domain-free: neutral resource prefixes such as `usr` or
  `cus`, and fictional `@example.com` addresses.

## At release

The release prep assembles the fragments into a new version section at the
top of `CHANGELOG.md`, at the `##` level the file's own `## Unreleased`
heading uses: `## X.Y.Z - YYYY-MM-DD`. The entries are grouped under the kind
headings, `### Added` and the rest, in the order Added, Changed, Deprecated,
Removed, Fixed, Security; within one kind, fragments go in file-name order and
each fragment's bullets keep their order and their `- ` marker. A `Breaking:`
bullet keeps that word. The sections already released in `CHANGELOG.md` are
not rewritten. The fragments are deleted in the same commit.

The prep (the version macros in `include/uxid.h`, the version in the README's
CMake lines once it carries them, the promoted section and the deleted
fragments) merges on its own, and then its merged commit is tagged `vX.Y.Z`.
The release workflow, `.github/workflows/release.yml`, creates the GitHub
release on that tag push, with the version's section of `CHANGELOG.md` as the
release notes; a tag whose version has no section there creates no release
(CLAUDE.md, "Release preps"). Because the prep consumes every fragment, it
merges last: a branch still holding an unmerged fragment when the prep lands
waits for the next release.
