# AGENTS.md

Read [README.md](README.md) and [docs/](docs) first, and bedrock-data's README: it is the format of everything extract
writes.

## Invariants

- **Each file has one writer.** `versions/`: `pnpm track`, except each zip's `"supported"`: `pnpm support`, through
  a pull request. bedrock-data: `pnpm extract`. Never hand-edit any of them, never add an override layer of
  corrections: where extracted data disagrees with another source, find out why and fix the extraction, or document
  the disagreement.
- **A run changes files only where the game says otherwise.** No timestamps, no extractor version, no
  machine-dependent bytes (gzip is rewritten only where its decompressed text changed). `pnpm track` with nothing new,
  or `pnpm extract` with nothing unexported, must leave both repos' `git status` clean.
- **Only supported zips are exported, and an export is never repeated unasked.** `pnpm extract` reads a release from
  a platform it is supported on, and skips a release bedrock-data has every kind of; `--force` exports it again.
- **A platform's server runs on its own OS, and its answers are its own.** The blocks are read from the running
  server: a release is exported from its first supported platform (linux's where both are), never from the other
  platform where this machine cannot run it. Where the platforms disagree, bedrock-data says which one a file came
  from, and its README says where they differ.
- **Exact builds only.** A build is what its zip is named (`1.21.42.01`, never `1.21.42.1`); a zip that is not at its
  URL is a missing platform, never stood in for by a redirect or another build. The channel is the download's, never
  guessed from the number. Only the zip `versions/` hashed is read.
- **Schemas and checks first.** `versions/` is checked against `schemas/` here; bedrock-data's files against
  `BEDROCK_DATA_DIR/schemas/` (the tests copy them from there), and extracted data against `src/checks.ts`. A new field
  or file changes its schema, and for bedrock-data its README, together with the code that writes it.
- **Previews are tracked, never supported.**
- **The agent binds, it never guesses.** Each build's bindings are steps in `native/bindings/`; a pattern must match
  once, a slot and an offset are checked by what the game answers (`src/checks.ts`). A build the agent cannot bind
  fails support, never exports a wrong value.

## Conventions

- Node 24 runs `src/*.ts` directly: erasable TypeScript only (no enums, no parameter properties), `.ts` in imports.
- Match the code around you: function declarations, comments that say what a thing is for, no comment noise.
- No one-line `if`/`for`/`while`: a block always has braces (oxlint's `curly`; in `native/`, clang-format's
  `InsertBraces`). In TypeScript a blank line goes before and after it (`@stylistic/padding-line-between-statements`).
- `pnpm verify && pnpm test` before a commit. A test that needs a zip writes a small one (`test/helpers.ts`
  `writeZip`, `cachedRelease`), one that needs a release's blocks a stand-in for the agent (`fakeBlocks`); none
  downloads or starts a server.
- `native/` is C++20, formatted by clang-format (`.clang-format`); `pnpm native` builds it.
